#include "esp_log.h"
#include "esp_timer.h"

#include "menu.h"
#include "nav.h"
#include "theme.h"
#include "ui.h"

#include "../content.h"

static const char *TAG __attribute__((unused)) = "screens/main [apps/menu]";

// List of apps in the menu
content_app_t menu_apps[] = {
    APP_HID,
    APP_HAMENU,
    APP_LEDS,
    APP_NFC,
    APP_SETTINGS,
    APP_HWTEST,
};

#define MENU_DEFAULT_ITEM APP_HID

// Forward declarations
static void menu_event_handler(lv_event_t *e);
static void menu_focus_event_handler(lv_event_t *e);
static void menu_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data);
static void menu_konami_key_handler(lv_event_t *e);

// -------------------------------------------------------------------------------------------------
// Konami code (opens the hardware test screen)
// -------------------------------------------------------------------------------------------------
typedef enum {
    KONAMI_UP1,
    KONAMI_UP2,
    KONAMI_DOWN1,
    KONAMI_DOWN2,
    KONAMI_LEFT1,
    KONAMI_RIGHT1,
    KONAMI_LEFT2,
    KONAMI_RIGHT2,
    KONAMI_CENTER,
    KONAMI_COMPLETE
} konami_step_t;

typedef struct {
    konami_step_t current_step;
    int64_t last_input_time;
    bool sequence_active;
} konami_state_t;

#define KONAMI_TIMEOUT_US (1000000) // 1 second timeout between inputs
static konami_state_t konami_state = {
    .current_step    = KONAMI_UP1,
    .last_input_time = 0,
    .sequence_active = false,
};

// Static state for focus restoration
static content_app_t last_focused_app = APP_NONE;

// Menu state tracking
typedef struct {
    content_app_t currently_focused_app;
    bool in_setup;
} menu_state_t;
static menu_state_t menu_state = {
    .currently_focused_app = APP_NONE,
    .in_setup              = false,
};

