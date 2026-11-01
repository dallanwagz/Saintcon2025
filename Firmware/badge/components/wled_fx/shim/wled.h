// SPDX-License-Identifier: EUPL-1.2
// wled_fx shim replacement for wled00/wled.h.
// Presents exactly the globals and types that effect code reaches into; nothing network/FS/JSON related.
#pragma once

#ifndef WLED_DISABLE_2D
#define WLED_DISABLE_2D            // badge is a 20-LED 1D strip
#endif
#define WLED_DISABLE_HUESYNC
#define WLED_DISABLE_MQTT
#define WLED_DISABLE_ESPNOW
#ifndef WLED_DISABLE_PARTICLESYSTEM1D // PS1D stays off: fx 204 has an upstream OOB write
#define WLED_DISABLE_PARTICLESYSTEM1D
#endif
#ifndef WLED_DISABLE_PARTICLESYSTEM2D
#define WLED_DISABLE_PARTICLESYSTEM2D
#endif
#ifndef WLED_PS_DONT_REPLACE_FX
#define WLED_PS_DONT_REPLACE_FX     // keep the stock (non particle) fx id table
#endif
#define LEDC_CHANNEL_MAX 8          // keeps const.h from including driver/ledc.h
#define LEDC_SPEED_MODE_MAX 1
#ifndef CONFIG_IDF_TARGET_ESP32S3
#define CONFIG_IDF_TARGET_ESP32S3 1 // host: select the same S3 branches in const.h/FX.h as the badge
#endif
#ifndef BOARD_HAS_PSRAM
#define BOARD_HAS_PSRAM             // matches badge (PSRAM enabled); lifts MAX_SEGMENT_DATA cap
#endif

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(x...)
#define DEBUG_PRINTF_P(x...)

#include "Arduino.h"
#include "const.h"
#include "fcn_declare.h"

// BusManager is referenced from an inline in FX.h (WS2812FX::isUpdating); the badge LED driver lives outside.
struct BusManager { static bool canAllShow() { return true; } };

// ---- globals that effect / segment code reads (declared in wled.h upstream with WLED_GLOBAL)
extern bool     gammaCorrectCol;          // apply gamma to colors in show()
extern bool     gammaCorrectBri;
extern float    gammaCorrectVal;
extern byte     paletteBlend;             // 0 wrap when moving, 1 always wrap, 2 never wrap, 3 none
extern uint8_t  blendingStyle;            // transition style (we only use TRANSITION_FADE / none)
extern byte     realtimeMode;
extern bool     realtimeRespectLedMaps;
extern byte     errorFlag;
extern bool     stateChanged;
extern uint8_t  randomPaletteChangeTime;  // seconds
extern bool     useHarmonicRandomPalette;
extern byte     lastRandomIndex;          // colors.cpp setRandomColor()

#include "colors.h"
#include "FX.h"

extern WS2812FX strip;
