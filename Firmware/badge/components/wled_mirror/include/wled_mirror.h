// wled_mirror: the wrench LED wallpaper. One LOW priority LED layer renders a WLED look (wled_fx): the manual
// look picked in the LEDs app (saved in badge_config), or, while Settings -> Wrench LEDs mirroring is on, the look
// an HA Menu pick maps to in the local preset table (CONFIG_WLED_PRESETS_FILE, generated at build time).
// All functions are for the UI/main task; the layer itself renders in the LED task.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "badge.h"
#include "wled_fx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Preset table generated from CONFIG_WLED_PRESETS_FILE by tools/wled_port/gen_presets_c.py
typedef enum { WM_PRESET, WM_TOGGLE, WM_OFF, WM_ON, WM_BRI } wm_action_t;
typedef struct {
    const char *menu;      // "<row label>/<entry label>" or "<row label>", exactly as HA Menu shows it
    wm_action_t action;
    uint8_t value;         // WM_BRI: mirror brightness percent (0-100)
    wled_params_t params;  // WM_PRESET
} wm_entry_t;
extern const wm_entry_t wm_entries[];
extern const int wm_entry_count;

void wled_mirror_init(void);                          // creates the layer, shows the saved manual look
void wled_mirror_set_manual(const wled_params_t *p);  // LEDs app pick (NULL = LEDs off); saved; clears any mirror override
void wled_mirror_preview(const wled_params_t *p);     // same, but only in RAM: the caller saves badge_config later
bool wled_mirror_pick(const char *row_label, const char *entry_label); // HA Menu hook; true if a table entry matched
void wled_mirror_clear(void);                         // drop the mirror override, back to the manual look
bool wled_mirror_get_manual(wled_params_t *out);      // the saved manual look; returns whether it is on

// badge_config look <-> wled_fx params
void wled_mirror_from_look(const badge_wled_look_t *l, wled_params_t *p);
void wled_mirror_to_look(const wled_params_t *p, badge_wled_look_t *l);

#ifdef __cplusplus
}
#endif
