// LEDs app: a WLED look picker for the wrench wallpaper (wled_fx via wled_mirror).
// Rows: Mirror strip (HA Menu picks also show here), Brightness, LEDs on/off, Effect, Palette, Speed, Intensity, the extra sliders/options/colours the effect's WLED
// metadata exposes, Virtual length. Up/down moves, left/right changes the value (hold to repeat), centre opens
// a list for Effect/Palette and toggles/cycles the other rows. Every change previews live; badge_config is
// saved 1 s after the last change.
#include "leds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"

#include "badge.h"
#include "led.h"
#include "wled_mirror.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

#include "../content.h"

static const char *TAG __attribute__((unused)) = "screens/main [apps/leds]";

typedef enum {
    R_MIRROR, R_BRI, R_POWER, R_FX, R_PAL, R_SX, R_IX, R_C1, R_C2, R_C3, R_O1, R_O2, R_O3, R_COL0, R_COL1, R_COL2, R_VLEN, R_COUNT
} row_t;

// ponytail: a small colour cycle instead of an RGB editor
static const struct {
    const char *name;
    uint8_t rgb[3];
} COLOURS[] = {
    {"White", {255, 255, 255}}, {"Warm", {255, 160, 60}}, {"Red", {255, 0, 0}},     {"Orange", {255, 96, 0}},
    {"Yellow", {255, 200, 0}},  {"Green", {0, 255, 0}},   {"Cyan", {0, 255, 200}},  {"Blue", {0, 0, 255}},
    {"Purple", {160, 0, 255}},  {"Pink", {255, 0, 128}},  {"Black", {0, 0, 0}},
};
#define COLOUR_COUNT (int)(sizeof(COLOURS) / sizeof(COLOURS[0]))
static const uint16_t VLENS[] = {0, 60, 120, 150, 300}; // 0 = the 20 badge LEDs

static wled_params_t s_p;   // the look being edited (also badge_config.wled after every preview)
static bool s_on;           // badge_config.wled_on
static row_t s_focus = R_MIRROR;
static int s_pick = -1;     // -1, or R_FX / R_PAL while the list picker is open
static wled_params_t s_before; // look when the picker opened (left = cancel)
static bool s_before_on, s_previewed; // on/off when the picker opened; true once the picker previewed something

static char s_lbl[R_COUNT][24]; // row labels from the effect metadata; "" = row hidden
static lv_obj_t *s_val[R_COUNT];
static lv_obj_t *s_list, *s_roller;
static lv_timer_t *s_save_timer;

static uint8_t s_order[256]; // effect ids sorted by name
static uint16_t s_order_n;

static int cmp_fx(const void *a, const void *b) {
    return strcasecmp(wled_fx_name(*(const uint8_t *)a), wled_fx_name(*(const uint8_t *)b));
}

static void build_order(void) {
    if (s_order_n) {
        return;
    }
    s_order_n = wled_fx_count();
    for (uint16_t i = 0; i < s_order_n; i++) {
        s_order[i] = wled_fx_id_at(i);
    }
    qsort(s_order, s_order_n, 1, cmp_fx);
}

static int fx_pos(uint8_t fx) {
    for (int i = 0; i < s_order_n; i++) {
        if (s_order[i] == fx) {
            return i;
        }
    }
    return 0;
}

