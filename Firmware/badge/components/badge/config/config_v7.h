#pragma once

// hid_autostart values (v5/v6 stored 0/1, which keep their meaning)
#define BOOT_APP_MENU   0
#define BOOT_APP_HID    1
#define BOOT_APP_HAMENU 2

// BADGE_NAME_LEN comes from config_v6.h

// The wrench LEDs' own look (the LEDs app wallpaper): the fields of a WLED segment that matter. Same meaning as
// wled_params_t in wled_fx.h, kept as a plain struct here so the config does not depend on the effect engine.
typedef struct {
    uint8_t fx, pal, sx, ix, c1, c2, c3; // WLED effect id (stock FX_MODE table), palette id, speed, intensity, custom 1-3
    uint8_t opts;                        // bit 0..2 = WLED option checkboxes o1..o3
    uint8_t col[3][3];                   // primary, secondary, tertiary RGB
    uint16_t vlen;                       // virtual segment length (0 = the 20 badge LEDs)
} badge_wled_look_t;

// Old v3-v6 led_pattern values (Off, White, Red, Green, Blue, Rainbow, Chase, Breathe) as a WLED look; returns on/off
bool badge_wled_look_legacy(uint8_t led_pattern, badge_wled_look_t *out);

typedef struct {
    uint32_t version;
    bool hw_pass;              // Hardware self-test has been passed at least once
    uint8_t screen_brightness; // Backlight level (0-255)
    uint32_t screen_timeout;   // Screen dim timeout in seconds (0 = never)
    uint8_t led_brightness;    // Global LED brightness (0-255)
    uint8_t led_pattern;       // Legacy idle pattern (v3-v6), only read by the v6 -> v7 migration
    uint8_t hid_profile;       // Active HID profile (hid_profile_t)
    uint8_t usb_hid;           // 1 = start the USB keyboard/mouse (TinyUSB) at boot; takes the USB port from the console
    uint8_t hid_autostart;     // app opened at boot: BOOT_APP_MENU / BOOT_APP_HID (resumes hid_profile) / BOOT_APP_HAMENU
    char name[BADGE_NAME_LEN + 1]; // Owner's name, typed in Settings: status bar, "<name> Wrench" over Bluetooth/USB, BTHome
    uint8_t led_mirror;        // 1 = the wrench LEDs echo the looks picked in HA Menu popups (Settings -> Wrench LEDs)
    uint8_t wled_on;           // 1 = the manual look below is shown, 0 = wrench LEDs dark (unless HA Menu mirrors)
    badge_wled_look_t wled;    // Manual look picked in the LEDs app
} badge_config_v7_t;

// clang-format off
#define BADGE_DEFAULTS_V7                       \
    (badge_config_v7_t){                        \
        .version           = 7,                 \
        .hw_pass           = false,             \
        .screen_brightness = LCD_BACKLIGHT_ON,  \
        .screen_timeout    = 30,                \
        .led_brightness    = 64,                \
        .led_pattern       = 0,                 \
        .hid_profile       = 0,                 \
        .usb_hid           = 0,                 \
        .hid_autostart     = 0,                 \
        .name              = "",                \
        .led_mirror        = 0,                 \
        .wled_on           = 0,                 \
        .wled              = {.fx = 9, .sx = 128, .ix = 128, .c1 = 128, .c2 = 128, .c3 = 16, \
                              .col = {{255, 160, 0}}}, /* Rainbow, WLED defaults */ \
    }
// clang-format on
