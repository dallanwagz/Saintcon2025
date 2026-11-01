#include <math.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "esp_log.h"

#include "led_patterns.h"
#include "led.h"

static const char *TAG = "led_patterns";

// -------------------------------------------------------------------------------------------------
// Flash arbitrary color pattern
// -------------------------------------------------------------------------------------------------
typedef struct {
    uint64_t start_ms;
    uint32_t on_ms;
    uint32_t off_ms;
    int flashes;
    uint8_t r, g, b;
    uint8_t brightness;
} flash_ctx_t;

static flash_ctx_t g_flash_ctx;
static int g_flash_layer = -1;

static void flash_render(uint32_t now_ms, void *user_ctx) {
    (void)user_ctx;
    uint32_t elapsed   = (uint32_t)(now_ms - g_flash_ctx.start_ms);
    uint32_t period_ms = g_flash_ctx.on_ms + g_flash_ctx.off_ms;
    int flash_index    = elapsed / period_ms;
    if (flash_index >= g_flash_ctx.flashes) {
        return;
    }
    uint32_t in_cycle = elapsed % period_ms;
    bool on           = in_cycle < g_flash_ctx.on_ms;
    if (on) {
        for (int i = 0; i < NUM_LEDS; i++) {
            led_set_with_brightness(i, g_flash_ctx.r, g_flash_ctx.g, g_flash_ctx.b, g_flash_ctx.brightness);
        }
    }
}

void flash_leds(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness, int flashes, uint32_t on_ms, uint32_t off_ms) {
    if (g_flash_layer >= 0) {
        led_layer_destroy(g_flash_layer);
        g_flash_layer = -1;
    }
    g_flash_ctx.start_ms   = esp_timer_get_time() / 1000ULL;
    g_flash_ctx.on_ms      = on_ms;
    g_flash_ctx.off_ms     = off_ms;
    g_flash_ctx.flashes    = flashes;
    g_flash_ctx.r          = r;
    g_flash_ctx.g          = g;
    g_flash_ctx.b          = b;
    g_flash_ctx.brightness = brightness;
    uint32_t duration      = flashes * (on_ms + off_ms);
    g_flash_layer          = led_layer_create_timed(LED_PRIORITY_HIGH, flash_render, &g_flash_ctx, true, true, duration, true);
}

typedef struct {
    uint8_t r, g, b;
    int lit;
} gauge_ctx_t;
static gauge_ctx_t g_gauge_ctx;
static int g_gauge_layer = -1;

static void gauge_render(uint32_t now_ms, void *user_ctx) {
    (void)now_ms;
    (void)user_ctx;
    for (int i = 0; i < g_gauge_ctx.lit && i < NUM_LEDS; i++) {
        led_set_scaled(i, g_gauge_ctx.r, g_gauge_ctx.g, g_gauge_ctx.b);
    }
}

void led_pattern_gauge(uint8_t r, uint8_t g, uint8_t b, uint8_t percent, uint32_t duration_ms) {
    if (g_gauge_layer >= 0) {
        led_layer_destroy(g_gauge_layer); // never auto-destroyed, so the handle is still ours
        g_gauge_layer = -1;
    }
    if (percent > 100) {
        percent = 100;
    }
    int lit = (percent * NUM_LEDS + 50) / 100;
    if (percent > 0 && lit == 0) {
        lit = 1; // a nearly flat battery still shows one LED
    }
    g_gauge_ctx   = (gauge_ctx_t){.r = r, .g = g, .b = b, .lit = lit};
    g_gauge_layer = led_layer_create_timed(LED_PRIORITY_HIGH, gauge_render, &g_gauge_ctx, true, true, duration_ms, false);
}

void led_pattern_flash_red() {
    flash_leds(255, 0, 0, 80, 3, 150, 150);
}

// -------------------------------------------------------------------------------------------------
// Success "sparkle" (random white LEDs with fade in/out)
// -------------------------------------------------------------------------------------------------
typedef struct {
    uint64_t start_ms;
    uint32_t duration_ms;
    uint32_t sparkle_count;
    uint32_t sparkle_period_ms;
    uint8_t brightness;
    uint32_t seed;
} sparkle_ctx_t;

static sparkle_ctx_t g_sparkle_ctx;
static int g_sparkle_layer = -1;

