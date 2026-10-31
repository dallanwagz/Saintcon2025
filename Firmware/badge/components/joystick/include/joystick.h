#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * Debounced 5-way joystick with press / release / hold / repeat events.
 *
 * A 10 ms poll task owns the GPIOs. Consumers either read the current state (LVGL keypad indev) or
 * register a listener (HID profiles). Listeners run in the joystick task: keep them short.
 */

typedef enum { JOY_UP = 0, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_CENTER, JOY_DIR_COUNT } joystick_dir_t;

typedef enum {
    JOY_EVENT_PRESS,   // debounced press
    JOY_EVENT_RELEASE, // debounced release; held_ms = how long it was down
    JOY_EVENT_HOLD,    // fired once when a press has lasted JOY_HOLD_MS
    JOY_EVENT_REPEAT,  // fired every JOY_REPEAT_MS while held (starts right after PRESS)
} joystick_event_type_t;

typedef struct {
    joystick_dir_t dir;
    joystick_event_type_t type;
    uint32_t held_ms; // time since the press (0 for PRESS)
} joystick_event_t;

typedef void (*joystick_listener_t)(const joystick_event_t *ev, void *arg);

#define JOY_BIT(dir) (1u << (dir))

esp_err_t joystick_init(void);

/** Bitmask of JOY_BIT(dir) for directions currently (debounced) pressed. */
uint8_t joystick_get_state(void);

esp_err_t joystick_add_listener(joystick_listener_t cb, void *arg);

#ifdef __cplusplus
}
#endif
