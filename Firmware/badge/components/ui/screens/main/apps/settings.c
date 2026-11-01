#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"

#include "badge.h"
#include "display.h"
#include "led.h"
#include "nav.h"
#include "text_input.h"
#include "theme.h"
#include "ui.h"
#include "version.h"

#include "../content.h"
#include "../../../../hid/include/hid.h" // the hid component (apps/hid.h shadows the plain name)

static const char *TAG __attribute__((unused)) = "screens/main [apps/settings]";

// -------------------------------------------------------------------------------------------------
// Internal State
// -------------------------------------------------------------------------------------------------
static nav_ctx_t *settings_nav_ctx = NULL;
static lv_obj_t *settings_list     = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_label  = NULL;
static lv_obj_t *timeout_slider    = NULL;
static lv_obj_t *timeout_label     = NULL;
static lv_obj_t *led_slider        = NULL;
static lv_obj_t *led_label         = NULL;
static lv_obj_t *name_input        = NULL;

// Bluetooth section: manage the HID host link and its bonds from the badge side
#define BT_MAX_BUTTONS 12
static lv_obj_t *bt_buttons[BT_MAX_BUTTONS];
static int bt_button_count    = 0;
static bool s_focus_bt        = false; // focus the Bluetooth section after a rebuild
static lv_obj_t *s_bt_armed   = NULL;  // destructive button waiting for its second press
static char s_bt_armed_text[40];

// LVGL moves a slider on LEFT/RIGHT before our KEY handler runs, so each slider stages the new value in
// VALUE_CHANGED, reverts the widget, and commits (apply + persist) from the KEY handler.
typedef struct {
    int16_t last;
    int16_t staged;
    bool has_staged;
    bool internal;
    void (*commit)(int32_t value);
} slider_guard_t;

// Timeout values in seconds
static const uint32_t timeout_values[] = {30, 60, 90, 120};
static const size_t timeout_count      = sizeof(timeout_values) / sizeof(timeout_values[0]);

// Brightness mapping: 1-100 percentage to actual backlight values
#define BRIGHTNESS_MIN_PERCENT 1
#define BRIGHTNESS_MAX_PERCENT 100
#define BRIGHTNESS_MIN_VALUE   LCD_BACKLIGHT_ON / 3 // Minimum actual brightness
#define BRIGHTNESS_MAX_VALUE   255

static int32_t percent_to_brightness(int32_t percent) {
    if (percent < BRIGHTNESS_MIN_PERCENT) {
        percent = BRIGHTNESS_MIN_PERCENT;
    }
    if (percent > BRIGHTNESS_MAX_PERCENT) {
        percent = BRIGHTNESS_MAX_PERCENT;
    }
    return BRIGHTNESS_MIN_VALUE + ((percent - BRIGHTNESS_MIN_PERCENT) * (BRIGHTNESS_MAX_VALUE - BRIGHTNESS_MIN_VALUE)) /
                                      (BRIGHTNESS_MAX_PERCENT - BRIGHTNESS_MIN_PERCENT);
}

static int32_t brightness_to_percent(int32_t value) {
    if (value < BRIGHTNESS_MIN_VALUE) {
        value = BRIGHTNESS_MIN_VALUE;
    }
    if (value > BRIGHTNESS_MAX_VALUE) {
        value = BRIGHTNESS_MAX_VALUE;
    }
    return BRIGHTNESS_MIN_PERCENT + ((value - BRIGHTNESS_MIN_VALUE) * (BRIGHTNESS_MAX_PERCENT - BRIGHTNESS_MIN_PERCENT)) /
                                        (BRIGHTNESS_MAX_VALUE - BRIGHTNESS_MIN_VALUE);
}

