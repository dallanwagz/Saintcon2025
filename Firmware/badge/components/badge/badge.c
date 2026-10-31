#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "badge.h"
#include "display.h"

static const char *TAG = "badge";

static void apply_screen_timeout(void) {
    if (badge_config.screen_timeout > 0) {
        set_screen_timeout_config(SCREEN_TIMEOUT_DIM, 20, badge_config.screen_timeout * 1000);
        screen_timeout_start();
    } else {
        screen_timeout_stop(true);
    }
}

esp_err_t badge_init(void) {
    esp_err_t err = load_badge_config();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Loading default badge config due to error");
        badge_config = BADGE_DEFAULTS;
    }
    ESP_LOGI(TAG, "Badge config: version=%lu, hw_pass=%d, screen_brightness=%d, screen_timeout=%lu, led_brightness=%d",
             badge_config.version, badge_config.hw_pass, badge_config.screen_brightness, badge_config.screen_timeout,
             badge_config.led_brightness);

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware %s (%s), IDF %s, built %s %s", app_desc->version, app_desc->project_name, app_desc->idf_ver,
             app_desc->date, app_desc->time);

    // Mount SPIFFS on the storage partition (screenshots, future assets)
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,
        .max_files              = 5,
        .format_if_mount_failed = true,
    };
    err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(err));
    } else {
        size_t total = 0, used = 0;
        if (esp_spiffs_info(NULL, &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS: %u/%u bytes used", used, total);
        }
    }

    apply_screen_timeout();
    return ESP_OK;
}

esp_err_t set_screen_timeout(uint32_t timeout) {
    badge_config.screen_timeout = timeout;
    apply_screen_timeout();
    return save_badge_config();
}
