#include "hid.h"

#include "esp_log.h"

#include "badge.h"
#include "input.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

// The hid component header is also called hid.h; include it by path to get past our own header
#include "../../../../hid/include/hid.h"
#include "hid_profiles.h"
#include "../content.h"

static const char *TAG = "screens/main [apps/hid]";

static lv_obj_t *app_root     = NULL;
static lv_obj_t *profile_list = NULL;
static lv_obj_t *active_panel = NULL;
static lv_obj_t *focus_sink   = NULL;  // invisible focus target between the home button and the list
static bool s_focus_sink_next = false; // focus the sink on the next build (set when a profile exits)
static bool s_autostart_pending = true; // boot-time resume (badge_config.hid_autostart) happens once, on the first build

static const char *state_text(void) {
    switch (hid_get_state()) {
        case HID_STATE_USB_CONNECTED: return "USB: connected";
        case HID_STATE_CONNECTED: return "Bluetooth: connected";
        case HID_STATE_ADVERTISING: return "Bluetooth: waiting for a host";
        default: return "Bluetooth: off";
    }
}

// Full-size panel shown while the joystick is being sent to the host
static void show_active_panel(void) {
    if (active_panel) {
        return;
    }
    hid_profile_t p = hid_profile_get();

    active_panel = lv_obj_create(app_root);
    lv_obj_set_size(active_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_align(active_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(active_panel, lv_color_hex(GRAY_SHADE_8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(active_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(active_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(active_panel, lv_color_hex(BLUE_MAIN), LV_PART_MAIN);
    lv_obj_set_style_radius(active_panel, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(active_panel, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(active_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(active_panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(active_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(active_panel);
    lv_label_set_text(name, hid_profile_names[p]);
    lv_obj_set_style_text_color(name, lv_color_hex(BLUE_LIGHTEST), LV_PART_MAIN);
    lv_obj_set_style_text_font(name, &white_rabbit_22, LV_PART_MAIN);

    lv_obj_t *legend = lv_label_create(active_panel);
    lv_label_set_text(legend, hid_profile_hints[p]);
    lv_label_set_long_mode(legend, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(legend, lv_pct(100));
    lv_obj_set_style_text_color(legend, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(legend, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(legend, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(legend, 4, LV_PART_MAIN);

    lv_obj_t *status = lv_label_create(active_panel);
    lv_label_set_text(status, state_text());
    lv_obj_set_style_text_color(status, lv_color_hex(hid_get_state() >= HID_STATE_CONNECTED ? GREEN_MAIN : YELLOW_MAIN),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN);

    lv_obj_t *footer = lv_label_create(active_panel);
    lv_label_set_text(footer, "Hold center for 1 s to go back");
    lv_obj_set_style_text_color(footer, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, LV_PART_MAIN);
}

// Joystick task -> UI: give the stick back to LVGL and rebuild this app in list mode
static void on_hid_exit(void) {
    s_focus_sink_next = true; // come back with nothing selected: UP = home, DOWN = first profile
    input_set_passthrough(false);
    ui_request_rerender();
}

static void start_profile(hid_profile_t p) {
    hid_profile_set(p);
    badge_config.hid_profile = (uint8_t)p;
    save_badge_config();

    ESP_LOGI(TAG, "Profile '%s' active: joystick now goes to the host", hid_profile_names[p]);
    input_set_passthrough(true);
    hid_profile_set_active(true);
    show_active_panel();
}

static void profile_clicked(lv_event_t *e) {
    start_profile((hid_profile_t)(uintptr_t)lv_event_get_user_data(e));
}

static void hid_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)user_data;
    lv_obj_t *home  = lv_obj_get_child_by_type(lv_obj_get_parent(content_area), 0, &lv_button_class);
    lv_obj_t *first = lv_obj_get_child(profile_list, 0);

    nav_register_children(nav_ctx, profile_list);
    nav_register(nav_ctx, focus_sink);

    // Wire the sink in explicitly so it is only ever reached on purpose, never by geometry
    nav_set_neighbor(nav_ctx, focus_sink, NAV_DIR_UP, home);
    nav_set_neighbor(nav_ctx, focus_sink, NAV_DIR_DOWN, first);
    if (home) {
        nav_set_neighbor(nav_ctx, home, NAV_DIR_DOWN, first);
        nav_set_neighbor(nav_ctx, first, NAV_DIR_UP, home);
    }
    nav_autolink(nav_ctx);

    if (s_focus_sink_next) {
        s_focus_sink_next = false;
        nav_focus(nav_ctx, focus_sink);
    } else {
        lv_obj_t *current = lv_obj_get_child(profile_list, hid_profile_get());
        nav_focus(nav_ctx, current ? current : first);
    }
}

void hid_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_HID, hid_nav_setup, NULL);
    hid_profile_set_exit_callback(on_hid_exit);
    active_panel = NULL;

    app_root = lv_obj_create(parent);
    lv_obj_set_size(app_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(app_root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(app_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(app_root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(app_root, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(app_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(app_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(app_root, LV_OBJ_FLAG_SCROLLABLE);

    // Invisible, non-clickable focus target that sits (geometrically) between the home button and the list
    focus_sink = lv_obj_create(app_root);
    lv_obj_remove_style_all(focus_sink);
    lv_obj_add_flag(focus_sink, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(focus_sink, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(focus_sink, lv_pct(100), 8);
    lv_obj_set_pos(focus_sink, 0, 15); // screen y ~70-78: below the home button (ends 44), above the list (starts 85)

    lv_obj_t *status = lv_label_create(app_root);
    lv_label_set_text(status, state_text());
    lv_obj_set_style_text_color(status, lv_color_hex(hid_get_state() >= HID_STATE_CONNECTED ? GREEN_MAIN : YELLOW_MAIN),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(status, 6, LV_PART_MAIN);

    profile_list = lv_list_create(app_root);
    lv_obj_set_width(profile_list, lv_pct(100));
    lv_obj_set_flex_grow(profile_list, 1);
    lv_obj_set_style_bg_color(profile_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(profile_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(profile_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(profile_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(profile_list, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(profile_list, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(profile_list, &lv_font_montserrat_16, LV_PART_MAIN);
    for (int i = 0; i < HID_PROFILE_COUNT; i++) {
        lv_obj_t *btn = lv_list_add_button(profile_list, NULL, hid_profile_names[i]);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS); // six rows no longer fit on screen
        lv_obj_add_event_cb(btn, profile_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_obj_t *hint = lv_label_create(app_root);
    lv_label_set_text(hint, "Pair \"SC25 Wrench\" over Bluetooth, or turn on\nUSB in Settings and plug in. Then pick a profile.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 6, LV_PART_MAIN);

    // Rebuilt (e.g. on a connection change) while a profile is running: keep the panel up
    if (hid_profile_is_active()) {
        show_active_panel();
    } else if (badge_config.hid_autostart == BOOT_APP_HID && s_autostart_pending) {
        start_profile((hid_profile_t)badge_config.hid_profile); // resume where the badge was before the power cycle
    }
    s_autostart_pending = false;
}