static void format_timeout_string(char *buf, size_t buf_size, uint32_t seconds) {
    if (seconds < 60) {
        snprintf(buf, buf_size, "%lu sec", (unsigned long)seconds);
    } else if (seconds % 60 == 0) {
        snprintf(buf, buf_size, "%lu min", (unsigned long)(seconds / 60));
    } else {
        snprintf(buf, buf_size, "%lu min %lu sec", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
    }
}

// -------------------------------------------------------------------------------------------------
// Commit handlers (apply + persist + update the value label)
// -------------------------------------------------------------------------------------------------
static void commit_brightness(int32_t percent) {
    int32_t value                  = percent_to_brightness(percent);
    badge_config.screen_brightness = (uint8_t)value;
    save_badge_config();
    set_backlight((uint8_t)value);
    lv_label_set_text_fmt(brightness_label, "%ld%%", (long)percent);
}

static void commit_timeout(int32_t index) {
    set_screen_timeout(timeout_values[index]);
    char buf[32];
    format_timeout_string(buf, sizeof(buf), timeout_values[index]);
    lv_label_set_text(timeout_label, buf);
}

static void commit_led(int32_t percent) {
    badge_config.led_brightness = (uint8_t)((percent * 255) / 100);
    save_badge_config();
    led_set_brightness(badge_config.led_brightness);
    lv_label_set_text_fmt(led_label, "%ld%%", (long)percent);
}

// -------------------------------------------------------------------------------------------------
// Event Callbacks
// -------------------------------------------------------------------------------------------------
static void scroll_to_focused_cb(lv_event_t *e) {
    lv_obj_t *target     = lv_event_get_target(e);
    lv_obj_t *alt_object = lv_event_get_user_data(e);
    if (settings_list && lv_obj_is_valid(settings_list)) {
        lv_obj_scroll_to_view_recursive(alt_object ? alt_object : target, LV_ANIM_ON);
    }
}

static void slider_event_cb(lv_event_t *e) {
    lv_obj_t *slider      = lv_event_get_target(e);
    slider_guard_t *guard = (slider_guard_t *)lv_obj_get_user_data(slider);
    lv_event_code_t code  = lv_event_get_code(e);
    if (!guard) {
        return;
    }

    if (code == LV_EVENT_FOCUSED) {
        guard->last       = lv_slider_get_value(slider);
        guard->has_staged = false;
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (guard->has_staged || guard->internal) {
            return;
        }
        guard->staged     = lv_slider_get_value(slider);
        guard->has_staged = true;
        guard->internal   = true;
        lv_slider_set_value(slider, guard->last, LV_ANIM_OFF);
        guard->internal = false;
        return;
    }

    if (code == LV_EVENT_KEY) {
        int32_t key = lv_event_get_key(e);
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            if (guard->internal) {
                return;
            }
            int32_t value = guard->has_staged ? guard->staged : lv_slider_get_value(slider);
            if (lv_slider_get_value(slider) != value) {
                guard->internal = true;
                lv_slider_set_value(slider, value, LV_ANIM_OFF);
                guard->internal = false;
            }
            guard->last       = value;
            guard->has_staged = false;
            guard->commit(value);
            return;
        }
        if ((key == LV_KEY_UP || key == LV_KEY_DOWN) && settings_nav_ctx && lv_group_get_editing(settings_nav_ctx->group)) {
            // Leaving edit mode vertically: make sure the widget shows the committed value
            if (lv_slider_get_value(slider) != guard->last) {
                guard->has_staged = false;
                guard->internal   = true;
                lv_slider_set_value(slider, guard->last, LV_ANIM_OFF);
                guard->internal = false;
                lv_event_stop_processing(e);
            }
        }
    }
}

static lv_obj_t *create_setting_row(lv_obj_t *parent, const char *label_text);

// -------------------------------------------------------------------------------------------------
// Bluetooth actions
// -------------------------------------------------------------------------------------------------
static void bt_disarm(void) {
    if (s_bt_armed && lv_obj_is_valid(s_bt_armed)) {
        lv_label_set_text(lv_obj_get_child(s_bt_armed, 0), s_bt_armed_text);
    }
    s_bt_armed = NULL;
}

// First press arms the button ("Sure? ..."), the second fires; moving focus away cancels.
static bool bt_confirmed(lv_obj_t *btn) {
    if (s_bt_armed == btn) {
        s_bt_armed = NULL;
        return true;
    }
    bt_disarm();
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    snprintf(s_bt_armed_text, sizeof(s_bt_armed_text), "%s", lv_label_get_text(label));
    lv_label_set_text_fmt(label, "Sure? %s", s_bt_armed_text);
    s_bt_armed = btn;
    return false;
}

static void bt_defocus_cb(lv_event_t *e) {
    if (lv_event_get_target(e) == s_bt_armed) {
        bt_disarm();
    }
}

static void bt_rebuild(void) {
    s_focus_bt = true;
    ui_request_rerender();
}

static void bt_toggle_cb(lv_event_t *e) {
    (void)e;
    if (hid_suspended()) {
        hid_resume();
    } else {
        hid_suspend();
    }
    bt_rebuild();
}

