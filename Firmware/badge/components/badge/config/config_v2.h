#pragma once

typedef struct {
    uint32_t version;
    bool hw_pass;              // Hardware self-test has been passed at least once
    uint8_t screen_brightness; // Backlight level (0-255)
    uint32_t screen_timeout;   // Screen dim timeout in seconds (0 = never)
    uint8_t led_brightness;    // Global LED brightness (0-255)
} badge_config_v2_t;
// clang-format off
#define BADGE_DEFAULTS_V2                       \
    (badge_config_v2_t){                        \
        .version           = 2,                 \
        .hw_pass           = false,             \
        .screen_brightness = LCD_BACKLIGHT_ON,  \
        .screen_timeout    = 30,                \
        .led_brightness    = 64,                \
    }
// clang-format on
