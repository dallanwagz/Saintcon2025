#pragma once

typedef struct {
    uint32_t version;
    bool hw_pass;              // Hardware self-test has been passed at least once
    uint8_t screen_brightness; // Backlight level (0-255)
    uint32_t screen_timeout;   // Screen dim timeout in seconds (0 = never)
    uint8_t led_brightness;    // Global LED brightness (0-255)
    uint8_t led_pattern;       // Idle LED pattern (led_idle_pattern_t)
    uint8_t hid_profile;       // Active HID profile (hid_profile_t)
    uint8_t usb_hid;           // 1 = start the USB keyboard/mouse (TinyUSB) at boot; takes the USB port from the console
} badge_config_v4_t;

// clang-format off
#define BADGE_DEFAULTS_V4                       \
    (badge_config_v4_t){                        \
        .version           = 4,                 \
        .hw_pass           = false,             \
        .screen_brightness = LCD_BACKLIGHT_ON,  \
        .screen_timeout    = 30,                \
        .led_brightness    = 64,                \
        .led_pattern       = 0,                 \
        .hid_profile       = 0,                 \
        .usb_hid           = 0,                 \
    }
// clang-format on