static void bt_disconnect_cb(lv_event_t *e) {
    (void)e;
    hid_disconnect_host(); // the HID state callback re-renders Settings once the link is down
}

static void bt_forget_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    if (!bt_confirmed(btn)) {
        return;
    }
    hid_forget_host((int)(intptr_t)lv_obj_get_user_data(btn));
    bt_rebuild();
}

static void bt_forget_all_cb(lv_event_t *e) {
    if (!bt_confirmed(lv_event_get_target(e))) {
        return;
    }
    hid_forget_bonds();
    bt_rebuild();
}

static lv_obj_t *create_action_button(lv_obj_t *row, const char *text, lv_event_cb_t cb, int user_data, bool danger) {
    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(GRAY_SHADE_6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 3, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(btn, lv_color_hex(BLUE_MAIN), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(btn, 2, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_user_data(btn, (void *)(intptr_t)user_data);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, bt_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(btn, scroll_to_focused_cb, LV_EVENT_FOCUSED, row);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(danger ? RED_MAIN : WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);

    if (bt_button_count < BT_MAX_BUTTONS) {
        bt_buttons[bt_button_count++] = btn;
    }
    return btn;
}

static void usb_restart_cb(lv_timer_t *t) {
    (void)t;
    esp_restart(); // the shutdown handler puts the USB PHY back on USB-Serial/JTAG on the way down
}

static void usb_toggle_cb(lv_event_t *e) {
    (void)e;
    badge_config.usb_hid = !badge_config.usb_hid;
    save_badge_config();
    if (badge_config.usb_hid) {
        hid_usb_start();
        bt_rebuild();
    } else {
        // TinyUSB has no clean runtime teardown while a host holds the port: reboot instead
        lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), 0), "Rebooting...");
        lv_timer_set_repeat_count(lv_timer_create(usb_restart_cb, 400, NULL), 1);
    }
}

static void autostart_toggle_cb(lv_event_t *e) {
    (void)e;
    badge_config.hid_autostart = !badge_config.hid_autostart;
    save_badge_config();
    bt_rebuild();
}

// Owner's name: typed with the stick, saved to NVS, applied right away (status bar, Bluetooth/USB name, BTHome)
static void name_changed_cb(const char *text, void *user_data) {
    (void)user_data;
    snprintf(badge_config.name, sizeof(badge_config.name), "%s", text);
    for (int i = strlen(badge_config.name) - 1; i >= 0 && badge_config.name[i] == ' '; i--) {
        badge_config.name[i] = '\0'; // the roller has a space; trailing ones are never wanted
    }
    save_badge_config();
    hid_set_name(badge_config.name);
}

static void create_name_section(lv_obj_t *list) {
    lv_obj_t *row = create_setting_row(list, "Name");
    // ponytail: 7 cells of 24 px + gaps fit the 192 px the row has; the widget scrolls if a font ever changes that
    text_input_config_t cfg = {.charset = TI_ALPHA_UPPER TI_ALPHA_LOWER TI_NUMERIC " ", .max_len = BADGE_NAME_LEN, .cell_w = 24};
    name_input              = text_input_create(row, &cfg, badge_config.name, name_changed_cb, NULL);
    lv_obj_t *hint          = lv_label_create(row);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "Shown top left and as \"<name> Wrench\" when pairing. Centre: edit the letter, up/down: pick, "
                            "right: next letter, left: delete, centre: done.");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
}

static void create_hid_section(lv_obj_t *list) {
    lv_obj_t *row = create_setting_row(list, "HID remote");
    lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);
    lv_obj_t *status = lv_label_create(row);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(status, badge_config.hid_autostart ? "Resume last profile at boot: ON" : "Resume last profile at boot: OFF");
    lv_obj_set_style_text_color(status, lv_color_hex(badge_config.hid_autostart ? GREEN_MAIN : GRAY_SHADE_1), LV_PART_MAIN);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN);
    create_action_button(row, badge_config.hid_autostart ? "Turn off" : "Turn on", autostart_toggle_cb, 0, false);
    lv_obj_t *hint = lv_label_create(row);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "On: after a power cycle the badge opens HID Remote and resumes the profile it was in. "
                            "Hold centre 1 s to get the menu back.");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
}

