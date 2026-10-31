#include "esp_log.h"

#include "main.h"
#include "theme.h"
#include "ui.h"

#include "main/content.h"
#include "main/status.h"

static const char *TAG __attribute__((unused)) = "screens/main";

static lv_obj_t *scr_main = NULL;

lv_obj_t *create_main_screen() {
    static ui_screen_t screen_type = SCREEN_MAIN;

    scr_main = lv_obj_create(NULL);
    lv_obj_set_user_data(scr_main, &screen_type);
    lv_obj_set_size(scr_main, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(GRAY_SHADE_8), LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr_main, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr_main, LV_SCROLLBAR_MODE_OFF);

    // Status bar on top, app content below
    lv_obj_t *status_bar                           = create_status_bar(scr_main);
    lv_obj_t *content_area __attribute__((unused)) = create_content_area(scr_main, status_bar);
    content_mark_layout_dirty();

    return scr_main;
}

void render_main() {
    ui_state_t state = get_ui_state();

    if (state.screen == SCREEN_MAIN) {
        if (lvgl_lock(portMAX_DELAY, __FILE__, __LINE__)) {
            update_status_bar(&state);
            update_content_area();
            lvgl_unlock(__FILE__, __LINE__);
        }
    }
}
