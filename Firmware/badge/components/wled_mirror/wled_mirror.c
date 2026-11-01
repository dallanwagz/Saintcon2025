// wled_mirror: the wrench LED wallpaper (see wled_mirror.h).
// State lives in the UI/main task; the LED task's render callback only reads s_dark and s_pct (single bytes)
// and calls wled_fx_frame, whose params arrive through the thread-safe wled_fx_apply.
#include "wled_mirror.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "led.h"

static const char *TAG = "wled_mirror";

typedef enum {
    OVR_NONE,   // manual look (dark if badge_config.wled_on == 0)
    OVR_PRESET, // last mirrored preset
    OVR_MANUAL, // manual look forced on (HA "on"/"toggle" before any preset was mirrored)
    OVR_OFF,    // mirrored "off": dark
} ovr_t;

static int s_layer = -1;
static ovr_t s_ovr = OVR_NONE;
static wled_params_t s_mirror;         // last mirrored preset
static bool s_have_mirror;
static uint8_t s_mirror_pct = 100;     // HA "bri" entries; scales mirrored output only
static volatile bool s_dark = true;    // read by the LED task
static volatile uint8_t s_pct = 100;   // read by the LED task

void wled_mirror_from_look(const badge_wled_look_t *l, wled_params_t *p) {
    *p = (wled_params_t){.fx = l->fx, .pal = l->pal, .sx = l->sx, .ix = l->ix, .c1 = l->c1, .c2 = l->c2, .c3 = l->c3,
                         .o1 = l->opts & 1, .o2 = (l->opts >> 1) & 1, .o3 = (l->opts >> 2) & 1, .vlen = l->vlen};
    memcpy(p->col, l->col, sizeof(p->col));
}

void wled_mirror_to_look(const wled_params_t *p, badge_wled_look_t *l) {
    *l = (badge_wled_look_t){.fx = p->fx, .pal = p->pal, .sx = p->sx, .ix = p->ix, .c1 = p->c1, .c2 = p->c2, .c3 = p->c3,
                             .opts = (p->o1 ? 1 : 0) | (p->o2 ? 2 : 0) | (p->o3 ? 4 : 0), .vlen = p->vlen};
    memcpy(l->col, p->col, sizeof(l->col));
}

static void render(uint32_t now_ms, void *user_ctx) {
    (void)user_ctx;
    if (s_dark) {
        return; // clear_before: the layer shows black and still hides nothing above it
    }
#ifdef CONFIG_WLED_MIRROR_STATS
    int64_t t0 = esp_timer_get_time();
#endif
    uint8_t px[NUM_LEDS][3];
    wled_fx_frame(now_ms, px, NUM_LEDS);
    const uint32_t pct = s_pct;
    for (int i = 0; i < NUM_LEDS; i++) {
        led_set_scaled(i, px[i][0] * pct / 100, px[i][1] * pct / 100, px[i][2] * pct / 100);
    }
#ifdef CONFIG_WLED_MIRROR_STATS
    static uint32_t max_us, next_ms;
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    max_us      = us > max_us ? us : max_us;
    if ((int32_t)(now_ms - next_ms) >= 0) {
        ESP_LOGI(TAG, "max frame %lu us, led task stack free %u B", (unsigned long)max_us,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        max_us  = 0;
        next_ms = now_ms + 10000;
    }
#endif
}

// Push the current state to the layer
static void show(void) {
    wled_params_t p;
    switch (s_ovr) {
        case OVR_PRESET:
            wled_fx_apply(&s_mirror);
            s_pct  = s_mirror_pct;
            s_dark = false;
            break;
        case OVR_OFF: s_dark = true; break;
        default: // OVR_NONE, OVR_MANUAL
            wled_mirror_from_look(&badge_config.wled, &p);
            wled_fx_apply(&p);
            s_pct  = 100;
            s_dark = s_ovr == OVR_NONE && !badge_config.wled_on;
            break;
    }
}

void wled_mirror_init(void) {
    wled_fx_init();
    show();
    if (s_layer < 0) {
        s_layer = led_layer_create(LED_PRIORITY_LOW, render, NULL, true, false, true);
        if (s_layer < 0) {
            ESP_LOGE(TAG, "Failed to create the wallpaper LED layer (err=%d)", s_layer);
        }
    }
    ESP_LOGI(TAG, "%d preset table entries, mirror %s", wm_entry_count, badge_config.led_mirror ? "on" : "off");
}

void wled_mirror_preview(const wled_params_t *p) {
    if (p) {
        wled_mirror_to_look(p, &badge_config.wled);
    }
    badge_config.wled_on = p != NULL;
    s_ovr                = OVR_NONE;
    show();
}

void wled_mirror_set_manual(const wled_params_t *p) {
    wled_mirror_preview(p);
    save_badge_config();
}

bool wled_mirror_get_manual(wled_params_t *out) {
    if (out) {
        wled_mirror_from_look(&badge_config.wled, out);
    }
    return badge_config.wled_on;
}

void wled_mirror_clear(void) {
    s_ovr = OVR_NONE;
    show();
}

bool wled_mirror_pick(const char *row_label, const char *entry_label) {
    if (!badge_config.led_mirror || !row_label) {
        return false;
    }
    char key[64];
    if (entry_label) {
        snprintf(key, sizeof(key), "%s/%s", row_label, entry_label);
    } else {
        snprintf(key, sizeof(key), "%s", row_label);
    }
    const wm_entry_t *e = NULL;
    for (int i = 0; i < wm_entry_count && !e; i++) {
        if (!strcmp(wm_entries[i].menu, key)) {
            e = &wm_entries[i];
        }
    }
    if (!e) {
        return false;
    }
    const bool lit = s_ovr == OVR_PRESET || s_ovr == OVR_MANUAL || (s_ovr == OVR_NONE && badge_config.wled_on);
    switch (e->action) {
        case WM_PRESET:
            s_mirror      = e->params;
            s_have_mirror = true;
            s_ovr         = OVR_PRESET;
            break;
        case WM_TOGGLE:
            s_ovr = lit ? OVR_OFF : s_have_mirror ? OVR_PRESET : OVR_MANUAL;
            break;
        case WM_ON: s_ovr = s_have_mirror ? OVR_PRESET : OVR_MANUAL; break;
        case WM_OFF: s_ovr = OVR_OFF; break;
        case WM_BRI: s_mirror_pct = e->value > 100 ? 100 : e->value; break;
    }
    ESP_LOGI(TAG, "mirror '%s' -> %d", key, (int)s_ovr);
    show();
    return true;
}
