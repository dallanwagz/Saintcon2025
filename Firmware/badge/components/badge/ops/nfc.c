#include <stdlib.h>
#include "esp_log.h"

#include "nfc.h"
#include "led_patterns.h"
#include "badge/ops_nfc.h"

static const char *TAG = "badge/ops/nfc";

static void log_records(void) {
    nfc_ndef_record_t *records = NULL;
    size_t count               = 0;
    if (nfc_get_records(&records, &count) != NFC_OK) {
        ESP_LOGE(TAG, "Failed to read NDEF records");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        char *text = NULL;
        char *lang = NULL;
        if (nfc_decode_text_record(&records[i], &text, &lang) == NFC_OK && text) {
            ESP_LOGI(TAG, "NDEF text[%u]: '%s'", (unsigned)i, text);
        } else {
            ESP_LOGI(TAG, "NDEF record[%u]: type=%s len=%u", (unsigned)i, records[i].type ? records[i].type : "?",
                     (unsigned)records[i].payload_len);
        }
        free(text);
        free(lang);
    }
    nfc_free_records(records, count);
}

static void badge_nfc_event_cb(nfc_event_t event, const uint8_t *data, size_t len, void *arg) {
    (void)data;
    (void)len;
    (void)arg;
    switch (event) {
        case NFC_EVENT_FIELD_DETECTED: ESP_LOGI(TAG, "NFC field detected"); break;
        case NFC_EVENT_FIELD_LOST: ESP_LOGI(TAG, "NFC field lost"); break;
        case NFC_EVENT_TAG_WRITE:
            ESP_LOGI(TAG, "NFC tag written by a reader");
            led_pattern_sparkles();
            log_records();
            break;
        case NFC_EVENT_WRITE_BLOCKED: ESP_LOGW(TAG, "NFC write rate-limited"); break;
        case NFC_EVENT_ERROR:
            ESP_LOGE(TAG, "NFC error");
            led_pattern_flash_red();
            break;
        default: break;
    }
}

esp_err_t badge_nfc_init(void) {
    nfc_set_event_callback(badge_nfc_event_cb, NULL);
    nfc_default_record_t def = {.type = NFC_DEFAULT_URL, .url = BADGE_NFC_URL};
    if (nfc_init_with_default(&def) != NFC_OK) {
        ESP_LOGE(TAG, "Failed to initialize NFC");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Badge NFC initialized");
    return ESP_OK;
}

void badge_nfc_deinit(void) {
    nfc_deinit();
}
