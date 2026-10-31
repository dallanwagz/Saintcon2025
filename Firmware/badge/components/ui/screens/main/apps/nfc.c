#include "nfc.h"

#include "esp_log.h"

#include "badge/ops_nfc.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

// The NFC component header is also called nfc.h; include it by path to get past our own header
#include "../../../../nfc/include/nfc.h"
#include "../content.h"

static const char *TAG __attribute__((unused)) = "screens/main [apps/nfc]";

#define BADGE_NFC_TEXT "Hello from the SAINTCON 2025 wrench!"

static lv_obj_t *buttons_row  = NULL;
static lv_obj_t *status_label = NULL;
static const char *s_tag_desc = "URL (default)";

static void set_status(const char *desc) {
    s_tag_desc = desc;
    lv_label_set_text_fmt(status_label, "Tag holds: %s", s_tag_desc);
}

static void write_url_cb(lv_event_t *e) {
    (void)e;
    set_status(nfc_write_url(BADGE_NFC_URL) == NFC_OK ? "URL" : "write failed");
}
static void write_text_cb(lv_event_t *e) {
    (void)e;
    set_status(nfc_write_text(BADGE_NFC_TEXT) == NFC_OK ? "Text" : "write failed");
}
static void clear_cb(lv_event_t *e) {
    (void)e;
    set_status(nfc_clear() == NFC_OK ? "nothing" : "write failed");
}

static lv_obj_t *add_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_style_t *color) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_add_style(btn, &button_style, LV_PART_MAIN);
    lv_obj_add_style(btn, color, LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void nfc_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)content_area;
    (void)user_data;
    nav_register_children(nav_ctx, buttons_row);
    nav_autolink(nav_ctx);
    nav_focus(nav_ctx, lv_obj_get_child(buttons_row, 0));
}

void nfc_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_NFC, nfc_nav_setup, NULL);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(root, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root, 10, LV_PART_MAIN);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(root);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_color(status_label, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    set_status(s_tag_desc);

    buttons_row = lv_obj_create(root);
    lv_obj_set_width(buttons_row, lv_pct(100));
    lv_obj_set_height(buttons_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(buttons_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(buttons_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(buttons_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(buttons_row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(buttons_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(buttons_row, LV_OBJ_FLAG_SCROLLABLE);
    add_button(buttons_row, "Write URL", write_url_cb, &button_blue);
    add_button(buttons_row, "Write Text", write_text_cb, &button_green);
    add_button(buttons_row, "Clear Tag", clear_cb, &button_grey);

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "Tap a phone to the wrench head to read it.\nLEDs sparkle when a phone writes to the tag.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}
