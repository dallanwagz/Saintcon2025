#include "hid_profiles.h"
#include "esp_log.h"
#include "hid.h"
#include "joystick.h"
#include "bthome.h"

static const char *TAG = "hid_profiles";

// Mouse acceleration: pixels per repeat tick, from MIN right after the press up to MAX after ~1.5 s
#define MOUSE_STEP_MIN 3
#define MOUSE_STEP_MAX 18

const char *hid_profile_names[HID_PROFILE_COUNT] = {"Media Remote", "Presenter", "Arrow Keys", "Mouse", "Macro Keys", "Phone Buttons", "Home Assistant"};
const char *hid_profile_hints[HID_PROFILE_COUNT] = {
    "Up / Down: volume\nLeft / Right: previous / next track\nCenter: play / pause",
    "Right / Down: next slide\nLeft / Up: previous slide\nCenter: blank screen (B)",
    "Stick: arrow keys\nCenter: Enter",
    "Stick: move the pointer (hold to speed up)\nCenter: left click",
    "Up/Down/Left/Right: F13/F14/F15/F16\nCenter: F17\nBind them: Karabiner, Shortcuts,\niOS Full Keyboard Access",
    "Up/Down/Left/Right: mouse buttons 4-7\nCenter: button 8\niPhone: AssistiveTouch > Pointer Devices\n> Buttons runs a Shortcut per button",
    "Stick: HA buttons 1-4 (hold 0.5 s = long press)\nCenter: button 5\nBTHome broadcast: needs HA Bluetooth\nor an ESPHome Bluetooth proxy",
};

static hid_profile_t s_profile = HID_PROFILE_MEDIA;
static bool s_active           = false;
static hid_exit_cb_t s_exit_cb = NULL;
static uint8_t s_mouse_buttons = 0;
static uint8_t s_long_sent     = 0; // Home Assistant profile: bit per direction, long press already broadcast

static uint16_t media_usage(joystick_dir_t d) {
    switch (d) {
        case JOY_UP: return HID_CONSUMER_VOL_UP;
        case JOY_DOWN: return HID_CONSUMER_VOL_DOWN;
        case JOY_LEFT: return HID_CONSUMER_SCAN_PREV;
        case JOY_RIGHT: return HID_CONSUMER_SCAN_NEXT;
        default: return 0;
    }
}

static uint8_t presenter_key(joystick_dir_t d) {
    switch (d) {
        case JOY_RIGHT:
        case JOY_DOWN: return HID_KEY_PAGE_DOWN;
        case JOY_LEFT:
        case JOY_UP: return HID_KEY_PAGE_UP;
        default: return 0;
    }
}

static uint8_t arrow_key(joystick_dir_t d) {
    switch (d) {
        case JOY_UP: return HID_KEY_UP;
        case JOY_DOWN: return HID_KEY_DOWN;
        case JOY_LEFT: return HID_KEY_LEFT;
        case JOY_RIGHT: return HID_KEY_RIGHT;
        default: return 0;
    }
}

// Stick direction -> 0..3 (up, down, left, right); center -> 4
static int dir_index(joystick_dir_t d) {
    switch (d) {
        case JOY_UP: return 0;
        case JOY_DOWN: return 1;
        case JOY_LEFT: return 2;
        case JOY_RIGHT: return 3;
        default: return 4;
    }
}

static void release_all(void) {
    hid_key_release();
    hid_consumer_release();
    if (s_mouse_buttons) {
        s_mouse_buttons = 0;
        hid_mouse(0, 0, 0, 0);
    }
}

