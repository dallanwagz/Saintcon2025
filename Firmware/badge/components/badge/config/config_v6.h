#pragma once

#define BADGE_NAME_LEN 7 // what fits as the local name in a BTHome button advert (and across the status bar)

typedef struct {
    uint32_t version;
    bool hw_pass;              // Hardware self-test has been passed at least once
    uint8_t screen_brightness; // Backlight level (0-255)
    uint32_t screen_timeout;   // Screen dim timeout in seconds (0 = never)
    uint8_t led_brightness;    // Global LED brightness (0-255)
    uint8_t led_pattern;       // Idle LED pattern (led_idle_pattern_t)
    uint8_t hid_profile;       // Active HID profile (hid_profile_t)
    uint8_t usb_hid;           // 1 = start the USB keyboard/mouse (TinyUSB) at boot; takes the USB port from the console
    uint8_t hid_autostart;     // 1 = open HID Remote and resume hid_profile right after boot (hold centre 1 s to leave)
    char name[BADGE_NAME_LEN + 1]; // Owner's name, typed in Settings: status bar, "<name> Wrench" over Bluetooth/USB, BTHome
} badge_config_v6_t;

// clang-format off
#define BADGE_DEFAULTS_V6                       \
    (badge_config_v6_t){                        \
        .version           = 6,                 \
        .hw_pass           = false,             \
        .screen_brightness = LCD_BACKLIGHT_ON,  \
        .screen_timeout    = 30,                \
        .led_brightness    = 64,                \
        .led_pattern       = 0,                 \
        .hid_profile       = 0,                 \
        .usb_hid           = 0,                 \
        .hid_autostart     = 0,                 \
        .name              = "",                \
    }
// clang-format on