// WLED metadata "Name@sx,ix,c1,c2,c3,o1,o2,o3;col1,col2,col3;pal;..." -> row labels (same rules as WLED's
// index.js setEffectParameters: '!' = default label, empty = hidden, no '@' = speed/intensity + all colours)
static void parse_meta(void) {
    static const char *DEF[R_COUNT] = {
        [R_PAL] = "Palette",   [R_SX] = "Speed",     [R_IX] = "Intensity", [R_C1] = "Custom 1",
        [R_C2] = "Custom 2",   [R_C3] = "Custom 3",  [R_O1] = "Option 1",  [R_O2] = "Option 2",
        [R_O3] = "Option 3",   [R_COL0] = "FX colour", [R_COL1] = "BG colour", [R_COL2] = "Colour 3",
    };
    static const row_t SEC_ROWS[3][8] = {
        {R_SX, R_IX, R_C1, R_C2, R_C3, R_O1, R_O2, R_O3}, {R_COL0, R_COL1, R_COL2}, {R_PAL}};
    static const int SEC_N[3] = {8, 3, 1};

    memset(s_lbl, 0, sizeof(s_lbl));
    snprintf(s_lbl[R_MIRROR], sizeof(s_lbl[0]), "Mirror strip");
    snprintf(s_lbl[R_BRI], sizeof(s_lbl[0]), "Brightness");
    snprintf(s_lbl[R_POWER], sizeof(s_lbl[0]), "LEDs");
    snprintf(s_lbl[R_FX], sizeof(s_lbl[0]), "Effect");
    snprintf(s_lbl[R_VLEN], sizeof(s_lbl[0]), "Virtual length");

    const char *meta = wled_fx_meta(s_p.fx);
    const char *at   = meta ? strchr(meta, '@') : NULL;
    if (!at || !at[1]) {
        const row_t def[] = {R_SX, R_IX, R_COL0, R_COL1, R_COL2, R_PAL};
        for (size_t i = 0; i < sizeof(def) / sizeof(def[0]); i++) {
            snprintf(s_lbl[def[i]], sizeof(s_lbl[0]), "%s", DEF[def[i]]);
        }
        return;
    }
    const char *p = at + 1;
    for (int sec = 0; sec < 3 && *p; sec++) {
        for (int i = 0; i < SEC_N[sec]; i++) {
            size_t len = strcspn(p, ",;");
            size_t eq  = strcspn(p, "=,;"); // "Label=default" -> "Label"
            len        = eq < len ? eq : len;
            row_t r    = SEC_ROWS[sec][i];
            if (len == 1 && *p == '!') {
                snprintf(s_lbl[r], sizeof(s_lbl[0]), "%s", DEF[r]);
            } else if (len && !(r == R_PAL && *p >= '0' && *p <= '9')) {
                snprintf(s_lbl[r], sizeof(s_lbl[0]), "%.*s", (int)len, p);
            }
            p += strcspn(p, ",;");
            if (*p != ',') {
                break;
            }
            p++;
        }
        p += strcspn(p, ";"); // skip extra fields in this section
        if (*p == ';') {
            p++;
        }
    }
}

// -------------------------------------------------------------------------------------------------
// Values
// -------------------------------------------------------------------------------------------------
static void save_cb(lv_timer_t *t) {
    (void)t;
    s_save_timer = NULL; // one-shot: LVGL deletes it after this
    save_badge_config();
}

static void schedule_save(void) {
    // ponytail: the timer outlives the app screen, so leaving the app still saves within 1 s
    if (s_save_timer) {
        lv_timer_reset(s_save_timer);
    } else {
        s_save_timer = lv_timer_create(save_cb, 1000, NULL);
        lv_timer_set_repeat_count(s_save_timer, 1);
    }
}

static void refresh(void);

// Badge-wide rows: they change settings, not the look, so they never preview (which would drop a mirrored look)
static bool global_row(row_t r, int dir) {
    if (r == R_MIRROR) {
        badge_config.led_mirror = !badge_config.led_mirror;
        if (!badge_config.led_mirror) {
            wled_mirror_clear(); // back to the look picked here
        }
    } else if (r == R_BRI) {
        int pct = (badge_config.led_brightness * 100 + 127) / 255 + dir * 5;
        pct     = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        badge_config.led_brightness = (uint8_t)((pct * 255) / 100);
        led_set_brightness(badge_config.led_brightness);
    } else {
        return false;
    }
    schedule_save();
    refresh();
    return true;
}

static void changed(void) {
    wled_mirror_preview(s_on ? &s_p : NULL);
    schedule_save();
}

