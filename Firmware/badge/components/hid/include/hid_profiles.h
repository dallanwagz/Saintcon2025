#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * Joystick -> HID mappings. While a profile is "active" the joystick listener turns stick events into
 * HID reports; holding the center button for HID_EXIT_HOLD_MS releases everything and calls the exit
 * callback (the UI uses it to take the stick back).
 */

typedef enum {
    HID_PROFILE_MEDIA = 0,
    HID_PROFILE_PRESENTER,
    HID_PROFILE_ARROWS,
    HID_PROFILE_MOUSE,
    HID_PROFILE_COUNT,
} hid_profile_t;

#define HID_EXIT_HOLD_MS 1000 // hold center this long to hand the stick back to the UI
#define HID_TAP_MAX_MS   500  // a center press released within this is a "button" tap

extern const char *hid_profile_names[HID_PROFILE_COUNT];
extern const char *hid_profile_hints[HID_PROFILE_COUNT]; // multi-line legend for the UI

typedef void (*hid_exit_cb_t)(void); // called from the joystick task

void hid_profiles_init(void); // registers the joystick listener (call once)
void hid_profile_set(hid_profile_t profile);
hid_profile_t hid_profile_get(void);
void hid_profile_set_active(bool active);
bool hid_profile_is_active(void);
void hid_profile_set_exit_callback(hid_exit_cb_t cb);

#ifdef __cplusplus
}
#endif
