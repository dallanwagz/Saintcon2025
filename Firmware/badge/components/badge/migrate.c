#include <stddef.h>
#include <string.h>
#include "esp_log.h"

#include "config.h"
#include "migrate.h"

static const char *TAG = "badge/config [migrate]";

_Static_assert(offsetof(badge_config_v7_t, name) == offsetof(badge_config_v6_t, name), "v6 must be a prefix of v7");

bool badge_wled_look_legacy(uint8_t led_pattern, badge_wled_look_t *out) {
    // WLED defaults (Segment::setMode(fx, true) for these effects) plus a color; ids are the stock FX_MODE table
    *out = (badge_wled_look_t){.fx = 0, .sx = 128, .ix = 128, .c1 = 128, .c2 = 128, .c3 = 16};
    static const uint8_t solid[5][3] = {{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    switch (led_pattern) {
        case 1: case 2: case 3: case 4: // White, Red, Green, Blue -> Solid
            memcpy(out->col[0], solid[led_pattern], 3);
            return true;
        case 5: // Rainbow -> Rainbow
            out->fx = 9;
            return true;
        case 6: // Chase (bouncing dot with a tail, cycling color) -> Scanner on the Rainbow palette
            out->fx  = 40;
            out->c1  = 0; // Scanner's metadata default
            out->pal = 11;
            memcpy(out->col[0], solid[1], 3);
            return true;
        case 7: // Breathe (warm white) -> Breathe
            out->fx = 2;
            out->col[0][0] = 255, out->col[0][1] = 191, out->col[0][2] = 64;
            return true;
        default: // Off: keep the default look for when the LEDs are turned back on
            *out = BADGE_DEFAULTS.wled;
            return false;
    }
}

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
    } else if (stored_version == 6) {
        badge_config_v6_t v6;
        size_t len = sizeof(v6);
        if (nvs_get_blob(nvs_handle, "badge_config", &v6, &len) == ESP_OK && len == sizeof(v6)) {
            // ponytail: v7 only appends fields, so v6 is its prefix. Copy up to led_mirror, not sizeof(v6): v6's
            // tail padding overlaps the first appended v7 fields.
            memcpy(&cfg, &v6, offsetof(badge_config_v7_t, led_mirror));
            cfg.version = BADGE_CONFIG_VERSION; // the appended fields keep their BADGE_DEFAULTS values
            ESP_LOGI(TAG, "Migrated config v6 -> v%d", BADGE_CONFIG_VERSION);
        } else {
            ESP_LOGW(TAG, "Could not read v6 config, using defaults");
        }
    } else {
        ESP_LOGW(TAG, "Unknown stored config version %lu, using defaults", stored_version);
    }

    if (stored_version >= 3 && stored_version <= 6) { // these had an idle pattern: carry it over as a WLED look
        cfg.wled_on = badge_wled_look_legacy(cfg.led_pattern, &cfg.wled);
    }

    badge_config = cfg;
    save_badge_config();
}
