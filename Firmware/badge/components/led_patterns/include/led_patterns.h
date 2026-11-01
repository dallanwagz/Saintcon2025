#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// -------------------------------------------------------------------------------------------------
// One-shot / status patterns (HIGH or MEDIUM priority layers on top of the wallpaper, see wled_mirror)
// -------------------------------------------------------------------------------------------------

/** Flash all LEDs a color (one-shot, HIGH priority, auto-destroys). */
void flash_leds(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness, int flashes, uint32_t on_ms, uint32_t off_ms);

/** Three red flashes: the generic "error" pattern. */
void led_pattern_flash_red();

/** One second of random white sparkles: the generic "success" pattern. */
void led_pattern_sparkles();

/**
 * @brief Start a continuous breathe (ease in / hold / ease out / hold) on all LEDs (MEDIUM priority).
 *        Used for status (e.g. "BLE advertising"); it sits above the LOW wallpaper (wled_mirror) while it runs.
 *
 * @param max_brightness Peak brightness (0-255). The pattern scales to this instead of the global brightness.
 * @return int Layer handle (>=0) on success, negative on error.
 */
int led_pattern_ease(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_in_ms, uint32_t hold_in_ms, uint32_t fade_out_ms,
                     uint32_t hold_out_ms, bool use_cosine_easing, uint8_t max_brightness);

/** Same as led_pattern_ease() but the breathe only shows for duration_ms (0 = forever), then the wallpaper
 *  below shows again. led_pattern_ease_stop() still works after it timed out. */
int led_pattern_ease_for(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_in_ms, uint32_t hold_in_ms, uint32_t fade_out_ms,
                         uint32_t hold_out_ms, bool use_cosine_easing, uint8_t max_brightness, uint32_t duration_ms);
/** Stop the breathe pattern started by led_pattern_ease(). */
void led_pattern_ease_stop();

/**
 * Timed gauge: `percent` of the ring lit in one colour, filling from LED 0 (top-left) down the left side,
 * across the bottom and up to LED 19 (top-right); a lower value empties from the top-right backwards.
 */
void led_pattern_gauge(uint8_t r, uint8_t g, uint8_t b, uint8_t percent, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