static void sparkle_render(uint32_t now_ms, void *user_ctx) {
    sparkle_ctx_t *ctx = (sparkle_ctx_t *)user_ctx;
    uint32_t elapsed   = (uint32_t)(now_ms - ctx->start_ms);
    if (elapsed >= ctx->duration_ms) {
        return;
    }

    // Each sparkle lasts sparkle_period_ms, fades in/out
    float sparkle_phase        = (float)(elapsed % ctx->sparkle_period_ms) / (float)ctx->sparkle_period_ms;
    float fade                 = 0.5f * (1.0f - cosf(2.0f * M_PI * sparkle_phase)); // cosine fade in/out
    uint8_t sparkle_brightness = (uint8_t)(ctx->brightness * fade);

    // Use a simple LCG for repeatable pseudo-randomness per frame
    uint32_t seed = ctx->seed + elapsed / 16;
    for (int i = 0; i < ctx->sparkle_count; i++) {
        seed    = seed * 1664525 + 1013904223;
        int led = seed % NUM_LEDS;
        led_set_with_brightness(led, 255, 255, 255, sparkle_brightness);
    }
}

void led_pattern_sparkles() {
    if (g_sparkle_layer >= 0) {
        led_layer_destroy(g_sparkle_layer);
        g_sparkle_layer = -1;
    }
    g_sparkle_ctx.start_ms          = esp_timer_get_time() / 1000ULL;
    g_sparkle_ctx.duration_ms       = 1000; // 1 second
    g_sparkle_ctx.sparkle_count     = 6;    // number of sparkles per frame
    g_sparkle_ctx.sparkle_period_ms = 180;  // ms per sparkle fade
    g_sparkle_ctx.brightness        = 90;
    g_sparkle_ctx.seed              = (uint32_t)g_sparkle_ctx.start_ms;
    g_sparkle_layer =
        led_layer_create_timed(LED_PRIORITY_HIGH, sparkle_render, &g_sparkle_ctx, true, true, g_sparkle_ctx.duration_ms, true);
}

// -------------------------------------------------------------------------------------------------
// Generic ease in/out pattern
// -------------------------------------------------------------------------------------------------
#define EASE_FIXED_POINT_SCALE 1024

typedef enum { EASE_PHASE_FADE_IN, EASE_PHASE_HOLD_IN, EASE_PHASE_FADE_OUT, EASE_PHASE_HOLD_OUT } ease_phase_t;

typedef struct {
    uint8_t r, g, b;
    uint8_t max_brightness;
    uint32_t fade_in_ms;
    uint32_t hold_in_ms;
    uint32_t fade_out_ms;
    uint32_t hold_out_ms;
    bool use_cosine;
    ease_phase_t phase;
    uint64_t phase_start_ms;
} ease_ctx_t;

static ease_ctx_t g_ease_ctx;
static int g_ease_layer = -1;

