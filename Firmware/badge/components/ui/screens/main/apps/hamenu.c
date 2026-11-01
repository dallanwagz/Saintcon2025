// HA Menu: a list of commands for Home Assistant. Picking a row broadcasts a BTHome command (bthome_command);
// nothing else happens on the badge, Home Assistant decides what the row means. A row with entries
// ("Strip|Off|Rainbow|Fire" in CONFIG_BADGE_MENU) opens a popup of those entries instead.
// Numbering, which is what HA automations key on: row r sends rotate_right steps r; entry k of row r sends
// rotate_left steps r*10+k, so rows <= 24 can have up to 9 entries and a row keeps its number when later rows
// or entries are appended.
#include "hamenu.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "bthome.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

#include "../content.h"

static const char *TAG = "screens/main [apps/hamenu]";

#define HAMENU_MAX_ROWS    32
#define HAMENU_MAX_ENTRIES 9

typedef struct {
    const char *label;
    const char *entries[HAMENU_MAX_ENTRIES];
    int n_entries;
} hamenu_row_t;

static char s_rows_buf[] = CONFIG_BADGE_MENU; // split in place, once
static hamenu_row_t s_rows[HAMENU_MAX_ROWS];
static int s_row_count    = -1;
static int s_open_row     = -1; // row whose entry popup is showing, else -1
static int s_last_row     = 0;  // refocused when the app is rebuilt
static int s_last_entry   = 0;  // 0 = the Back row
static lv_obj_t *s_list   = NULL;
static lv_obj_t *s_status = NULL;

static char *clip(char *t) {
    while (*t == ' ') {
        t++;
    }
    if (strlen(t) > BTHOME_CMD_MAX) {
        t[BTHOME_CMD_MAX] = '\0'; // the screen shows exactly what Home Assistant receives
        ESP_LOGW(TAG, "menu text cut to '%s' (%d characters max)", t, BTHOME_CMD_MAX);
    }
    return t;
}

static void parse_rows(void) {
    if (s_row_count >= 0) {
        return;
    }
    s_row_count = 0;
    char *save_row = NULL;
    for (char *row = strtok_r(s_rows_buf, ";", &save_row); row && s_row_count < HAMENU_MAX_ROWS;
         row = strtok_r(NULL, ";", &save_row)) {
        hamenu_row_t *r = &s_rows[s_row_count];
        char *save_part = NULL;
        char *part      = strtok_r(row, "|", &save_part);
        if (!part || !*clip(part)) {
            continue;
        }
        r->label     = part;
        r->n_entries = 0;
        for (part = strtok_r(NULL, "|", &save_part); part && r->n_entries < HAMENU_MAX_ENTRIES;
             part = strtok_r(NULL, "|", &save_part)) {
            if (*clip(part)) {
                r->entries[r->n_entries++] = part;
            }
        }
        if (r->n_entries && s_row_count >= 24) {
            ESP_LOGW(TAG, "row %d '%s': entries only work on rows 1-24, sending it as a plain row", s_row_count + 1, r->label);
            r->n_entries = 0;
        }
        ESP_LOGI(TAG, "row %d '%s' entries %d", s_row_count + 1, r->label, r->n_entries);
        s_row_count++;
    }
}

static void sent(const char *text) {
    lv_label_set_text_fmt(s_status, "Sent: %s", text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(GREEN_MAIN), LV_PART_MAIN);
    lv_obj_fade_in(s_status, 400, 0); // visible blink even when the same row is sent twice
}

static void row_clicked(lv_event_t *e) {
    int i      = (int)(uintptr_t)lv_event_get_user_data(e);
    s_last_row = i;
    if (s_rows[i].n_entries) {
        s_open_row   = i;
        s_last_entry = 0;
        ui_request_rerender(); // rebuild as the popup
        return;
    }
    bthome_command(s_rows[i].label, i + 1);
    sent(s_rows[i].label);
}

static void entry_clicked(lv_event_t *e) {
    int k        = (int)(uintptr_t)lv_event_get_user_data(e); // 0 = Back
    s_last_entry = k;
    if (k == 0) {
        s_open_row = -1;
        ui_request_rerender();
        return;
    }
    const hamenu_row_t *r = &s_rows[s_open_row];
    bthome_command(r->entries[k - 1], -((s_open_row + 1) * 10 + k)); // negative = rotate_left
    sent(r->entries[k - 1]);
}

static void hamenu_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)user_data;
    lv_obj_t *home  = lv_obj_get_child_by_type(lv_obj_get_parent(content_area), 0, &lv_button_class);
    lv_obj_t *first = lv_obj_get_child(s_list, 0);
    if (!first) {
        return; // empty CONFIG_BADGE_MENU: only the home button is left
    }
    nav_register_children(nav_ctx, s_list);
    if (home) {
        nav_set_neighbor(nav_ctx, home, NAV_DIR_DOWN, first);
        nav_set_neighbor(nav_ctx, first, NAV_DIR_UP, home);
    }
    nav_autolink(nav_ctx);
    lv_obj_t *row = lv_obj_get_child(s_list, s_open_row >= 0 ? s_last_entry : s_last_row);
    nav_focus(nav_ctx, row ? row : first);
}

static lv_obj_t *add_row(const char *text, lv_event_cb_t cb, int idx) {
    lv_obj_t *btn = lv_list_add_button(s_list, NULL, text);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);
    return btn;
}

void hamenu_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_HAMENU, hamenu_nav_setup, NULL);
    parse_rows();
    char label[BTHOME_CMD_MAX + 8];

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
    if (s_open_row >= 0) {
        snprintf(label, sizeof(label), "%d  %s", s_open_row + 1, s_rows[s_open_row].label);
        lv_label_set_text(s_status, label);
        lv_obj_set_style_text_color(s_status, lv_color_hex(BLUE_LIGHTEST), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_status, "Pick a command");
        lv_obj_set_style_text_color(s_status, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    }
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_status, lv_pct(100));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_status, 6, LV_PART_MAIN);

    s_list = lv_list_create(root);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, s_open_row >= 0 ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_list, lv_color_hex(BLUE_MAIN), LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_list, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_list, &lv_font_montserrat_16, LV_PART_MAIN);
    if (s_open_row >= 0) {
        const hamenu_row_t *r = &s_rows[s_open_row];
        add_row(LV_SYMBOL_LEFT "  Back", entry_clicked, 0);
        for (int k = 1; k <= r->n_entries; k++) {
            snprintf(label, sizeof(label), "%d  %s", k, r->entries[k - 1]);
            add_row(label, entry_clicked, k);
        }
    } else {
        for (int i = 0; i < s_row_count; i++) {
            snprintf(label, sizeof(label), "%d  %s%s", i + 1, s_rows[i].label, s_rows[i].n_entries ? "  " LV_SYMBOL_RIGHT : "");
            add_row(label, row_clicked, i);
        }
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, s_open_row >= 0 ? "Center sends the entry (row.entry)\nto Home Assistant."
                                            : "Center sends the row to Home Assistant.\nAutomations match the row number.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 6, LV_PART_MAIN);
}
