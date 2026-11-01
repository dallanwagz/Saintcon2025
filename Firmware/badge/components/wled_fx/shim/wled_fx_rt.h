// SPDX-License-Identifier: EUPL-1.2
// Glue between the shim runtime (wled_fx_rt.cpp) and the public API (src/wled_fx_api.cpp).
#pragma once
#include <stdint.h>

extern uint32_t wled_fx_clock_ms;  // what millis() returns; wled_fx_frame sets it per sub-step
extern uint16_t wled_fx_rt_len;    // strip/segment length used by WS2812FX::finalizeInit()
#ifndef ESP_PLATFORM
void wled_fx_rt_seed(uint32_t s);  // host only: reseed the deterministic RNG (golden frames)
#endif

// Registration record a community/*.cpp effect exports (see tools/wled_port/cmd_add.py); wled_fx_init()
// hands it to strip.addEffect() under the id listed in community/wled_fx_community.h.
typedef struct { void (*fn)(void); const char *data; } wled_fx_comm_t;