static uint8_t *u8_of(row_t r) {
    switch (r) {
        case R_SX: return &s_p.sx;
        case R_IX: return &s_p.ix;
        case R_C1: return &s_p.c1;
        case R_C2: return &s_p.c2;
        case R_C3: return &s_p.c3;
        default: return NULL;
    }
}

static bool *opt_of(row_t r) {
    return r == R_O1 ? &s_p.o1 : r == R_O2 ? &s_p.o2 : r == R_O3 ? &s_p.o3 : NULL;
}

static int colour_idx(const uint8_t *rgb) {
    for (int i = 0; i < COLOUR_COUNT; i++) {
        if (!memcmp(COLOURS[i].rgb, rgb, 3)) {
            return i;
        }
    }
    return -1;
}

static void refresh(void) {
    for (int r = 0; r < R_COUNT; r++) {
        lv_obj_t *v = s_val[r];
        if (!v) {
            continue;
        }
        const char *n;
        switch ((row_t)r) {
            case R_MIRROR: lv_label_set_text(v, badge_config.led_mirror ? "On" : "Off"); break;
            case R_BRI: lv_label_set_text_fmt(v, "%d%%", (badge_config.led_brightness * 100 + 127) / 255); break;
            case R_POWER: lv_label_set_text(v, s_on ? "On" : "Off"); break;
            case R_FX: lv_label_set_text(v, (n = wled_fx_name(s_p.fx)) ? n : "?"); break;
            case R_PAL: lv_label_set_text(v, (n = wled_pal_name(s_p.pal)) ? n : "?"); break;
            case R_O1:
            case R_O2:
            case R_O3: lv_label_set_text(v, *opt_of(r) ? "On" : "Off"); break;
            case R_COL0:
            case R_COL1:
            case R_COL2: {
                const uint8_t *c = s_p.col[r - R_COL0];
                int i            = colour_idx(c);
                lv_label_set_text(v, i < 0 ? "Custom" : COLOURS[i].name);
                lv_color_t lc = (c[0] | c[1] | c[2]) ? lv_color_make(c[0], c[1], c[2]) : lv_color_hex(GRAY_SHADE_2);
                lv_obj_set_style_text_color(v, lc, LV_PART_MAIN);
                break;
            }
            case R_VLEN: lv_label_set_text_fmt(v, "%u", s_p.vlen ? s_p.vlen : 20); break;
            default: lv_label_set_text_fmt(v, "%u", *u8_of(r)); break;
        }
    }
}

static void open_picker(row_t r) {
    s_before    = s_p;
    s_before_on = s_on;
    s_previewed = false;
    s_pick      = r;
    ui_request_rerender();
}

static void set_fx(uint8_t fx) {
    s_p.fx = fx;
    wled_fx_defaults(fx, &s_p); // keeps col and vlen
}

// Step a row's value; returns true if the row set changed (new effect -> rebuild)
static bool step(row_t r, int dir) {
    s_on = true; // ponytail: touching any control lights the LEDs
    switch (r) {
        case R_POWER: s_on = !badge_config.wled_on; break;
        case R_FX: set_fx(s_order[(fx_pos(s_p.fx) + dir + s_order_n) % s_order_n]); return true;
        case R_PAL: {
            int n  = wled_pal_count();
            s_p.pal = (uint8_t)((s_p.pal + dir + n) % n);
            break;
        }
        case R_O1:
        case R_O2:
        case R_O3: *opt_of(r) = !*opt_of(r); break;
        case R_COL0:
        case R_COL1:
        case R_COL2: {
            uint8_t *c = s_p.col[r - R_COL0];
            int i      = colour_idx(c);
            i          = i < 0 ? 0 : (i + dir + COLOUR_COUNT) % COLOUR_COUNT;
            memcpy(c, COLOURS[i].rgb, 3);
            break;
        }
        case R_VLEN: {
            int n = sizeof(VLENS) / sizeof(VLENS[0]), i = 0;
            while (i < n - 1 && VLENS[i] < s_p.vlen) {
                i++;
            }
            s_p.vlen = VLENS[(i + dir + n) % n];
            break;
        }
        default: {
            uint8_t *v  = u8_of(r);
            int max     = r == R_C3 ? 31 : 255; // WLED custom3 is 5 bits
            int stp     = r == R_C3 ? 1 : 8;
            int x       = *v + dir * stp;
            *v          = (uint8_t)(x < 0 ? 0 : x > max ? max : x);
            break;
        }
    }
    return false;
}

