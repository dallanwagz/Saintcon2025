#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Called with the final text when the user presses OK (not called on cancel).
typedef void (*kbd_popup_done_cb_t)(const char *text, void *user_data);

/**
 * Full-screen popup with a one-line text field and an on-screen keyboard driven by the joystick:
 * arrows move between keys, centre presses the key. Keys: letters (ABC / abc pages), 1# page with digits,
 * space, backspace, cancel (X) and OK. Takes over navigation until closed and restores it afterwards.
 */
void kbd_popup_open(const char *title, const char *initial, uint8_t max_len, kbd_popup_done_cb_t done, void *user_data);

#ifdef __cplusplus
}
#endif
