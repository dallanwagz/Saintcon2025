// LEDs app: everything about the wrench LEDs, built for the joystick. Five rows, left/right changes the focused
// row, centre does the obvious thing:
//   Effect      < Aurora >   browse the showcase (effects that visibly animate on 20 LEDs, see
//                            tools/wled_port showcase); each step loads a setup that is known to show something.
//                            Centre opens a list to jump around (live preview, centre picks, left cancels).
//   Colours     Party        palette, or the main colour for effects that do not use a palette
//   Speed       4/8
//   Brightness  25%          badge-wide LED brightness
//   Mirror strip On/Off      HA Menu picks also show on the wrench
// Changes preview live; badge_config is saved 1 s after the last change.
#include "leds.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "badge.h"
#include "led.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"
#include "wled_mirror.h"
#include "wled_showcase_gen.h"

#include "../content.h"

typedef enum { R_FX, R_COLOUR, R_SPEED, R_BRI, R_MIRROR, R_COUNT } row_t;
static const char *const ROW_NAMES[R_COUNT] = {"Effect", "Colours", "Speed", "Brightness", "Mirror strip"};

// ponytail: a short colour cycle instead of an RGB editor (first entry = WLED's default orange)
static const struct {
    const char *name;
    uint8_t rgb[3];
} COLOURS[] = {
    {"Orange", {255, 160, 0}}, {"Warm white", {255, 177, 110}}, {"White", {255, 255, 255}}, {"Red", {255, 0, 0}},
    {"Yellow", {255, 200, 0}}, {"Green", {0, 255, 0}},         {"Cyan", {0, 255, 200}},    {"Blue", {0, 0, 255}},
    {"Purple", {160, 0, 255}}, {"Pink", {255, 0, 128}},
};
#define COLOUR_COUNT (int)(sizeof(COLOURS) / sizeof(COLOURS[0]))
#define SPEED_STEP   32

static wled_params_t s_p; // the look being shown (badge_config.wled after every preview)
static bool s_on;
static row_t s_focus = R_FX;
static bool s_picking;         // the effect list is open
static wled_params_t s_before; // look when the list opened (left = cancel)
static bool s_before_on;
static lv_obj_t *s_val[R_COUNT];
static lv_obj_t *s_roller;
static lv_timer_t *s_save_timer;

// -------------------------------------------------------------------------------------------------
// Looks
// -------------------------------------------------------------------------------------------------
static int showcase_pos(void) { // -1 = Off, else index in WLED_SHOWCASE (0 if the saved fx is not in it)
    if (!s_on) {
        return -1;
    }
    for (int i = 0; i < WLED_SHOWCASE_COUNT; i++) {
        if (WLED_SHOWCASE[i].fx == s_p.fx) {
            return i;
        }
    }
    return 0;
}

// The exact setup the showcase scorer rendered: metadata defaults + its palette/colour fix, native 20 LEDs
static void load_showcase(int i) {
    const wled_showcase_t *e = &WLED_SHOWCASE[i];
    memset(&s_p, 0, sizeof(s_p));
    wled_fx_defaults(e->fx, &s_p);
    if (e->pal != 255) {
        s_p.pal = e->pal;
    }
    if (e->has_col) {
        memcpy(s_p.col, e->col, sizeof(s_p.col));
    } else {
        memcpy(s_p.col[0], COLOURS[0].rgb, 3); // WLED's default orange, black, black
    }
    s_on = true;
}

static bool uses_palette(void) { // WLED metadata: no '@' = all controls; else the 3rd ';' section shows the palette
    const char *m = wled_fx_meta(s_p.fx);
    if (!m || !strchr(m, '@')) {
        return true;
    }
    for (int sec = 0; sec < 2; sec++) {
        m = strchr(m, ';');
        if (!m) {
            return false;
        }
        m++;
    }
    return *m && *m != ';' && !(*m >= '0' && *m <= '9');
}

static int colour_idx(void) {
    for (int i = 0; i < COLOUR_COUNT; i++) {
        if (!memcmp(COLOURS[i].rgb, s_p.col[0], 3)) {
            return i;
        }
    }
    return -1;
}

