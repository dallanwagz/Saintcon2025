// On-screen keyboard popup. LVGL's lv_keyboard already moves its selection with LV_KEY_LEFT/RIGHT/UP/DOWN
// and presses the selected key on LV_KEY_ENTER, which is exactly what the joystick sends, so the popup only
// has to own a focus group holding the keyboard (nav_scope_push) and give it a small 240 px friendly map.
#include "kbd_popup.h"

#include <stdio.h>
#include <string.h>

#include "input.h"
#include "lvgl.h"
#include "nav.h"
#include "theme.h"

typedef struct {
    nav_scope_guard_t guard;
    lv_indev_t *keypad;
    lv_group_t *group;
    lv_obj_t *overlay, *ta;
    kbd_popup_done_cb_t done;
    void *user_data;
} kbd_ctx_t;

// 7 keys per row fit 240 px at ~30 px each. lv_keyboard switches pages on the "ABC" / "abc" / "1#" texts and
// handles backspace, OK (READY) and close (CANCEL) itself.
#define KB_BOTTOM "\n", "1#", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_CLOSE, LV_SYMBOL_OK, ""
// clang-format off
static const char *const MAP_UPPER[] = {"A", "B", "C", "D", "E", "F", "G", "\n",
                                        "H", "I", "J", "K", "L", "M", "N", "\n",
                                        "O", "P", "Q", "R", "S", "T", "U", "\n",
                                        "V", "W", "X", "Y", "Z", "-", "abc", KB_BOTTOM};
static const char *const MAP_LOWER[] = {"a", "b", "c", "d", "e", "f", "g", "\n",
                                        "h", "i", "j", "k", "l", "m", "n", "\n",
                                        "o", "p", "q", "r", "s", "t", "u", "\n",
                                        "v", "w", "x", "y", "z", "-", "ABC", KB_BOTTOM};
static const char *const MAP_NUM[]   = {"1", "2", "3", "4", "5", "\n",
                                        "6", "7", "8", "9", "0", "\n",
                                        ".", "_", "!", "?", "ABC", "\n",
                                        "1#", " ", LV_SYMBOL_BACKSPACE, LV_SYMBOL_CLOSE, LV_SYMBOL_OK, ""};
#define W(n) ((lv_buttonmatrix_ctrl_t)(n) | LV_BUTTONMATRIX_CTRL_NO_REPEAT)
static const lv_buttonmatrix_ctrl_t CTRL_ALPHA[] = {
    W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1), W(1), W(1) | LV_BUTTONMATRIX_CTRL_CHECKED,
    W(2) | LV_BUTTONMATRIX_CTRL_CHECKED, W(3), W(2), W(2), W(2) | LV_BUTTONMATRIX_CTRL_CHECKED,
};
static const lv_buttonmatrix_ctrl_t CTRL_NUM[] = {
    W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1),
    W(1), W(1), W(1), W(1), W(1) | LV_BUTTONMATRIX_CTRL_CHECKED,
    W(2) | LV_BUTTONMATRIX_CTRL_CHECKED, W(3), W(2), W(2), W(2) | LV_BUTTONMATRIX_CTRL_CHECKED,
};
// clang-format on

static void close_popup(kbd_ctx_t *ctx, bool ok) {
    char text[64];
    snprintf(text, sizeof(text), "%s", lv_textarea_get_text(ctx->ta));
    nav_scope_pop(ctx->keypad, ctx->guard);
    lv_group_delete(ctx->group);
    lv_obj_delete_async(ctx->overlay); // we are inside one of its children's event handlers
    kbd_popup_done_cb_t done = ctx->done;
    void *ud                 = ctx->user_data;
    lv_free(ctx);
    if (ok && done) {
        done(text, ud);
    }
}

static void kb_event(lv_event_t *e) {
    kbd_ctx_t *ctx = lv_event_get_user_data(e);
    close_popup(ctx, lv_event_get_code(e) == LV_EVENT_READY);
}

void kbd_popup_open(const char *title, const char *initial, uint8_t max_len, kbd_popup_done_cb_t done, void *user_data) {
    kbd_ctx_t *ctx = lv_malloc_zeroed(sizeof(kbd_ctx_t));
    if (!ctx) {
        return;
    }
    ctx->done      = done;
    ctx->user_data = user_data;

    lv_obj_t *ov = lv_obj_create(lv_screen_active());
    ctx->overlay = ov;
    lv_obj_set_size(ov, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
    lv_obj_set_style_bg_color(ov, lv_color_hex(GRAY_SHADE_7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ov, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(ov, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(ov, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(ov);
    lv_label_set_text(t, title ? title : "");
    lv_obj_set_style_text_font(t, &white_rabbit_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, lv_color_hex(WHITE), LV_PART_MAIN);

    ctx->ta = lv_textarea_create(ov);
    lv_textarea_set_one_line(ctx->ta, true);
    lv_textarea_set_max_length(ctx->ta, max_len);
    lv_textarea_set_text(ctx->ta, initial ? initial : "");
    lv_obj_set_width(ctx->ta, lv_pct(100));
    lv_obj_set_style_text_font(ctx->ta, &white_rabbit_22, LV_PART_MAIN);
    lv_obj_add_state(ctx->ta, LV_STATE_FOCUSED); // shows the cursor; the keyboard holds the real focus
    lv_obj_remove_flag(ctx->ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    lv_obj_t *hint = lv_label_create(ov);
    lv_label_set_text_fmt(hint, "Up to %u characters. Centre types the key.", max_len);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(GRAY_SHADE_2), LV_PART_MAIN);

    lv_obj_t *kb = lv_keyboard_create(ov);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, MAP_UPPER, CTRL_ALPHA);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, MAP_LOWER, CTRL_ALPHA);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, MAP_NUM, CTRL_NUM);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_keyboard_set_textarea(kb, ctx->ta);
    lv_obj_set_width(kb, lv_pct(100));
    lv_obj_set_flex_grow(kb, 1);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(kb, 3, LV_PART_MAIN);
    // the selected key must be obvious: LVGL marks it with FOCUS_KEY on the items part
    lv_obj_set_style_bg_color(kb, lv_color_hex(BLUE_MAIN), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(kb, lv_color_hex(WHITE), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(kb, 2, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(kb, lv_color_hex(WHITE), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(kb, kb_event, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, kb_event, LV_EVENT_CANCEL, ctx);
    lv_buttonmatrix_set_selected_button(kb, 0);

    // Own focus scope holding only the keyboard: arrows go straight to it (no nav ctx to steal them)
    ctx->keypad = input_get_device();
    ctx->group  = lv_group_create();
    ctx->guard  = nav_scope_push(ctx->keypad, ctx->group);
    lv_group_add_obj(ctx->group, kb);
    lv_group_focus_obj(kb);
}