void menu_app_create(lv_obj_t *parent) {
    // Register our navigation callback
    content_register_nav_callback(APP_MENU, menu_nav_setup, NULL);

    lv_obj_t *menu = lv_obj_create(parent);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(menu, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *menu_list = lv_list_create(menu);
    lv_obj_set_size(menu_list, LV_PCT(85), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(menu_list, lv_color_hex(GRAY_SHADE_5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu_list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(menu_list, lv_color_hex(MENU_BUTTON_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_list, 0, LV_PART_MAIN);
    lv_obj_align(menu_list, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(menu_list, lv_color_hex(MENU_BUTTON_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(menu_list, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_radius(menu_list, 5, LV_PART_MAIN);

    // Add menu items
    for (size_t i = 0; i < sizeof(menu_apps) / sizeof(menu_apps[0]); i++) {
        content_app_t app = menu_apps[i];
        lv_obj_t *btn     = lv_list_add_button(menu_list, content_app_symbols[app], get_content_app_label(app));
        lv_obj_t *icon    = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_font(icon, &fa_duotone_icons_16, LV_PART_MAIN);
        lv_obj_set_style_opa(btn, LV_OPA_80, LV_PART_MAIN);
        lv_obj_set_user_data(btn, (void *)app);
        lv_obj_add_event_cb(btn, menu_konami_key_handler, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(btn, menu_event_handler, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(btn, menu_focus_event_handler, LV_EVENT_FOCUSED, NULL);
    }
}

static void menu_focus_event_handler(lv_event_t *e) {
    lv_obj_t *obj        = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        // Don't update state if we're in the middle of setup
        if (menu_state.in_setup) {
            return;
        }

        // Update our state tracking when focus changes
        content_app_t app = (content_app_t)lv_obj_get_user_data(obj);
        if (app != APP_NONE) {
            menu_state.currently_focused_app = app;
            ESP_LOGD(TAG, "Focus changed to app: %d [%s]", app, get_content_app_label(app));
        }
    }
}

static void menu_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_obj_t *btn     = lv_event_get_target(e);
    content_app_t app = (content_app_t)lv_obj_get_user_data(btn);
    ESP_LOGD(TAG, "Menu item clicked: %d [%s]", app, get_content_app_label(app));

    // Save the focused app for restoration later
    last_focused_app = app;

    if (app == APP_HWTEST) {
        set_screen(SCREEN_HWTEST); // Separate full screen, not a content app
    } else {
        launch_app(app);
    }
}

static void menu_konami_key_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_KEY) {
        return;
    }

    lv_key_t key         = lv_event_get_key(e);
    int64_t current_time = esp_timer_get_time();

    // Check if we've timed out since the last input
    if (konami_state.sequence_active && (current_time - konami_state.last_input_time) > KONAMI_TIMEOUT_US) {
        konami_state.current_step    = KONAMI_UP1;
        konami_state.sequence_active = false;
    }

    // Expected key for the current step
    lv_key_t expected_key;
    switch (konami_state.current_step) {
        case KONAMI_UP1:
        case KONAMI_UP2: expected_key = LV_KEY_UP; break;
        case KONAMI_DOWN1:
        case KONAMI_DOWN2: expected_key = LV_KEY_DOWN; break;
        case KONAMI_LEFT1:
        case KONAMI_LEFT2: expected_key = LV_KEY_LEFT; break;
        case KONAMI_RIGHT1:
        case KONAMI_RIGHT2: expected_key = LV_KEY_RIGHT; break;
        case KONAMI_CENTER: expected_key = LV_KEY_ENTER; break;
        default: expected_key = LV_KEY_ENTER; break;
    }

    if (key == expected_key) {
        konami_state.last_input_time = current_time;
        konami_state.sequence_active = true;
        if (konami_state.current_step == KONAMI_CENTER) {
            ESP_LOGI(TAG, "Konami code ACTIVATED! Switching to hardware test...");
            konami_state.current_step    = KONAMI_UP1;
            konami_state.sequence_active = false;
            set_screen(SCREEN_HWTEST);
        } else {
            konami_state.current_step++;
        }
    } else if (key == LV_KEY_UP) {
        // UP always (re)starts the sequence
        konami_state.current_step    = KONAMI_UP2;
        konami_state.last_input_time = current_time;
        konami_state.sequence_active = true;
    } else if (konami_state.sequence_active) {
        konami_state.current_step    = KONAMI_UP1;
        konami_state.sequence_active = false;
    }
}

static void menu_nav_setup(lv_obj_t *content_area, nav_ctx_t *nav_ctx, void *user_data) {
    (void)user_data;

    // Reset Konami code sequence when entering menu
    konami_state.current_step    = KONAMI_UP1;
    konami_state.sequence_active = false;
    konami_state.last_input_time = 0;

    // Set flag to prevent focus events from doing anything to the state during setup
    menu_state.in_setup = true;

    // Find the menu components
    lv_obj_t *menu      = lv_obj_get_child(content_area, 0);
    lv_obj_t *menu_list = menu ? lv_obj_get_child_by_type(menu, 0, &lv_list_class) : NULL;

    if (menu_list == NULL) {
        ESP_LOGW(TAG, "Could not find menu components");
        menu_state.in_setup = false;
        return;
    }

    // Register all menu buttons
    nav_register_children(nav_ctx, menu_list);
    nav_autolink(nav_ctx);

    // Restore focus: current state, then last launched app, then the default
    content_app_t target_app = menu_state.currently_focused_app;
    if (target_app == APP_NONE || target_app == APP_MENU) {
        target_app = last_focused_app;
    }
    if (target_app == APP_NONE || target_app == APP_MENU) {
        target_app = MENU_DEFAULT_ITEM;
    }

    lv_obj_t *focus_target = NULL;
    uint32_t child_count   = lv_obj_get_child_count(menu_list);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *btn = lv_obj_get_child(menu_list, i);
        if (btn != NULL && (content_app_t)lv_obj_get_user_data(btn) == target_app) {
            focus_target = btn;
            break;
        }
    }
    if (focus_target == NULL) {
        focus_target = lv_obj_get_child(menu_list, 0);
        if (focus_target != NULL) {
            target_app = (content_app_t)lv_obj_get_user_data(focus_target);
        }
    }

    menu_state.currently_focused_app = target_app;
    menu_state.in_setup              = false;

    if (focus_target != NULL) {
        nav_focus(nav_ctx, focus_target);
    }
}
