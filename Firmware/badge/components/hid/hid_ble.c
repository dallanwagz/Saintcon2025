#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_hidd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

#include "hid.h"
#include "hid_internal.h"

static const char *TAG = "hid_ble";

#include "hid_report_map.h"

static esp_hid_raw_report_map_t report_maps[] = {{.data = hid_report_map, .len = sizeof(hid_report_map)}};

static esp_hid_device_config_t hid_config = {
    .vendor_id         = 0x16C0, // generic test VID; replace if this ever ships
    .product_id        = 0x05DF,
    .version           = 0x0100,
    .device_name       = CONFIG_BADGE_NAME,
    .manufacturer_name = "SAINTCON",
    .serial_number     = "2025",
    .report_maps       = report_maps,
    .report_maps_len   = 1,
};

static esp_hidd_dev_t *s_dev          = NULL;
static hid_state_t s_state            = HID_STATE_IDLE;
static bool s_suspended               = false;
static uint16_t s_conn_handle         = BLE_HS_CONN_HANDLE_NONE; // the HID host link (not another role's link)
static hid_state_cb_t s_state_cb      = NULL;
static struct ble_hs_adv_fields s_adv = {0};
static ble_uuid16_t s_hid_uuid        = BLE_UUID16_INIT(0x1812);
static char s_name[32]                = CONFIG_BADGE_NAME;
static uint8_t s_battery              = 100;

void ble_store_config_init(void); // provided by the bt component when NimBLE is enabled

static void set_state(hid_state_t st) {
    if (st == s_state) {
        return;
    }
    s_state = st;
    ESP_LOGI(TAG, "state -> %s", st == HID_STATE_ADVERTISING ? "advertising" : st == HID_STATE_CONNECTED ? "connected" : "idle");
    if (s_state_cb) {
        s_state_cb(hid_get_state()); // USB, when mounted, outranks whatever BLE is doing
    }
}

void hid_transport_changed(void) {
    if (s_state_cb) {
        s_state_cb(hid_get_state());
    }
}

static int gap_event(struct ble_gap_event *event, void *arg);

static esp_err_t adv_start(void) {
    if (s_suspended) {
        return ESP_OK; // someone else owns the radio right now
    }
    int rc = ble_gap_adv_set_fields(&s_adv);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc);
        return ESP_FAIL;
    }
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min  = BLE_GAP_ADV_ITVL_MS(30), // HID hosts like 30-50 ms while pairing; ponytail: slow this down later for battery
        .itvl_max  = BLE_GAP_ADV_ITVL_MS(50),
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
        return ESP_FAIL;
    }
    set_state(HID_STATE_ADVERTISING);
    return ESP_OK;
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "connection %s (status=%d)", event->connect.status == 0 ? "established" : "failed",
                     event->connect.status);
            if (event->connect.status != 0) {
                adv_start();
            } else {
                s_conn_handle = event->connect.conn_handle;
                // Ask the host to pair right away: HID reports live behind encryption, and a host that only
                // "connects" (no bond) would otherwise sit there receiving nothing.
                int rc = ble_gap_security_initiate(event->connect.conn_handle);
                ESP_LOGI(TAG, "security request rc=%d", rc);
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            break; // esp_hidd reports this too; advertising restarts from the HIDD callback
        case BLE_GAP_EVENT_ADV_COMPLETE: // a hid_broadcast() burst ended: HID advertising only matters while unconnected
            if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
                adv_start();
            }
            break;
        case BLE_GAP_EVENT_ENC_CHANGE: {
            struct ble_gap_conn_desc desc;
            bool bonded = ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 && desc.sec_state.bonded;
            ESP_LOGI(TAG, "encryption change status=%d encrypted=%d bonded=%d", event->enc_change.status,
                     event->enc_change.status == 0, bonded);
            break;
        }
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "subscribe attr=%d notify=%d indicate=%d", event->subscribe.attr_handle, event->subscribe.cur_notify,
                     event->subscribe.cur_indicate);
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // The host lost its bond and wants to pair again: drop ours and let it
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            // NO_IO capability means Just Works; accept a numeric comparison if a host insists
            struct ble_sm_io pkey = {.action = event->passkey.params.action};
            if (pkey.action == BLE_SM_IOACT_NUMCMP) {
                pkey.numcmp_accept = 1;
            } else if (pkey.action == BLE_SM_IOACT_DISP || pkey.action == BLE_SM_IOACT_INPUT) {
                pkey.passkey = 0;
            }
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            break;
        }
        default: break;
    }
    return 0;
}

