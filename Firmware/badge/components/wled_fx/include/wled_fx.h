// SPDX-License-Identifier: EUPL-1.2
// wled_fx: WLED effects + palettes rendered as one 1D segment for the badge LEDs.
// The only header badge code includes. Single renderer: wled_fx_init/frame are called from the
// LED render task only; wled_fx_apply may be called from any task.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A WLED segment look (the fields of a WLED JSON "seg" object that matter for rendering).
typedef struct {
  uint8_t  fx, pal, sx, ix, c1, c2, c3;
  bool     o1, o2, o3;
  uint8_t  col[3][3];   // primary, secondary, tertiary RGB
  uint16_t vlen;        // virtual segment length; 0 (or <= physical count) = native, capped at 300
} wled_params_t;

#define WLED_FX_VLEN_MAX 300

void        wled_fx_init(void);
void        wled_fx_apply(const wled_params_t *p);            // takes effect at the next wled_fx_frame
void        wled_fx_defaults(uint8_t fx, wled_params_t *out); // sx/ix/c1-3/o1-3/pal from effect metadata
void        wled_fx_frame(uint32_t now_ms, uint8_t out[][3], uint16_t n); // gamma applied, not brightness scaled
uint16_t    wled_fx_count(void);
uint8_t     wled_fx_id_at(uint16_t i);
const char *wled_fx_name(uint8_t fx);
const char *wled_fx_meta(uint8_t fx);
int         wled_fx_find(const char *name);
uint8_t     wled_pal_count(void);
const char *wled_pal_name(uint8_t pal);
int         wled_pal_find(const char *name);

#ifdef __cplusplus
}
#endif