static void create_usb_section(lv_obj_t *list) {
    lv_obj_t *row = create_setting_row(list, "USB");
    lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);

    lv_obj_t *status = lv_label_create(row);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(status, lv_color_hex(GRAY_SHADE_1), LV_PART_MAIN);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, LV_PART_MAIN);
    if (hid_usb_active()) {
        lv_label_set_text(status, hid_usb_mounted() ? "Keyboard over USB: on, host connected" : "Keyboard over USB: on, nothing plugged in");
    } else if (badge_config.usb_hid) {
        lv_label_set_text(status, "Keyboard over USB: on (skipped this boot)");
    } else {
        lv_label_set_text(status, "Keyboard over USB: off (the port is the console)");
    }
    create_action_button(row, badge_config.usb_hid ? "Turn USB keyboard off" : "Turn USB keyboard on", usb_toggle_cb, 0, false);

    lv_obj_t *hint = lv_label_create(row);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, "USB on moves logs and flashing to the CDC port (tools/enter_bootloader.py). "
                            "Hold centre while powering on to skip it for one boot.");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
}

static void create_bluetooth_section(lv_obj_t *list) {
    s_bt_armed      = NULL;
    lv_obj_t *row   = create_setting_row(list, "Bluetooth");
    lv_obj_set_style_pad_row(row, 4, LV_PART_MAIN);

    char addr[18];
    bool connected = hid_host_addr(addr);
    char hosts[8][18];
    int bonds = hid_bonded_hosts(hosts, 8);

    lv_obj_t *status = lv_label_create(row);
    lv_obj_set_width(status, lv_pct(100));
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(status, lv_color_hex(GRAY_SHADE_1), LV_PART_MAIN);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_12, LV_PART_MAIN);
    if (hid_suspended()) {
        lv_label_set_text_fmt(status, "HID radio off (nothing can connect)\n%d bonded host%s", bonds, bonds == 1 ? "" : "s");
    } else if (connected) {
        lv_label_set_text_fmt(status, "Connected: %s\n%d bonded host%s", addr, bonds, bonds == 1 ? "" : "s");
    } else {
        lv_label_set_text_fmt(status, "Advertising, no host connected\n%d bonded host%s", bonds, bonds == 1 ? "" : "s");
    }

    create_action_button(row, hid_suspended() ? "Turn HID radio on" : "Turn HID radio off", bt_toggle_cb, 0, false);
    if (connected) {
        create_action_button(row, "Disconnect host", bt_disconnect_cb, 0, false);
    }
    for (int i = 0; i < bonds; i++) {
        char text[32] = "Forget ";
        strlcat(text, hosts[i], sizeof(text));
        create_action_button(row, text, bt_forget_cb, i, true);
    }
    if (bonds > 1) {
        create_action_button(row, "Forget all bonds", bt_forget_all_cb, 0, true);
    }

    lv_obj_t *hint = lv_label_create(row);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(hint, connected ? "A bonded host reconnects on its own while the radio is on; turn the radio off to keep it away."
                                      : "Forgetting a host also removes the badge from its side next time it looks.");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
}

static void settings_app_destroy(lv_event_t *e) {
    (void)e;
    lv_obj_t *sliders[] = {brightness_slider, timeout_slider, led_slider};
    for (size_t i = 0; i < sizeof(sliders) / sizeof(sliders[0]); i++) {
        if (sliders[i] && lv_obj_is_valid(sliders[i])) {
            free(lv_obj_get_user_data(sliders[i]));
            lv_obj_set_user_data(sliders[i], NULL);
        }
    }
    brightness_slider = timeout_slider = led_slider = NULL;
    if (name_input && lv_obj_is_valid(name_input)) {
        text_input_destroy(name_input); // frees the widget's heap state; the parent's delete would only take the objects
    }
    name_input = NULL;
}

// -------------------------------------------------------------------------------------------------
// Widget helpers
// -------------------------------------------------------------------------------------------------
static lv_obj_t *create_setting_row(lv_obj_t *parent, const char *label_text) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(row, 6, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(label, 4, LV_PART_MAIN);

    return row;
}

