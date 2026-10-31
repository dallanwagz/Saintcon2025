#include "input.h"
#include "esp_log.h"

#include "display.h"
#include "joystick.h"
#include "minibadge.h"
#include "i2c_manager.h"

static const char *TAG __attribute__((unused)) = "input";

static lv_indev_t *g_indev = NULL;
static bool g_passthrough                 = false;
static bool g_passthrough_release_pending = false; // hand the stick back only once nothing is pressed

// Minibadge D-pad state tracking
static uint32_t minibadge_last_key                                 = 0;
static bool minibadge_key_pressed                                  = false;
static minibadge_slot_t minibadge_dpad_slots[MINIBADGE_SLOT_COUNT] = {0};
static int minibadge_dpad_count                                    = 0;
static minibadge_slot_t minibadge_dpad_active_slot                 = (minibadge_slot_t)(-1); // only one logical D-pad supported

// Map minibadge D-pad state to LVGL key codes
static uint32_t minibadge_dpad_state_to_key(minibadge_dpad_state_t state) {
    switch (state) {
        case MINIBADGE_DPAD_UP: return LV_KEY_UP;
        case MINIBADGE_DPAD_DOWN: return LV_KEY_DOWN;
        case MINIBADGE_DPAD_LEFT: return LV_KEY_LEFT;
        case MINIBADGE_DPAD_RIGHT: return LV_KEY_RIGHT;
        case MINIBADGE_DPAD_PRESS: return LV_KEY_ENTER;
        default: return 0;
    }
}

// Event handler for minibadge D-pad events
static void minibadge_dpad_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    if (id == MINIBADGE_DPAD_EVENT_PRESS) {
        minibadge_dpad_event_t *event = (minibadge_dpad_event_t *)event_data;

        if (event->state == MINIBADGE_DPAD_NONE) {
            // Button released
            minibadge_key_pressed = false;
            minibadge_last_key    = 0;
        } else {
            // Button pressed
            minibadge_last_key    = minibadge_dpad_state_to_key(event->state);
            minibadge_key_pressed = (minibadge_last_key != 0);

            if (minibadge_key_pressed) {
                screen_timeout_reset();
                ESP_LOGD(TAG, "Minibadge D-pad [slot %d]: key=%lu", event->slot + 1, minibadge_last_key);
            }
        }
    }
}

// Callback for minibadge insertion/removal events
static void minibadge_insertion_callback(minibadge_event_t event) {
    // Treat the bus as hosting at most one logical D-pad. Any INSERT/REPLACED event triggers a probe if inactive.
    if (event.type == MINIBADGE_EVENT_INSERTED || event.type == MINIBADGE_EVENT_REPLACED) {
        if (minibadge_dpad_count == 0) {
            i2c_manager_device_config_t probe_cfg = {
                .bus_index = I2C_BUS_B,
                .config =
                    {
                        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                        .device_address  = MINIBADGE_I2C_ADDR_DPAD,
                        .scl_speed_hz    = 100000,
                    },
            };
            if (i2c_manager_ping(&probe_cfg) == ESP_OK) {
                minibadge_dpad_slots[0]    = event.slot;
                minibadge_dpad_active_slot = event.slot;
                minibadge_dpad_count       = 1;
                minibadge_dpad_poll(true, event.slot);
                ESP_LOGI(TAG, "D-pad polling enabled");
            }
        }
    } else if (event.type == MINIBADGE_EVENT_REMOVED) {
        // On any removal, if active, stop. We rely on future INSERT to restart.
        if (minibadge_dpad_count > 0) {
            minibadge_dpad_poll(false, minibadge_dpad_active_slot);
            minibadge_dpad_count       = 0;
            minibadge_dpad_active_slot = (minibadge_slot_t)(-1);
            ESP_LOGI(TAG, "D-pad polling disabled (removal)");
        }
    }
}

static void joystick_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    static uint32_t last_key = 0;
    uint8_t joy              = joystick_get_state();
    if (g_passthrough) { // someone else (HID) owns the stick: report nothing to LVGL
        if (g_passthrough_release_pending && joy == 0 && !minibadge_key_pressed) {
            g_passthrough                 = false; // the press that ended HID mode never reaches the UI
            g_passthrough_release_pending = false;
        }
        data->state = LV_INDEV_STATE_RELEASED;
        data->key   = 0;
        last_key    = 0;
        return;
    }
    bool up     = joy & JOY_BIT(JOY_UP);
    bool down   = joy & JOY_BIT(JOY_DOWN);
    bool left   = joy & JOY_BIT(JOY_LEFT);
    bool right  = joy & JOY_BIT(JOY_RIGHT);
    bool center = joy & JOY_BIT(JOY_CENTER);

    // Merge hardware joystick and minibadge D-pad inputs
    bool any_pressed = up || down || left || right || center || minibadge_key_pressed;

    data->state = LV_INDEV_STATE_RELEASED;
    data->key   = last_key;

    if (any_pressed) {
        data->state = LV_INDEV_STATE_PRESSED;

        // Prioritize hardware joystick, but allow minibadge if hardware is idle
        if (up) {
            last_key = data->key = LV_KEY_UP;
        } else if (down) {
            last_key = data->key = LV_KEY_DOWN;
        } else if (left) {
            last_key = data->key = LV_KEY_LEFT;
        } else if (right) {
            last_key = data->key = LV_KEY_RIGHT;
        } else if (center) {
            last_key = data->key = LV_KEY_ENTER;
        } else if (minibadge_key_pressed) {
            // Use minibadge input when hardware joystick is idle
            last_key = data->key = minibadge_last_key;
        }

        screen_timeout_reset();
    } else if (last_key != 0) {
        screen_timeout_reset();
        last_key = 0; // Reset last key when no button is pressed
    }
}

void input_init(lv_display_t *display) {
    (void)display;
    if (joystick_init() != ESP_OK) {
        ESP_LOGE(TAG, "Joystick init failed");
    }

    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);

    lv_indev_t *joy_indev = lv_indev_create();
    lv_indev_set_type(joy_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_group(joy_indev, g);
    lv_indev_set_read_cb(joy_indev, joystick_read_cb);
    g_indev = joy_indev;

    // Register minibadge callbacks for automatic D-pad detection
    minibadge_add_event_callback(minibadge_insertion_callback);
    // Register for D-pad events (deferred until minibadge_init if not ready yet)
    if (minibadge_register_dpad_handler(minibadge_dpad_event_handler, NULL) == ESP_OK) {
        ESP_LOGI(TAG, "Minibadge D-pad event handler registration requested");
    } else {
        ESP_LOGW(TAG, "Failed to request minibadge D-pad handler registration");
    }
}

lv_indev_t *input_get_device() {
    return g_indev;
}

void input_set_passthrough(bool enable) {
    if (enable) {
        g_passthrough                 = true;
        g_passthrough_release_pending = false;
    } else {
        g_passthrough_release_pending = true; // dropped by the read callback once the stick is released
    }
}

bool input_get_passthrough(void) {
    return g_passthrough;
}
