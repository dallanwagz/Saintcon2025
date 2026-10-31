#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "lvgl.h"

/**
 * @brief Start the joystick driver and register it (plus the minibadge D-pad passthrough) with LVGL as a keypad.
 *
 * @param display The LVGL display object (unused for a keypad indev, kept for API symmetry).
 */
void input_init(lv_display_t *display);

/** The LVGL input device for the joystick. */
lv_indev_t *input_get_device();

/**
 * @brief While enabled, LVGL sees the joystick as released so another consumer (the HID profile) owns it.
 */
void input_set_passthrough(bool enable);
bool input_get_passthrough(void);

#ifdef __cplusplus
}
#endif
