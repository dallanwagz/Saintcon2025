#include <string.h>
#include "esp_log.h"

#include "config.h"
#include "migrate.h"

static const char *TAG = "badge/config [migrate]";

/**
 * @brief Migrates a stored badge configuration to the current version and saves it.
 *
 * @param nvs_handle Open NVS handle for the badge namespace
 * @param stored_version The version of the stored configuration
 */
void migrate_badge_config(nvs_handle_t nvs_handle, uint32_t stored_version) {
    badge_config_t cfg = BADGE_DEFAULTS;

    if (stored_version == 1) {
        badge_config_v1_t v1;
        size_t len = sizeof(v1);
        if (nvs_get_blob(nvs_handle, "badge_config", &v1, &len) == ESP_OK && len == sizeof(v1)) {
            cfg.hw_pass           = v1.hw_pass;
            cfg.screen_brightness = v1.screen_brightness;
            cfg.screen_timeout    = v1.screen_timeout;
            ESP_LOGI(TAG, "Migrated config v1 -> v%d", BADGE_CONFIG_VERSION);
        } else {
            ESP_LOGW(TAG, "Could not read v1 config, using defaults");
        }
    } else if (stored_version == 2) {
        badge_config_v2_t v2;
        size_t len = sizeof(v2);
        if (nvs_get_blob(nvs_handle, "badge_config", &v2, &len) == ESP_OK && len == sizeof(v2)) {
            cfg.hw_pass           = v2.hw_pass;
            cfg.screen_brightness = v2.screen_brightness;
            cfg.screen_timeout    = v2.screen_timeout;
            cfg.led_brightness    = v2.led_brightness;
            ESP_LOGI(TAG, "Migrated config v2 -> v%d", BADGE_CONFIG_VERSION);
        } else {
            ESP_LOGW(TAG, "Could not read v2 config, using defaults");
        }
    } else if (stored_version == 3) {
        badge_config_v3_t v3;
        size_t len = sizeof(v3);
        if (nvs_get_blob(nvs_handle, "badge_config", &v3, &len) == ESP_OK && len == sizeof(v3)) {
            cfg.hw_pass           = v3.hw_pass;
            cfg.screen_brightness = v3.screen_brightness;
            cfg.screen_timeout    = v3.screen_timeout;
            cfg.led_brightness    = v3.led_brightness;
            cfg.led_pattern       = v3.led_pattern;
            cfg.hid_profile       = v3.hid_profile;
            ESP_LOGI(TAG, "Migrated config v3 -> v%d", BADGE_CONFIG_VERSION);
        } else {
            ESP_LOGW(TAG, "Could not read v3 config, using defaults");
        }
    } else if (stored_version == 4) {
        badge_config_v4_t v4;
        size_t len = sizeof(v4);
        if (nvs_get_blob(nvs_handle, "badge_config", &v4, &len) == ESP_OK && len == sizeof(v4)) {
            cfg.hw_pass           = v4.hw_pass;
            cfg.screen_brightness = v4.screen_brightness;
            cfg.screen_timeout    = v4.screen_timeout;
            cfg.led_brightness    = v4.led_brightness;
            cfg.led_pattern       = v4.led_pattern;
            cfg.hid_profile       = v4.hid_profile;
            cfg.usb_hid           = v4.usb_hid;
            ESP_LOGI(TAG, "Migrated config v4 -> v%d", BADGE_CONFIG_VERSION);
        } else {
            ESP_LOGW(TAG, "Could not read v4 config, using defaults");
        }
    } else if (stored_version == 5) {
        badge_config_v5_t v5;
        size_t len = sizeof(v5);
        if (nvs_get_blob(nvs_handle, "badge_config", &v5, &len) == ESP_OK && len == sizeof(v5)) {
            cfg.hw_pass           = v5.hw_pass;
            cfg.screen_brightness = v5.screen_brightness;
            cfg.screen_timeout    = v5.screen_timeout;
            cfg.led_brightness    = v5.led_brightness;
            cfg.led_pattern       = v5.led_pattern;
            cfg.hid_profile       = v5.hid_profile;
            cfg.usb_hid           = v5.usb_hid;
            cfg.hid_autostart     = v5.hid_autostart;
            ESP_LOGI(TAG, "Migrated config v5 -> v%d", BADGE_CONFIG_VERSION);
        } else {
            ESP_LOGW(TAG, "Could not read v5 config, using defaults");
        }
    } else {
        ESP_LOGW(TAG, "Unknown stored config version %lu, using defaults", stored_version);
    }

    badge_config = cfg;
    save_badge_config();
}