static void on_joystick(const joystick_event_t *ev, void *arg) {
    (void)arg;
    if (!s_active) {
        return;
    }
    ESP_LOGD(TAG, "joy dir=%d type=%d held=%lu profile=%d", ev->dir, ev->type, (unsigned long)ev->held_ms, s_profile);

    // Exit gesture: hold the center button
    if (ev->dir == JOY_CENTER && (ev->type == JOY_EVENT_HOLD || ev->type == JOY_EVENT_REPEAT) &&
        ev->held_ms >= HID_EXIT_HOLD_MS) {
        release_all();
        s_active = false;
        if (s_exit_cb) {
            s_exit_cb();
        }
        return;
    }
    // A quick center tap is the "button" in most profiles; a longer press does nothing (so a hold that falls
    // short of the exit threshold doesn't toggle play/pause by accident)
    const bool center_tap = ev->dir == JOY_CENTER && ev->type == JOY_EVENT_RELEASE && ev->held_ms < HID_TAP_MAX_MS;

    switch (s_profile) {
        case HID_PROFILE_MEDIA:
            if (ev->dir == JOY_CENTER) {
                if (center_tap) {
                    hid_consumer_tap(HID_CONSUMER_PLAY_PAUSE);
                }
            } else if (ev->type == JOY_EVENT_PRESS) {
                hid_consumer_press(media_usage(ev->dir)); // the host auto-repeats volume while held
            } else if (ev->type == JOY_EVENT_RELEASE) {
                hid_consumer_release();
            }
            break;

        case HID_PROFILE_PRESENTER:
            if (ev->dir == JOY_CENTER) {
                if (center_tap) {
                    hid_key_tap(0, HID_KEY_B);
                }
            } else if (ev->type == JOY_EVENT_PRESS) {
                hid_key_press(0, presenter_key(ev->dir));
            } else if (ev->type == JOY_EVENT_RELEASE) {
                hid_key_release();
            }
            break;

        case HID_PROFILE_ARROWS:
            if (ev->dir == JOY_CENTER) {
                if (center_tap) {
                    hid_key_tap(0, HID_KEY_ENTER);
                }
            } else if (ev->type == JOY_EVENT_PRESS) {
                hid_key_press(0, arrow_key(ev->dir)); // the host auto-repeats held keys
            } else if (ev->type == JOY_EVENT_RELEASE) {
                hid_key_release();
            }
            break;

        case HID_PROFILE_MOUSE:
            if (ev->dir == JOY_CENTER) {
                // Immediate press/release so drags work; the exit hold releases it first
                if (ev->type == JOY_EVENT_PRESS) {
                    s_mouse_buttons = HID_MOUSE_BUTTON_LEFT;
                    hid_mouse(s_mouse_buttons, 0, 0, 0);
                } else if (ev->type == JOY_EVENT_RELEASE) {
                    s_mouse_buttons = 0;
                    hid_mouse(0, 0, 0, 0);
                }
            } else if (ev->type == JOY_EVENT_PRESS || ev->type == JOY_EVENT_REPEAT) {
                int step = MOUSE_STEP_MIN + (int)(ev->held_ms / 100);
                if (step > MOUSE_STEP_MAX) {
                    step = MOUSE_STEP_MAX;
                }
                int8_t dx = ev->dir == JOY_LEFT ? -step : ev->dir == JOY_RIGHT ? step : 0;
                int8_t dy = ev->dir == JOY_UP ? -step : ev->dir == JOY_DOWN ? step : 0;
                hid_mouse(s_mouse_buttons, dx, dy, 0);
            }
            break;

        case HID_PROFILE_MACRO:
            // One tap per press, never held: a host macro must fire once, not auto-repeat
            if (ev->dir == JOY_CENTER ? center_tap : ev->type == JOY_EVENT_PRESS) {
                hid_key_tap(0, HID_KEY_F13 + dir_index(ev->dir));
            }
            break;

        case HID_PROFILE_BUTTONS:
            if (ev->dir == JOY_CENTER ? center_tap : ev->type == JOY_EVENT_PRESS) {
                hid_mouse(HID_MOUSE_BUTTON(4 + dir_index(ev->dir)), 0, 0, 0);
                hid_mouse(0, 0, 0, 0);
            }
            break;

        case HID_PROFILE_HOME: {
            // Nothing goes to the HID host here; the stick is a BTHome remote. Center has no long press
            // because holding it is the exit gesture.
            int i = dir_index(ev->dir);
            if (ev->type == JOY_EVENT_PRESS) {
                s_long_sent &= ~(1u << i);
            } else if (ev->type == JOY_EVENT_HOLD && ev->dir != JOY_CENTER) {
                s_long_sent |= 1u << i;
                bthome_button(i, BTHOME_LONG);
            } else if (ev->type == JOY_EVENT_RELEASE && !(s_long_sent & (1u << i)) &&
                       (ev->dir != JOY_CENTER || center_tap)) {
                bthome_button(i, BTHOME_PRESS);
            }
            break;
        }

        default: break;
    }
}

void hid_profiles_init(void) {
    joystick_add_listener(on_joystick, NULL);
}

void hid_profile_set(hid_profile_t profile) {
    if (profile >= HID_PROFILE_COUNT) {
        profile = HID_PROFILE_MEDIA;
    }
    if (profile != s_profile) {
        release_all();
    }
    s_profile = profile;
}

hid_profile_t hid_profile_get(void) {
    return s_profile;
}

void hid_profile_set_active(bool active) {
    if (!active) {
        release_all();
    }
    s_mouse_buttons = 0;
    s_active        = active;
}

bool hid_profile_is_active(void) {
    return s_active;
}

void hid_profile_set_exit_callback(hid_exit_cb_t cb) {
    s_exit_cb = cb;
}
