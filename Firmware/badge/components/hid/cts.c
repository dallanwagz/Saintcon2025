// Clock from the phone: iOS exposes the Bluetooth Current Time Service (0x1805) to a bonded accessory on the
// same link the HID connection uses, so the badge reads Current Time (0x2A2B) once per encrypted connection and
// sets its clock from it. The badge has no RTC backup (2xAA through a slide switch), so this runs at every
// reconnect. Macs offer no CTS: the discovery simply finds nothing. The value is the phone's local time, stored
// as the badge's UTC (TZ is never set), so the status bar shows it unchanged.
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "cts.h"

static const char *TAG = "cts";

static int on_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) {
    (void)arg;
    uint8_t b[10] = {0};
    if (err->status != 0 || !attr || OS_MBUF_PKTLEN(attr->om) < 7) {
        ESP_LOGW(TAG, "Current Time read failed (status %d)", err->status);
        return 0;
    }
    ble_hs_mbuf_to_flat(attr->om, b, sizeof(b), NULL);
    struct tm t = {
        .tm_year = (b[0] | (b[1] << 8)) - 1900,
        .tm_mon  = b[2] - 1,
        .tm_mday = b[3],
        .tm_hour = b[4],
        .tm_min  = b[5],
        .tm_sec  = b[6],
    };
    if (t.tm_year < 2024 - 1900) {
        ESP_LOGW(TAG, "phone sent an unset time (year %d)", t.tm_year + 1900);
        return 0;
    }
    struct timeval tv = {.tv_sec = mktime(&t)};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "clock set from the phone: %04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg) {
    (void)arg;
    if (err->status == 0 && chr) {
        ble_gattc_read(conn, chr->val_handle, on_read, NULL);
    } else if (err->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "Current Time characteristic discovery failed (status %d)", err->status);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg) {
    (void)arg;
    if (err->status == 0 && svc) {
        ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle, BLE_UUID16_DECLARE(0x2A2B), on_chr, NULL);
    } else if (err->status != BLE_HS_EDONE) {
        ESP_LOGD(TAG, "no Current Time Service on this host (status %d)", err->status);
    }
    return 0;
}

void cts_sync(uint16_t conn_handle) {
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, BLE_UUID16_DECLARE(0x1805), on_svc, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "service discovery rc=%d", rc);
    }
}