// -------------------------------------------------------------------------------------------------
// Events
// -------------------------------------------------------------------------------------------------
static void row_key(lv_event_t *e) { // preprocess: runs before nav, which would move focus on left/right
    lv_key_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT) {
        return;
    }
    lv_event_stop_processing(e);
    row_t r = (row_t)(intptr_t)lv_event_get_user_data(e);
    if (global_row(r, key == LV_KEY_RIGHT ? 1 : -1)) {
        return;
    }
    bool rebuild = step(r, key == LV_KEY_RIGHT ? 1 : -1);
    changed();
    if (rebuild) {
        ui_request_rerender();
    } else {
        refresh();
    }
}

static void row_clicked(lv_event_t *e) {
    row_t r = (row_t)(intptr_t)lv_event_get_user_data(e);
    if (r == R_FX || r == R_PAL) {
        open_picker(r);
        return;
    }
    if (r == R_MIRROR) {
        global_row(r, 1);
        return;
    }
    if ((r >= R_SX && r <= R_C3) || r == R_BRI) {
        return; // sliders: left/right only
    }
    step(r, 1);
    changed();
    refresh();
}

static void row_focused(lv_event_t *e) {
    s_focus = (row_t)(intptr_t)lv_event_get_user_data(e);
}

static void picker_select(void) {
    uint16_t sel = lv_roller_get_selected(s_roller);
    s_on         = true;
    s_previewed  = true;
    if (s_pick == R_FX) {
        set_fx(s_order[sel < s_order_n ? sel : 0]);
    } else {
        s_p.pal = (uint8_t)sel;
    }
    changed();
}

static void picker_key(lv_event_t *e) { // preprocess: we drive the roller ourselves, nav and the class stay out
    lv_key_t key = lv_event_get_key(e);
    lv_event_stop_processing(e);
    if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
        int sel = (int)lv_roller_get_selected(s_roller) + (key == LV_KEY_DOWN ? 1 : -1);
        int n   = (int)lv_roller_get_option_count(s_roller);
        lv_roller_set_selected(s_roller, (uint32_t)((sel + n) % n), LV_ANIM_ON);
        picker_select(); // live preview while scrolling
    } else if (key == LV_KEY_LEFT) {
        s_p  = s_before; // cancel
        s_on = s_before_on;
        if (s_previewed) {
            changed(); // ponytail: an untouched picker leaves the HA Menu mirror override alone; a previewed one has cleared it
        }
        s_focus = (row_t)s_pick;
        s_pick  = -1;
        ui_request_rerender();
    }
    // ENTER: handled on CLICKED (release), so the release cannot land on the rebuilt list
}

static void picker_clicked(lv_event_t *e) {
    (void)e;
    s_focus = (row_t)s_pick;
    s_pick  = -1;
    ui_request_rerender();
}

// -------------------------------------------------------------------------------------------------
// Screen
// -------------------------------------------------------------------------------------------------
static void leds_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)content_area;
    (void)user_data;
    if (s_roller) {
        nav_register(nav_ctx, s_roller);
        nav_focus(nav_ctx, s_roller);
        return;
    }
    nav_register_children(nav_ctx, s_list);
    nav_autolink(nav_ctx);
    lv_obj_t *f = s_val[s_focus] ? lv_obj_get_parent(s_val[s_focus]) : lv_obj_get_child(s_list, 0);
    nav_focus(nav_ctx, f);
}