static void hidd_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    (void)handler_args;
    (void)base;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    // esp_hidd's NimBLE backend treats *every* GAP connection as a HID host. While another BLE role owns the
    // radio those events describe its link, so they must not touch our state or restart advertising.
    if (s_suspended && (id == ESP_HIDD_CONNECT_EVENT || id == ESP_HIDD_DISCONNECT_EVENT)) {
        return;
    }
    switch ((esp_hidd_event_t)id) {
        case ESP_HIDD_START_EVENT:
            ESP_LOGI(TAG, "HID device started");
            ble_svc_gap_device_name_set(s_name);
            adv_start();
            break;
        case ESP_HIDD_CONNECT_EVENT: set_state(HID_STATE_CONNECTED); break;
        case ESP_HIDD_PROTOCOL_MODE_EVENT:
            ESP_LOGI(TAG, "protocol mode: %s", param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
            break;
        case ESP_HIDD_CONTROL_EVENT: ESP_LOGI(TAG, "control: %ssuspend", param->control.control ? "exit " : ""); break;
        case ESP_HIDD_OUTPUT_EVENT: ESP_LOGD(TAG, "output report id=%u len=%u", param->output.report_id, param->output.length); break;
        case ESP_HIDD_DISCONNECT_EVENT:
            ESP_LOGI(TAG, "disconnected: %s",
                     esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
            adv_start();
            break;
        case ESP_HIDD_STOP_EVENT: set_state(HID_STATE_IDLE); break;
        default: break;
    }
}

static void host_task(void *param) {
    (void)param;
    nimble_port_run(); // returns only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

esp_err_t hid_init(const char *device_name) {
    if (s_dev) {
        return ESP_OK;
    }
    if (device_name) {
        snprintf(s_name, sizeof(s_name), "%s", device_name);
    }
    hid_config.device_name = s_name;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG, "controller init");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_BLE), TAG, "controller enable");
    ESP_RETURN_ON_ERROR(esp_nimble_init(), TAG, "nimble init");

    // Advertising payload: flags, keyboard appearance, name, HID service UUID
    memset(&s_adv, 0, sizeof(s_adv));
    s_adv.flags                  = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_adv.appearance             = ESP_HID_APPEARANCE_KEYBOARD;
    s_adv.appearance_is_present  = 1;
    s_adv.tx_pwr_lvl_is_present  = 1;
    s_adv.tx_pwr_lvl             = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    s_adv.name                   = (uint8_t *)s_name;
    s_adv.name_len               = strlen(s_name);
    s_adv.name_is_complete       = 1;
    s_adv.uuids16                = &s_hid_uuid;
    s_adv.num_uuids16            = 1;
    s_adv.uuids16_is_complete    = 1;

    // Security: Just Works bonding (no passkey UI on the badge), secure connections, keys distributed both ways
    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    ESP_RETURN_ON_ERROR(esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE, hidd_event, &s_dev), TAG, "hidd init");

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ESP_RETURN_ON_ERROR(esp_nimble_enable(host_task), TAG, "nimble enable");
    ESP_LOGI(TAG, "BLE HID '%s' starting", s_name);
    return ESP_OK;
}

hid_state_t hid_get_state(void) {
    return hid_usb_mounted() ? HID_STATE_USB_CONNECTED : s_state;
}

void hid_set_state_callback(hid_state_cb_t cb) {
    s_state_cb = cb;
}

static void send_report(uint8_t report_id, uint8_t *data, size_t len) {
    if (hid_usb_mounted()) { // a wired host wins while it has us
        hid_usb_send_report(report_id, data, len);
        return;
    }
    if (!s_dev || s_suspended || !esp_hidd_dev_connected(s_dev)) {
        return;
    }
    esp_err_t err = esp_hidd_dev_input_set(s_dev, 0, report_id, data, len);
    ESP_LOGI(TAG, "report %u [%02x %02x ..] -> %s", report_id, data[0], len > 1 ? data[1] : 0, esp_err_to_name(err));
}

void hid_key_press(uint8_t modifiers, uint8_t keycode) {
    uint8_t r[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0};
    send_report(HID_RPT_KEYBOARD, r, sizeof(r));
}