static void ease_render(uint32_t now_ms, void *user_ctx) {
    ease_ctx_t *ctx  = (ease_ctx_t *)user_ctx;
    uint32_t elapsed = (uint32_t)(now_ms - ctx->phase_start_ms);

    // Fixed-point brightness factor (0..EASE_FIXED_POINT_SCALE)
    uint32_t f_fp = 0;

    switch (ctx->phase) {
        case EASE_PHASE_FADE_IN: {
            if (elapsed >= ctx->fade_in_ms) {
                ctx->phase          = EASE_PHASE_HOLD_IN;
                ctx->phase_start_ms = now_ms;
                f_fp                = EASE_FIXED_POINT_SCALE;
            } else {
                uint64_t num = (uint64_t)elapsed * EASE_FIXED_POINT_SCALE;
                f_fp         = (uint32_t)(num / ctx->fade_in_ms);
                if (ctx->use_cosine) {
                    float t  = (float)f_fp / (float)EASE_FIXED_POINT_SCALE;
                    float cf = 0.5f - 0.5f * cosf((float)M_PI * t);
                    f_fp     = (uint32_t)(cf * (float)EASE_FIXED_POINT_SCALE + 0.5f);
                }
            }
            break;
        }
        case EASE_PHASE_HOLD_IN: {
            f_fp = EASE_FIXED_POINT_SCALE;
            if (elapsed >= ctx->hold_in_ms) {
                ctx->phase          = EASE_PHASE_FADE_OUT;
                ctx->phase_start_ms = now_ms;
            }
            break;
        }
        case EASE_PHASE_FADE_OUT: {
            if (elapsed >= ctx->fade_out_ms) {
                ctx->phase          = EASE_PHASE_HOLD_OUT;
                ctx->phase_start_ms = now_ms;
                f_fp                = 0;
            } else {
                uint64_t num = (uint64_t)(ctx->fade_out_ms - elapsed) * EASE_FIXED_POINT_SCALE;
                f_fp         = (uint32_t)(num / ctx->fade_out_ms);
                if (ctx->use_cosine) {
                    float t  = (float)f_fp / (float)EASE_FIXED_POINT_SCALE;
                    float cf = 0.5f - 0.5f * cosf((float)M_PI * t);
                    f_fp     = (uint32_t)(cf * (float)EASE_FIXED_POINT_SCALE + 0.5f);
                }
            }
            break;
        }
        case EASE_PHASE_HOLD_OUT: {
            f_fp = 0;
            if (elapsed >= ctx->hold_out_ms) {
                ctx->phase          = EASE_PHASE_FADE_IN;
                ctx->phase_start_ms = now_ms;
            }
            break;
        }
        default: f_fp = 0; break;
    }

    // Scale brightness and apply to all LEDs
    const uint32_t peak = ctx->max_brightness;
    const uint32_t FP   = EASE_FIXED_POINT_SCALE;

    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t r_lin = (uint32_t)ctx->r * f_fp * peak / (FP * 255);
        uint32_t g_lin = (uint32_t)ctx->g * f_fp * peak / (FP * 255);
        uint32_t b_lin = (uint32_t)ctx->b * f_fp * peak / (FP * 255);
        uint8_t r      = (r_lin > 255) ? 255 : (uint8_t)r_lin;
        uint8_t g      = (g_lin > 255) ? 255 : (uint8_t)g_lin;
        uint8_t b      = (b_lin > 255) ? 255 : (uint8_t)b_lin;
        led_gamma_apply_rgb(&r, &g, &b);
        led_set(i, r, g, b);
    }
}

int led_pattern_ease(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_in_ms, uint32_t hold_in_ms, uint32_t fade_out_ms,
                     uint32_t hold_out_ms, bool use_cosine_easing, uint8_t max_brightness) {
    return led_pattern_ease_for(r, g, b, fade_in_ms, hold_in_ms, fade_out_ms, hold_out_ms, use_cosine_easing,
                                max_brightness, 0);
}

int led_pattern_ease_for(uint8_t r, uint8_t g, uint8_t b, uint32_t fade_in_ms, uint32_t hold_in_ms, uint32_t fade_out_ms,
                         uint32_t hold_out_ms, bool use_cosine_easing, uint8_t max_brightness, uint32_t duration_ms) {
    if (g_ease_layer >= 0) {
        led_layer_destroy(g_ease_layer);
        g_ease_layer = -1;
    }

    g_ease_ctx.r              = r;
    g_ease_ctx.g              = g;
    g_ease_ctx.b              = b;
    g_ease_ctx.max_brightness = max_brightness;
    g_ease_ctx.fade_in_ms     = fade_in_ms;
    g_ease_ctx.hold_in_ms     = hold_in_ms;
    g_ease_ctx.fade_out_ms    = fade_out_ms;
    g_ease_ctx.hold_out_ms    = hold_out_ms;
    g_ease_ctx.use_cosine     = use_cosine_easing;
    g_ease_ctx.phase          = EASE_PHASE_FADE_IN;
    g_ease_ctx.phase_start_ms = esp_timer_get_time() / 1000ULL;

    // ponytail: a timed layer only deactivates (auto_destroy=false) so g_ease_layer stays a valid handle for _stop()
    g_ease_layer = duration_ms
                       ? led_layer_create_timed(LED_PRIORITY_MEDIUM, ease_render, &g_ease_ctx, false, true, duration_ms, false)
                       : led_layer_create(LED_PRIORITY_MEDIUM, ease_render, &g_ease_ctx, true, false, true);
    if (g_ease_layer < 0) {
        ESP_LOGE(TAG, "Failed to create ease LED layer (err=%d)", g_ease_layer);
    }
    return g_ease_layer;
}

void led_pattern_ease_stop() {
    if (g_ease_layer >= 0) {
        led_layer_destroy(g_ease_layer);
        g_ease_layer = -1;
    }
}
