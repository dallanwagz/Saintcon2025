// Shortcuts: one press runs a Home Assistant macro. The badge only broadcasts; HA decides what a shortcut does.
// Shortcuts come from CONFIG_BADGE_SHORTCUTS ("Lock doors;All lights off", per badge in its defaults file).
// Shortcut n sends the same BTHome dimmer event as HA Menu, rotate_right with steps = SHORTCUT_BASE + n, a range
// HA Menu never uses (its rows are 1-32 on rotate_right), so one HA automation per badge maps each number to a
// script (ha/wrench_shortcuts.yaml). Append new shortcuts at the end so numbers keep their meaning.
// An entry starting with '#' ("# Office") is a heading: shown, not pressable, not numbered.
#include "shortcuts.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "bthome.h"
#include "led_patterns.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

#include "../content.h"

static const char *TAG = "screens/main [apps/shortcuts]";

#define SHORTCUT_BASE 200
#define SHORTCUT_MAX  32

static char s_buf[] = CONFIG_BADGE_SHORTCUTS; // split in place, once
static const char *s_items[SHORTCUT_MAX];
static int s_num[SHORTCUT_MAX]; // shortcut number (1-based) per item, 0 = heading
static int s_count       = -1;
static int s_last        = 0;
static lv_obj_t *s_list   = NULL;
static lv_obj_t *s_status = NULL;

static void parse(void) {
    if (s_count >= 0) {
        return;
    }
    s_count    = 0;
    int n      = 0;
    char *save = NULL;
    for (char *t = strtok_r(s_buf, ";", &save); t && s_count < SHORTCUT_MAX; t = strtok_r(NULL, ";", &save)) {
        while (*t == ' ') {
            t++;
        }
        if (!*t) {
            continue;
        }
        if (*t == '#') {
            for (t++; *t == ' '; t++) {
            }
            s_num[s_count] = 0;
        } else {
            if (strlen(t) > BTHOME_CMD_MAX) {
                t[BTHOME_CMD_MAX] = '\0'; // the screen shows exactly what Home Assistant receives
            }
            s_num[s_count] = ++n;
        }
        s_items[s_count++] = t;
    }
}

bool shortcuts_available(void) {
    return CONFIG_BADGE_SHORTCUTS[0] != '\0';
}

static void clicked(lv_event_t *e) {
    int i  = (int)(uintptr_t)lv_event_get_user_data(e);
    s_last = i;
    bthome_command(s_items[i], SHORTCUT_BASE + s_num[i]);
    ESP_LOGI(TAG, "shortcut %d '%s'", s_num[i], s_items[i]);
    char buf[BTHOME_CMD_MAX + 8];
    snprintf(buf, sizeof(buf), "Sent: %s", s_items[i]);
    lv_label_set_text(s_status, buf);
    lv_obj_set_style_text_color(s_status, lv_color_hex(GREEN_MAIN), LV_PART_MAIN);
    lv_obj_fade_in(s_status, 400, 0);
    flash_leds(0, 160, 255, 80, 1, 150, 0); // a blue blink on the wrench: the press went out
}

static void nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)user_data;
    lv_obj_t *home  = lv_obj_get_child_by_type(lv_obj_get_parent(content_area), 0, &lv_button_class);
    lv_obj_t *first = lv_obj_get_child_by_type(s_list, 0, &lv_list_button_class);
    if (!first) {
        return;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_list); i++) { // shortcuts only: headings are not focusable
        lv_obj_t *c = lv_obj_get_child(s_list, i);
        if (lv_obj_check_type(c, &lv_list_button_class)) {
            nav_register(nav_ctx, c);
        }
    }
    if (home) {
        nav_set_neighbor(nav_ctx, home, NAV_DIR_DOWN, first);
        nav_set_neighbor(nav_ctx, first, NAV_DIR_UP, home);
    }
    nav_autolink(nav_ctx);
    lv_obj_t *row = s_num[s_last] ? lv_obj_get_child(s_list, s_last) : NULL; // item i is child i
    nav_focus(nav_ctx, row ? row : first);
}

void shortcuts_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_SHORTCUTS, nav_setup, NULL);
    parse();

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(root, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(root);
    lv_label_set_text(s_status, "Shortcuts");
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status, lv_pct(100));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_status, 8, LV_PART_MAIN);

    s_list = lv_list_create(root);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_list, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_list, &lv_font_montserrat_16, LV_PART_MAIN);
    for (int i = 0; i < s_count; i++) {
        if (!s_num[i]) {
            lv_obj_t *h = lv_list_add_text(s_list, s_items[i]);
            lv_obj_set_style_bg_color(h, lv_color_hex(GRAY_SHADE_6), LV_PART_MAIN);
            lv_obj_set_style_text_color(h, lv_color_hex(ORANGE_LIGHTER), LV_PART_MAIN);
            lv_obj_set_style_text_font(h, &lv_font_montserrat_14, LV_PART_MAIN);
            continue;
        }
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_PLAY, s_items[i]);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(btn, 14, LV_PART_MAIN); // big targets: one press, no scrolling hunt
        lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(btn, clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "Centre runs the shortcut in Home Assistant.");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 6, LV_PART_MAIN);
}
