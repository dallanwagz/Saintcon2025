#include "leds.h"

#include "esp_log.h"

#include "badge.h"
#include "led_patterns.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

#include "../content.h"

static const char *TAG __attribute__((unused)) = "screens/main [apps/leds]";

static lv_obj_t *pattern_list  = NULL;
static lv_obj_t *current_label = NULL;

static void pattern_clicked(lv_event_t *e) {
    led_idle_pattern_t p = (led_idle_pattern_t)(uintptr_t)lv_event_get_user_data(e);
    led_idle_set(p);
    badge_config.led_pattern = (uint8_t)p;
    save_badge_config();
    lv_label_set_text_fmt(current_label, "Pattern: %s", led_idle_pattern_names[p]);
}

static void leds_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)content_area;
    (void)user_data;
    nav_register_children(nav_ctx, pattern_list);
    nav_autolink(nav_ctx);
    lv_obj_t *current = lv_obj_get_child(pattern_list, led_idle_get());
    nav_focus(nav_ctx, current ? current : lv_obj_get_child(pattern_list, 0));
}

void leds_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_LEDS, leds_nav_setup, NULL);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(root, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    current_label = lv_label_create(root);
    lv_label_set_text_fmt(current_label, "Pattern: %s", led_idle_pattern_names[led_idle_get()]);
    lv_obj_set_style_text_color(current_label, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(current_label, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(current_label, 6, LV_PART_MAIN);

    pattern_list = lv_list_create(root);
    lv_obj_set_width(pattern_list, lv_pct(100));
    lv_obj_set_flex_grow(pattern_list, 1);
    lv_obj_set_style_bg_color(pattern_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pattern_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(pattern_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pattern_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pattern_list, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(pattern_list, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(pattern_list, &lv_font_montserrat_16, LV_PART_MAIN);
    for (int i = 0; i < LED_IDLE_COUNT; i++) {
        lv_obj_t *btn = lv_list_add_button(pattern_list, NULL, led_idle_pattern_names[i]);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, pattern_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "Brightness lives in Settings");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 6, LV_PART_MAIN);
}