void hid_key_release(void) {
    uint8_t r[8] = {0};
    send_report(HID_RPT_KEYBOARD, r, sizeof(r));
}

void hid_key_tap(uint8_t modifiers, uint8_t keycode) {
    hid_key_press(modifiers, keycode);
    vTaskDelay(pdMS_TO_TICKS(10));
    hid_key_release();
}

void hid_consumer_press(uint16_t usage) {
    uint8_t r[2] = {usage & 0xFF, usage >> 8};
    send_report(HID_RPT_CONSUMER, r, sizeof(r));
}

void hid_consumer_release(void) {
    uint8_t r[2] = {0, 0};
    send_report(HID_RPT_CONSUMER, r, sizeof(r));
}

void hid_consumer_tap(uint16_t usage) {
    hid_consumer_press(usage);
    vTaskDelay(pdMS_TO_TICKS(10));
    hid_consumer_release();
}

void hid_mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    uint8_t r[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
    send_report(HID_RPT_MOUSE, r, sizeof(r));
}

void hid_set_battery(uint8_t percent) {
    s_battery = percent > 100 ? 100 : percent;
    if (s_dev && esp_hidd_dev_connected(s_dev)) {
        esp_hidd_dev_battery_set(s_dev, percent > 100 ? 100 : percent);
    }
}

uint8_t hid_battery_percent(void) {
    return s_battery;
}

esp_err_t hid_broadcast(const uint8_t *adv, uint8_t len, uint32_t ms) {
    if (s_suspended) {
        return ESP_ERR_INVALID_STATE;
    }
    ble_gap_adv_stop(); // whatever is running: HID advertising (unconnected) or an earlier burst
    int rc = ble_gap_adv_set_data(adv, len);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data rc=%d", rc);
        return ESP_FAIL;
    }
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_NON,
        .disc_mode = BLE_GAP_DISC_MODE_NON,
        .itvl_min  = BLE_GAP_ADV_ITVL_MS(20),
        .itvl_max  = BLE_GAP_ADV_ITVL_MS(30),
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, ms, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "broadcast ble_gap_adv_start rc=%d", rc);
        if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            adv_start(); // give HID its advertising back
        }
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t hid_forget_bonds(void) {
    hid_disconnect_host();
    int rc = ble_store_clear();
    ESP_LOGI(TAG, "bonds cleared (rc=%d)", rc);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

// -------------------------------------------------------------------------------------------------
// Radio sharing + host management
// -------------------------------------------------------------------------------------------------

void hid_suspend(void) {
    if (s_suspended) {
        return;
    }
    s_suspended = true;
    ble_gap_adv_stop();
    hid_disconnect_host();
    set_state(HID_STATE_IDLE);
    ESP_LOGI(TAG, "suspended (radio handed over)");
}

void hid_resume(void) {
    if (!s_suspended) {
        return;
    }
    s_suspended = false;
    ESP_LOGI(TAG, "resumed");
    adv_start();
}

bool hid_suspended(void) {
    return s_suspended;
}

static void addr_to_str(const ble_addr_t *a, char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a->val[5], a->val[4], a->val[3], a->val[2], a->val[1], a->val[0]);
}

bool hid_host_addr(char out[18]) {
    struct ble_gap_conn_desc desc;
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(s_conn_handle, &desc) != 0) {
        return false;
    }
    addr_to_str(&desc.peer_id_addr, out);
    return true;
}

void hid_disconnect_host(void) {
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

int hid_bonded_hosts(char (*out)[18], int max) {
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, CONFIG_BT_NIMBLE_MAX_BONDS) != 0) {
        return 0;
    }
    if (n > max) {
        n = max;
    }
    for (int i = 0; i < n; i++) {
        addr_to_str(&peers[i], out[i]);
    }
    return n;
}

esp_err_t hid_forget_host(int index) {
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int n = 0;
    if (ble_store_util_bonded_peers(peers, &n, CONFIG_BT_NIMBLE_MAX_BONDS) != 0 || index < 0 || index >= n) {
        return ESP_ERR_NOT_FOUND;
    }
    int rc = ble_gap_unpair(&peers[index]); // drops the bond and the link if that peer is connected
    ESP_LOGI(TAG, "unpair #%d rc=%d", index, rc);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
