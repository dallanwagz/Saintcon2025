#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "wifi_manager.h"

static const char *TAG = "wifi_manager";

#if CONFIG_WIFI_MANAGER_ENABLED

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"

#define WIFI_MAX_NETWORKS CONFIG_WIFI_MAX_NETWORKS

static wifi_status_t wifi_status                                              = WIFI_STATUS_DISCONNECTED;
static wifi_credentials_t wifi_credentials[WIFI_MAX_NETWORKS]                 = {0};
static wifi_status_callback_t wifi_status_callbacks[WIFI_STATUS_CALLBACK_MAX] = {0};

// Wifi event group
static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT        = BIT0;
static const int WIFI_DISCONNECT_BIT       = BIT1;
static const int WIFI_NOCONNECT_BIT        = BIT2;

static void notify_status(wifi_status_t status) {
    wifi_status = status;
    for (size_t i = 0; i < WIFI_STATUS_CALLBACK_MAX; i++) {
        if (wifi_status_callbacks[i] != NULL) {
            wifi_status_callbacks[i](status);
        }
    }
}

void wifi_force_reconnect() {
    ESP_LOGI(TAG, "Force WiFi reconnect requested");
    esp_wifi_disconnect();
    notify_status(WIFI_STATUS_DISCONNECTED);
}

// -------------------------------------------------------------------------------------------------
// Saved networks (NVS namespace "wifi": "count" + "cred<i>" blobs)
// -------------------------------------------------------------------------------------------------
static esp_err_t serialize_creds() {
    if (!nvs_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret;
    nvs_handle_t nvs;
    size_t count = 0;
    ESP_RETURN_ON_ERROR(nvs_open("wifi", NVS_READWRITE, &nvs), TAG, "Failed to open NVS");

    for (size_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "cred%d", i);
        if (strlen(wifi_credentials[i].ssid) == 0) {
            nvs_erase_key(nvs, key); // may not exist, ignore
            continue;
        }
        if ((ret = nvs_set_blob(nvs, key, &wifi_credentials[i], sizeof(wifi_credentials_t))) != ESP_OK) {
            nvs_close(nvs);
            return ret;
        }
        ++count;
    }
    if ((ret = nvs_set_u8(nvs, "count", (uint8_t)count)) != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }
    ret = nvs_commit(nvs);
    nvs_close(nvs);
    return ret;
}

static esp_err_t deserialize_creds() {
    if (!nvs_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t nvs;
    uint8_t count = 0;
    ESP_RETURN_ON_ERROR(nvs_open("wifi", NVS_READONLY, &nvs), TAG, "Failed to open NVS");
    if (nvs_get_u8(nvs, "count", &count) != ESP_OK) {
        nvs_close(nvs);
        return ESP_OK; // nothing saved yet
    }
    if (count > WIFI_MAX_NETWORKS) {
        count = WIFI_MAX_NETWORKS;
    }
    for (uint8_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "cred%d", i);
        size_t len = sizeof(wifi_credentials_t);
        nvs_get_blob(nvs, key, &wifi_credentials[i], &len);
    }
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t get_saved_wifi_networks(wifi_credentials_t *creds, uint8_t *count) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (strlen(wifi_credentials[i].ssid) > 0) {
            memcpy(&creds[n++], &wifi_credentials[i], sizeof(wifi_credentials_t));
        }
    }
    *count = n;
    return ESP_OK;
}

esp_err_t save_wifi_network(wifi_credentials_t *creds) {
    // Update the network if it already exists, otherwise add it
    for (size_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (strcmp(wifi_credentials[i].ssid, creds->ssid) == 0 || strlen(wifi_credentials[i].ssid) == 0) {
            memcpy(&wifi_credentials[i], creds, sizeof(wifi_credentials_t));
            esp_err_t ret = serialize_creds();
            if (wifi_event_group) {
                xEventGroupSetBits(wifi_event_group, WIFI_NOCONNECT_BIT); // wake the manager task to try it
            }
            return ret;
        }
    }
    ESP_LOGE(TAG, "No slots remaining to save WiFi network");
    return ESP_ERR_NO_MEM;
}

esp_err_t delete_wifi_network(char *ssid) {
    for (size_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
        if (strcmp(wifi_credentials[i].ssid, ssid) == 0) {
            memset(&wifi_credentials[i], 0, sizeof(wifi_credentials_t));
            return serialize_creds();
        }
    }
    ESP_LOGE(TAG, "No saved network with SSID %s", ssid);
    return ESP_ERR_NOT_FOUND;
}

// -------------------------------------------------------------------------------------------------
// Station setup
// -------------------------------------------------------------------------------------------------
static esp_err_t init_wifi_sta() {
    const esp_netif_t *const netif = esp_netif_create_default_wifi_sta();
    assert(netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to initialize WiFi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    // Disable WiFi power management to prevent beacon timeouts and disconnections seen on this hardware
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "Failed to disable WiFi power save");
    return ESP_OK;
}

static void time_sync_notification_cb(struct timeval *tv) {
    (void)tv;
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "SNTP time sync complete: %s", asctime(&timeinfo));
}

static esp_err_t init_sntp() {
    // Mountain Time
    setenv("TZ", "MST7MDT,M3.2.0,M11.1.0", 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("time.aws.com", "pool.ntp.org"));
    config.sync_cb           = time_sync_notification_cb;
    return esp_netif_sntp_init(&config);
}