static lv_obj_t *create_slider(lv_obj_t *row, int32_t min, int32_t max, int32_t value, void (*commit)(int32_t)) {
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, lv_pct(100), 16);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);

    slider_guard_t *guard = calloc(1, sizeof(slider_guard_t));
    guard->last           = value;
    guard->commit         = commit;
    lv_obj_set_user_data(slider, guard);

    // Thin track, filled indicator, round knob
    lv_obj_set_style_bg_color(slider, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(BLUE_MAIN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(WHITE), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 6, LV_PART_KNOB);

    // Focus styling
    lv_obj_set_style_outline_width(slider, 4, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(slider, lv_color_hex(BLUE_MAIN), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(slider, 3, LV_PART_MAIN | LV_STATE_FOCUS_KEY);

    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(slider, scroll_to_focused_cb, LV_EVENT_FOCUSED, row);
    return slider;
}

static lv_obj_t *create_value_label(lv_obj_t *row) {
    lv_obj_t *label = lv_label_create(row);
    lv_obj_set_style_text_color(label, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label, 4, LV_PART_MAIN);
    lv_obj_set_width(label, lv_pct(100));
    return label;
}

// -------------------------------------------------------------------------------------------------
// Main App Creation
// -------------------------------------------------------------------------------------------------
static void settings_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data);

void settings_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_SETTINGS, settings_nav_setup, NULL);

    // Main container
    lv_obj_t *main_container = lv_obj_create(parent);
    lv_obj_set_size(main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(main_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(main_container, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_left(main_container, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_right(main_container, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(main_container, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(main_container, settings_app_destroy, LV_EVENT_DELETE, NULL);

    // Scrollable settings list
    settings_list = lv_obj_create(main_container);
    lv_obj_set_width(settings_list, lv_pct(100));
    lv_obj_set_flex_grow(settings_list, 1);
    lv_obj_set_style_bg_color(settings_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(settings_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_list, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(settings_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(settings_list, 8, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(settings_list, LV_SCROLLBAR_MODE_AUTO);

    create_name_section(settings_list);

    // Screen brightness
    lv_obj_t *brightness_row = create_setting_row(settings_list, "Brightness");
    int32_t current_percent  = brightness_to_percent(badge_config.screen_brightness);
    brightness_slider = create_slider(brightness_row, BRIGHTNESS_MIN_PERCENT, BRIGHTNESS_MAX_PERCENT, current_percent,
                                      commit_brightness);
    brightness_label  = create_value_label(brightness_row);
    lv_label_set_text_fmt(brightness_label, "%ld%%", (long)current_percent);

    // Screen timeout
    lv_obj_t *timeout_row = create_setting_row(settings_list, "Screen Timeout");
    int32_t current_index = 0;
    for (size_t i = 0; i < timeout_count; i++) {
        if (timeout_values[i] == badge_config.screen_timeout) {
            current_index = i;
            break;
        }
    }
    timeout_slider = create_slider(timeout_row, 0, timeout_count - 1, current_index, commit_timeout);
    timeout_label  = create_value_label(timeout_row);
    char timeout_buf[32];
    format_timeout_string(timeout_buf, sizeof(timeout_buf), timeout_values[current_index]);
    lv_label_set_text(timeout_label, timeout_buf);

    // LED brightness
    lv_obj_t *led_row   = create_setting_row(settings_list, "LED Brightness");
    int32_t led_percent = (badge_config.led_brightness * 100) / 255;
    led_slider          = create_slider(led_row, 0, 100, led_percent, commit_led);
    led_label           = create_value_label(led_row);
    lv_label_set_text_fmt(led_label, "%ld%%", (long)led_percent);

    // USB keyboard + Bluetooth host link / bonds (both register their buttons in bt_buttons[])
    bt_button_count = 0;
    create_hid_section(settings_list);
    create_usb_section(settings_list);
    create_bluetooth_section(settings_list);

    // Version
    lv_obj_t *version_label = lv_label_create(main_container);
    version_t ver           = firmware_version();
    lv_label_set_text_fmt(version_label, "v%d.%d.%d", ver.major, ver.minor, ver.patch);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(version_label, lv_color_hex(GRAY_SHADE_3), LV_PART_MAIN);
    lv_obj_set_style_pad_top(version_label, 8, LV_PART_MAIN);
}

static void settings_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)content_area;
    (void)user_data;

    settings_nav_ctx = nav_ctx;
    text_input_setup_nav(name_input, nav_ctx);
    nav_register(nav_ctx, brightness_slider);
    nav_register(nav_ctx, timeout_slider);
    nav_register(nav_ctx, led_slider);
    for (int i = 0; i < bt_button_count; i++) {
        nav_register(nav_ctx, bt_buttons[i]);
    }
    if (s_focus_bt && bt_button_count > 0) {
        s_focus_bt = false;
        nav_focus(nav_ctx, bt_buttons[0]);
    } else {
        nav_focus(nav_ctx, brightness_slider);
    }
}
