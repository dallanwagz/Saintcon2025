// SPDX-License-Identifier: EUPL-1.2
// wled_fx shim replacement for wled00/fcn_declare.h.
// Only the declarations that effect code (FX.cpp), colors.cpp, palettes.cpp and wled_math.cpp reach.
// Prototypes copied verbatim from WLED v16.0.1 fcn_declare.h (lines noted) so carved bodies link unchanged.
#pragma once
#include "Arduino.h"

// ---- usermod data exchange (fcn_declare.h:312-344), used by audio-reactive effects via getAudioData()
typedef enum UM_Data_Types {
  UMT_BYTE = 0, UMT_UINT16, UMT_INT16, UMT_UINT32, UMT_INT32, UMT_FLOAT, UMT_DOUBLE,
  UMT_BYTE_ARR, UMT_UINT16_ARR, UMT_INT16_ARR, UMT_UINT32_ARR, UMT_INT32_ARR, UMT_FLOAT_ARR, UMT_DOUBLE_ARR
} um_types_t;
typedef struct UM_Exchange_Data {
  size_t       u_size;
  um_types_t  *u_type;
  void       **u_data;
  UM_Exchange_Data() { u_size = 0; u_type = nullptr; u_data = nullptr; }
  ~UM_Exchange_Data() { if (u_type) delete[] u_type; if (u_data) delete[] u_data; }
} um_data_t;

// No usermods on the badge: getUMData always fails, so effects fall back to simulateSound().
class UsermodManager {
 public:
  static bool getUMData(um_data_t **um_data, uint8_t mod_id = 0) { if (um_data) *um_data = nullptr; return false; }
};

// ---- util.cpp (fcn_declare.h:451-493)
int16_t extractModeDefaults(uint8_t mode, const char *segVar);
uint16_t beat88(uint16_t beats_per_minute_88, uint32_t timebase = 0);
uint16_t beat16(uint16_t beats_per_minute, uint32_t timebase = 0);
uint8_t beat8(uint16_t beats_per_minute, uint32_t timebase = 0);
uint16_t beatsin88_t(uint16_t beats_per_minute_88, uint16_t lowest = 0, uint16_t highest = 65535, uint32_t timebase = 0, uint16_t phase_offset = 0);
uint16_t beatsin16_t(uint16_t beats_per_minute, uint16_t lowest = 0, uint16_t highest = 65535, uint32_t timebase = 0, uint16_t phase_offset = 0);
uint8_t beatsin8_t(uint16_t beats_per_minute, uint8_t lowest = 0, uint8_t highest = 255, uint32_t timebase = 0, uint8_t phase_offset = 0);
um_data_t* simulateSound(uint8_t simulationId);
[[gnu::hot]] uint8_t get_random_wheel_index(uint8_t pos);
float mapf(float x, float in_min, float in_max, float out_min, float out_max);
uint32_t hashInt(uint32_t s);
int32_t perlin1D_raw(uint32_t x, bool is16bit = false);
int32_t perlin2D_raw(uint32_t x, uint32_t y, bool is16bit = false);
int32_t perlin3D_raw(uint32_t x, uint32_t y, uint32_t z, bool is16bit = false);
uint16_t perlin16(uint32_t x);
uint16_t perlin16(uint32_t x, uint32_t y);
uint16_t perlin16(uint32_t x, uint32_t y, uint32_t z);
uint8_t perlin8(uint16_t x);
uint8_t perlin8(uint16_t x, uint16_t y);
uint8_t perlin8(uint16_t x, uint16_t y, uint16_t z);
#define inoise8 perlin8   // fastled legacy alias
#define inoise16 perlin16 // fastled legacy alias

// ---- hardware RNG (fcn_declare.h:420-493). Badge: esp_random() reads the same WDEV_RND_REG.
uint32_t wled_core_hw_rnd();
#define HW_RND_REGISTER wled_core_hw_rnd()
#define random hw_random // replace arduino random()
inline uint32_t hw_random() { return HW_RND_REGISTER; };
uint32_t hw_random(uint32_t upperlimit);
int32_t hw_random(int32_t lowerlimit, int32_t upperlimit);
inline uint16_t hw_random16() { return HW_RND_REGISTER; };
inline uint16_t hw_random16(uint32_t upperlimit) { return (hw_random16() * upperlimit) >> 16; };
inline int16_t hw_random16(int32_t lowerlimit, int32_t upperlimit) { int32_t range = upperlimit - lowerlimit; return lowerlimit + hw_random16(range); };
inline uint8_t hw_random8() { return HW_RND_REGISTER; };
inline uint8_t hw_random8(uint32_t upperlimit) { return (hw_random8() * upperlimit) >> 8; };
inline uint8_t hw_random8(uint32_t lowerlimit, uint32_t upperlimit) { uint32_t range = upperlimit - lowerlimit; return lowerlimit + hw_random8(range); };

// ---- memory (fcn_declare.h:494-532): plain heap on host; heap_caps on badge
#define BFRALLOC_NOBYTEACCESS    (1 << 0)
#define BFRALLOC_PREFER_DRAM     (1 << 1)
#define BFRALLOC_ENFORCE_DRAM    (1 << 2)
#define BFRALLOC_PREFER_PSRAM    (1 << 3)
#define BFRALLOC_ENFORCE_PSRAM   (1 << 4)
#define BFRALLOC_CLEAR           (1 << 5)
void *allocate_buffer(size_t size, uint32_t type);
inline void d_free(void *ptr) { free(ptr); }
inline void p_free(void *ptr) { free(ptr); }
inline void *d_malloc(size_t s) { return malloc(s); }
inline void *p_malloc(size_t s) { return malloc(s); }
inline void *d_calloc(size_t n, size_t s) { return calloc(n, s); }
inline void *p_calloc(size_t n, size_t s) { return calloc(n, s); }

// ---- wled_math.cpp (fcn_declare.h:555-583)
#define sin_t sin_approx
#define cos_t cos_approx
#define tan_t tan_approx
int16_t sin16_t(uint16_t theta);
int16_t cos16_t(uint16_t theta);
uint8_t sin8_t(uint8_t theta);
uint8_t cos8_t(uint8_t theta);
float sin_approx(float theta);
float cos_approx(float theta);
float tan_approx(float x);
float atan2_t(float y, float x);
float acos_t(float x);
float asin_t(float x);
template <typename T> T atan_t(T x);
float floor_t(float x);
float fmod_t(float num, float denom);
uint32_t sqrt32_bw(uint32_t x);