static int bri_pct(void) {
    return (badge_config.led_brightness * 100 + 127) / 255;
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

// lv_label_set_text_fmt() frees the old text before lv_label_refr_text() reverts the "..." of a LONG_MODE_DOTS
// label, so the saved characters land in the new string (garbage like "?`?"). Format here, set via set_text.
static void set_fmt(lv_obj_t *label, const char *fmt, ...) {
    char buf[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(label, buf);
}

static void refresh(void) {
    if (!s_val[R_FX]) {
        return;
    }
    const char *n = wled_fx_name(s_p.fx);
    set_fmt(s_val[R_FX], LV_SYMBOL_LEFT " %s " LV_SYMBOL_RIGHT, !s_on ? "Off" : n ? n : "?");
    if (!s_on) {
        lv_label_set_text(s_val[R_COLOUR], "-");
        lv_obj_set_style_text_color(s_val[R_COLOUR], lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    } else if (uses_palette()) {
        lv_label_set_text(s_val[R_COLOUR], (n = wled_pal_name(s_p.pal)) ? n : "?");
        lv_obj_set_style_text_color(s_val[R_COLOUR], lv_color_hex(ORANGE_LIGHTER), LV_PART_MAIN);
    } else {
        int i = colour_idx();
        lv_label_set_text(s_val[R_COLOUR], i < 0 ? "Custom" : COLOURS[i].name);
        const uint8_t *c = s_p.col[0];
        lv_obj_set_style_text_color(s_val[R_COLOUR], lv_color_make(c[0], c[1], c[2]), LV_PART_MAIN);
    }
    if (s_on) {
        set_fmt(s_val[R_SPEED], "%d/8", (s_p.sx + SPEED_STEP / 2) / SPEED_STEP);
    } else {
        lv_label_set_text(s_val[R_SPEED], "-");
    }
    set_fmt(s_val[R_BRI], "%d%%", bri_pct());
    lv_label_set_text(s_val[R_MIRROR], badge_config.led_mirror ? "On" : "Off");
}

static void look_changed(void) {
    wled_mirror_preview(s_on ? &s_p : NULL); // also drops a mirrored look: the wrench shows what you pick here
    schedule_save();
    refresh();
}

static void step(row_t r, int dir) {
    switch (r) {
        case R_FX: {
            int n = WLED_SHOWCASE_COUNT + 1, p = showcase_pos() + 1; // slot 0 = Off
            p     = (p + dir + n) % n;
            if (p == 0) {
                s_on = false;
            } else {
                load_showcase(p - 1);
            }
            look_changed();
            return;
        }
        case R_COLOUR:
            if (!s_on) {
                return;
            }
            if (uses_palette()) {
                int n   = wled_pal_count();
                s_p.pal = (uint8_t)((s_p.pal + dir + n) % n);
            } else {
                int i = colour_idx();
                i     = i < 0 ? 0 : (i + dir + COLOUR_COUNT) % COLOUR_COUNT;
                memcpy(s_p.col[0], COLOURS[i].rgb, 3);
            }
            look_changed();
            return;
        case R_SPEED: {
            if (!s_on) {
                return;
            }
            int x  = s_p.sx + dir * SPEED_STEP;
            s_p.sx = (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x);
            look_changed();
            return;
        }
        case R_BRI: {
            int pct = bri_pct() + dir * 5;
            pct     = pct < 0 ? 0 : pct > 100 ? 100 : pct;
            badge_config.led_brightness = (uint8_t)((pct * 255) / 100);
            led_set_brightness(badge_config.led_brightness);
            break;
        }
        case R_MIRROR:
            badge_config.led_mirror = !badge_config.led_mirror;
            if (!badge_config.led_mirror) {
                wled_mirror_clear(); // back to the look picked here
            }
            break;
        default: return;
    }
    schedule_save(); // badge-wide rows: no preview, so a mirrored look stays up
    refresh();
}

// -------------------------------------------------------------------------------------------------
// Effect list
// -------------------------------------------------------------------------------------------------
static void picker_select(void) {
    int sel = (int)lv_roller_get_selected(s_roller); // 0 = Off
    if (sel == 0) {
        s_on = false;
    } else {
        load_showcase(sel - 1);
    }
    wled_mirror_preview(s_on ? &s_p : NULL);
}

static void picker_close(bool keep) {
    if (!keep) {
        s_p  = s_before;
        s_on = s_before_on;
        wled_mirror_preview(s_on ? &s_p : NULL);
    }
    schedule_save();
    s_picking = false;
    ui_request_rerender();
}

static void picker_key(lv_event_t *e) { // preprocess: we drive the roller ourselves, nav and the class stay out
    lv_key_t key = lv_event_get_key(e);
    lv_event_stop_processing(e);
    if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
        int n   = (int)lv_roller_get_option_count(s_roller);
        int sel = ((int)lv_roller_get_selected(s_roller) + (key == LV_KEY_DOWN ? 1 : -1) + n) % n;
        lv_roller_set_selected(s_roller, (uint32_t)sel, LV_ANIM_ON);
        picker_select(); // live preview while scrolling
    } else if (key == LV_KEY_LEFT) {
        picker_close(false);
    }
    // ENTER: handled on CLICKED (release), so the release cannot land on the rebuilt list
}

static void picker_clicked(lv_event_t *e) {
    (void)e;
    picker_close(true);
}

static void build_picker(lv_obj_t *root) {
    size_t cap = (WLED_SHOWCASE_COUNT + 1) * 26u + 4;
    char *opts = malloc(cap);
    if (!opts) {
        s_picking = false;
        return;
    }
    size_t len = snprintf(opts, cap, "Off");
    for (int i = 0; i < WLED_SHOWCASE_COUNT; i++) {
        const char *name = wled_fx_name(WLED_SHOWCASE[i].fx);
        len += snprintf(opts + len, cap - len, "\n%.24s", name ? name : "?");
    }
    s_roller = lv_roller_create(root);
    lv_roller_set_options(s_roller, opts, LV_ROLLER_MODE_NORMAL);
    free(opts);
    lv_roller_set_selected(s_roller, (uint32_t)(showcase_pos() + 1), LV_ANIM_OFF);
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

// -------------------------------------------------------------------------------------------------
// Rows
// -------------------------------------------------------------------------------------------------
static void row_key(lv_event_t *e) { // preprocess: runs before nav, which would move focus on left/right
    lv_key_t key = lv_event_get_key(e);
    if (key != LV_KEY_LEFT && key != LV_KEY_RIGHT) {
        return;
    }
    lv_event_stop_processing(e);
    step((row_t)(intptr_t)lv_event_get_user_data(e), key == LV_KEY_RIGHT ? 1 : -1);
}

static void row_clicked(lv_event_t *e) {
    row_t r = (row_t)(intptr_t)lv_event_get_user_data(e);
    if (r == R_FX) {
        s_before    = s_p;
        s_before_on = s_on;
        s_picking   = true;
        ui_request_rerender();
    } else if (r == R_COLOUR || r == R_MIRROR) {
        step(r, 1);
    }
}

static void row_focused(lv_event_t *e) {
    s_focus = (row_t)(intptr_t)lv_event_get_user_data(e);
}

static void add_row(lv_obj_t *list, row_t r) {
    lv_obj_t *btn = lv_list_add_button(list, NULL, ROW_NAMES[r]);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_pad_ver(btn, 10, LV_PART_MAIN);
    lv_obj_t *v = lv_label_create(btn);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_max_width(v, 140, LV_PART_MAIN);
    lv_obj_set_style_text_color(v, lv_color_hex(ORANGE_LIGHTER), LV_PART_MAIN);
    s_val[r] = v;
    lv_obj_add_event_cb(btn, row_key, LV_EVENT_KEY | LV_EVENT_PREPROCESS, (void *)(intptr_t)r);
    lv_obj_add_event_cb(btn, row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)r);
    lv_obj_add_event_cb(btn, row_focused, LV_EVENT_FOCUSED, (void *)(intptr_t)r);
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
    if (!s_val[R_FX]) {
        return;
    }
    lv_obj_t *list = lv_obj_get_parent(lv_obj_get_parent(s_val[R_FX]));
    nav_register_children(nav_ctx, list);
    nav_autolink(nav_ctx);
    nav_focus(nav_ctx, lv_obj_get_parent(s_val[s_focus]));
}

static void root_deleted(lv_event_t *e) {
    (void)e;
    memset(s_val, 0, sizeof(s_val));
    s_roller = NULL;
}

void leds_app_create(lv_obj_t *parent) {
    content_register_nav_callback(APP_LEDS, leds_nav_setup, NULL);
    if (!s_picking) {
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
    lv_label_set_text(title, s_picking ? "Pick an effect" : "Wrench LEDs");
    lv_obj_set_style_text_color(title, lv_color_hex(WHITE), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(title, 6, LV_PART_MAIN);

    if (s_picking) {
        build_picker(root);
    }
    if (!s_picking) { // also when the list could not be built
        lv_obj_t *list = lv_list_create(root);
        lv_obj_set_width(list, lv_pct(100));
        lv_obj_set_flex_grow(list, 1);
        lv_obj_set_style_bg_color(list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 5, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
        lv_obj_set_style_text_color(list, lv_color_hex(WHITE), LV_PART_MAIN);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_16, LV_PART_MAIN);
        for (int r = 0; r < R_COUNT; r++) {
            add_row(list, (row_t)r);
        }
        refresh();
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, s_picking ? "Up/down preview, centre pick, left cancel"
                                      : "Left/right change. Centre on Effect: list");
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(hint, 6, LV_PART_MAIN);
}