static esp_err_t try_connect(const char *ssid, const char *password) {
    wifi_ap_record_t current_ap_info;
    if (esp_wifi_sta_get_ap_info(&current_ap_info) == ESP_OK && strcmp((char *)current_ap_info.ssid, ssid) == 0) {
        ESP_LOGI(TAG, "Already connected to %s", current_ap_info.ssid);
        return ESP_OK;
    }

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable    = true;
    wifi_config.sta.pmf_cfg.required   = false;

    ESP_RETURN_ON_ERROR(esp_wifi_disconnect(), TAG, "Failed to disconnect from current network");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config), TAG, "Failed to set WiFi configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "Connection failure while trying to connect to %s", ssid);
    notify_status(WIFI_STATUS_CONNECTING);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_DISCONNECT_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }
    xEventGroupSetBits(wifi_event_group, WIFI_NOCONNECT_BIT);
    return ESP_FAIL;
}

static void wifi_event_handler(void *_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)_arg;
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: ESP_LOGI(TAG, "WiFi station started"); break;
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
                ESP_LOGI(TAG, "WiFi connected to %s", event->ssid);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "WiFi disconnected from %s, reason: %d", event->ssid, event->reason);
                xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECT_BIT);
                notify_status(WIFI_STATUS_DISCONNECTED);
                break;
            }
            default: break;
        }
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi station got an IP address");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        notify_status(WIFI_STATUS_CONNECTED);
    }
}

static void wifi_manager_task(void *_arg) {
    (void)_arg;
    const TickType_t initialDelay = pdMS_TO_TICKS(1000);
    const TickType_t maxDelay     = pdMS_TO_TICKS(60000);
    TickType_t delay              = initialDelay;

    for (;;) {
        bool connected = false;
        for (size_t i = 0; i < WIFI_MAX_NETWORKS && !connected; i++) {
            if (strlen(wifi_credentials[i].ssid) == 0) {
                continue;
            }
            connected = try_connect(wifi_credentials[i].ssid, wifi_credentials[i].password) == ESP_OK;
        }
        if (connected) {
            delay = initialDelay;
        } else {
            ESP_LOGW(TAG, "No saved WiFi network reachable");
        }

        // Wait for a disconnect, a failed attempt, or a newly saved network; then retry with backoff
        xEventGroupWaitBits(wifi_event_group, WIFI_DISCONNECT_BIT | WIFI_NOCONNECT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        vTaskDelay(delay);
        delay = delay < maxDelay ? delay * 2 : maxDelay;
    }
}

void add_wifi_status_callback(wifi_status_callback_t cb) {
    for (size_t i = 0; i < WIFI_STATUS_CALLBACK_MAX; i++) {
        if (wifi_status_callbacks[i] == NULL) {
            wifi_status_callbacks[i] = cb;
            return;
        }
    }
    ESP_LOGE(TAG, "No slots remaining for WiFi status callbacks");
}

wifi_status_t get_wifi_status() {
    return wifi_status;
}

esp_err_t get_wifi_network_list(wifi_ap_record_t **networks, uint16_t *count) {
    wifi_scan_config_t scan_config = {0};
    scan_config.show_hidden        = true;
    scan_config.scan_type          = WIFI_SCAN_TYPE_ACTIVE;
    scan_config.scan_time.active   = (wifi_active_scan_time_t){.min = 100, .max = 200};

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_config, true), TAG, "Failed to start WiFi scan");
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(count), TAG, "Failed to get AP count");
    if (*count == 0) {
        *networks = NULL;
        return ESP_OK;
    }
    *networks = malloc(sizeof(wifi_ap_record_t) * *count);
    if (*networks == NULL) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    if (esp_wifi_scan_get_ap_records(count, *networks) != ESP_OK) {
        free(*networks);
        *networks = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_init() {
    ESP_LOGI(TAG, "Initializing WiFi manager");
    ESP_RETURN_ON_ERROR(nvs_init(), TAG, "Failed to initialize NVS");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize network stack");
    esp_err_t event_loop_result = esp_event_loop_create_default();
    if (event_loop_result != ESP_OK && event_loop_result != ESP_ERR_INVALID_STATE) {
        return event_loop_result;
    }
    ESP_RETURN_ON_ERROR(init_sntp(), TAG, "Failed to initialize SNTP");
    ESP_RETURN_ON_ERROR(init_wifi_sta(), TAG, "Failed to initialize WiFi");
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    deserialize_creds();
    wifi_event_group = xEventGroupCreate();
    xTaskCreate(wifi_manager_task, "wifi_manager_task", 4096, NULL, 5, NULL);
    return ESP_OK;
}

#else // !CONFIG_WIFI_MANAGER_ENABLED

esp_err_t get_saved_wifi_networks(wifi_credentials_t *creds, uint8_t *count) {
    (void)creds;
    *count = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t save_wifi_network(wifi_credentials_t *creds) {
    (void)creds;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t delete_wifi_network(char *ssid) {
    (void)ssid;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t get_wifi_network_list(wifi_ap_record_t **networks, uint16_t *count) {
    *networks = NULL;
    *count    = 0;
    return ESP_ERR_NOT_SUPPORTED;
}
void add_wifi_status_callback(wifi_status_callback_t cb) {
    (void)cb;
}
wifi_status_t get_wifi_status() {
    return WIFI_STATUS_DISCONNECTED;
}
void wifi_force_reconnect() {
}
esp_err_t wifi_manager_init() {
    ESP_LOGI(TAG, "WiFi manager disabled (CONFIG_WIFI_MANAGER_ENABLED=n)");
    return ESP_OK;
}

#endif // CONFIG_WIFI_MANAGER_ENABLED