static void add_row(row_t r) {
    lv_obj_t *btn = lv_list_add_button(s_list, NULL, s_lbl[r]);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_pad_ver(btn, 6, LV_PART_MAIN);
    lv_obj_t *v = lv_label_create(btn);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(v, 120, LV_PART_MAIN);
    lv_obj_set_style_text_color(v, lv_color_hex(ORANGE_LIGHTER), LV_PART_MAIN);
    s_val[r] = v;
    lv_obj_add_event_cb(btn, row_key, LV_EVENT_KEY | LV_EVENT_PREPROCESS, (void *)(intptr_t)r);
    lv_obj_add_event_cb(btn, row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)r);
    lv_obj_add_event_cb(btn, row_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)r);
}

static void build_picker(lv_obj_t *root) {
    size_t cap = (s_pick == R_FX ? s_order_n : wled_pal_count()) * 26u + 1;
    char *opts = malloc(cap);
    if (!opts) {
        s_pick = -1;
        return;
    }
    size_t len = 0;
    uint16_t n = s_pick == R_FX ? s_order_n : wled_pal_count(), sel = 0;
    for (uint16_t i = 0; i < n; i++) {
        const char *name = s_pick == R_FX ? wled_fx_name(s_order[i]) : wled_pal_name((uint8_t)i);
        len += snprintf(opts + len, cap - len, "%s%.24s", i ? "\n" : "", name ? name : "?");
        if (s_pick == R_FX ? s_order[i] == s_p.fx : i == s_p.pal) {
            sel = i;
        }
    }
    s_roller = lv_roller_create(root);
    lv_roller_set_options(s_roller, opts, LV_ROLLER_MODE_NORMAL);
    free(opts);
    lv_roller_set_selected(s_roller, sel, LV_ANIM_OFF);
    lv_roller_set_visible_row_count(s_roller, 7);
    lv_obj_set_width(s_roller, lv_pct(100));
    lv_obj_set_flex_grow(s_roller, 1);
    lv_obj_set_style_bg_color(s_roller, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_roller, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_roller, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roller, lv_color_hex(GRAY_TINT_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_roller, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_roller, lv_color_hex(BLUE_MAIN), LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_roller, lv_color_hex(WHITE), LV_PART_SELECTED);
    lv_obj_add_event_cb(s_roller, picker_key, LV_EVENT_KEY | LV_EVENT_PREPROCESS, NULL);
    lv_obj_add_event_cb(s_roller, picker_clicked, LV_EVENT_CLICKED, NULL);
}

static void build_rows(lv_obj_t *root) {
    s_list = lv_list_create(root);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_list, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_list, &lv_font_montserrat_14, LV_PART_MAIN);
    parse_meta();
    for (int r = 0; r < R_COUNT; r++) {
        if (s_lbl[r][0]) {
            add_row((row_t)r);
        }
    }
    refresh();
}

static void root_deleted(lv_event_t *e) {
    (void)e;
    memset(s_val, 0, sizeof(s_val));
    s_list   = NULL;
    s_roller = NULL;
}

void leds_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_LEDS, leds_nav_setup, NULL);
    build_order();
    if (s_pick < 0) {
        s_on = wled_mirror_get_manual(&s_p); // the saved look (badge_config.wled), also after a preview
    }

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(root, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, root_deleted, LV_EVENT_DELETE, NULL);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, s_pick == R_FX ? "Pick an effect" : s_pick == R_PAL ? "Pick a palette" : "Wrench LEDs");
    lv_obj_set_style_text_color(title, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(title, 6, LV_PART_MAIN);

    if (s_pick >= 0) {
        build_picker(root);
    }
    if (s_pick < 0) {
        build_rows(root);
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, s_roller ? "Up/down browse, centre pick, left cancel"
                                     : "Left/right change, centre list/toggle");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 6, LV_PART_MAIN);
}
