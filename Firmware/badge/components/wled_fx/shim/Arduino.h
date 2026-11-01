// SPDX-License-Identifier: EUPL-1.2
// wled_fx shim: the minimal Arduino API surface WLED effect code needs.
// Same file for the badge (ESP-IDF) and the host harness; millis() is the wled_fx virtual clock.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <cmath>
#include <string>

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int uint;

// ---- PROGMEM: flash is memory-mapped on ESP32, so all of these are plain accesses
#define PROGMEM
#define PGM_P const char *
#define PSTR(s) (s)
#define F(s) (s)
#define FPSTR(s) (s)
#define pgm_read_byte(p)  (*(const uint8_t *)(p))
#define pgm_read_word(p)  (*(const uint16_t *)(p))
// pgm_read_dword: WLED reads tables of *pointers* with it (FX_fcn.cpp loadPalette, gGradientPalettes).
// Fine on 32-bit ESP32; on a 64-bit host that truncates, so return the pointee type when it is a pointer.
#include <type_traits>
template <typename T> inline auto pgm_read_dword_impl(const T *p) {
  if constexpr (std::is_pointer<T>::value) return *p; else return *(const uint32_t *)p;
}
#define pgm_read_dword(p) pgm_read_dword_impl(p)
#define pgm_read_ptr(p)   (*(void * const *)(p))
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define WLED_FX_ASAN 1
#endif
#endif
#ifdef WLED_FX_ASAN
// Host ASan build only: upstream Segment::loadPalette() memcpy_P's a fixed 72 bytes out of gradient
// palettes that are shorter (WLED v16.0.1 bug; read-only, harmless in ESP32 flash). Copy without
// instrumentation so the sweep catches everything else.
__attribute__((no_sanitize("address"))) inline void *memcpy_P(void *d, const void *s, size_t n) {
  volatile uint8_t *o = (volatile uint8_t *)d; const uint8_t *i = (const uint8_t *)s;
  while (n--) *o++ = *i++;
  return d;
}
#else
#define memcpy_P memcpy
#endif
#define strncmp_P strncmp
#define strcmp_P strcmp
#define strlen_P strlen
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strstr_P strstr
#define strchr_P strchr
#define sprintf_P sprintf
#define snprintf_P snprintf
#define strlcpy_P strlcpy

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)
#define sq(x) ((x)*(x))
#ifndef M_TWOPI
#define M_TWOPI 6.283185307179586476925286766559
#endif
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

// arduino-esp32 exposes std::min/std::max/std::abs (type-strict), match that.
using std::min;
using std::max;
using std::abs;
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  const long run = in_max - in_min;
  if (run == 0) return out_min;
  return (x - in_min) * (out_max - out_min) / run + out_min;
}

// ---- time: one render clock for the whole engine (host: simulated; badge: esp_timer)
uint32_t millis();
uint32_t micros();
inline void yield() {}
inline void delay(uint32_t) {}

// ---- tiny String (only what FX.h / FX.cpp touch)
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  unsigned length() const { return (unsigned)size(); }
};

// ---- Serial stub (debug output only)
struct SerialStub {
  template <typename T> void print(const T &) {}
  template <typename T> void println(const T &) {}
  void println() {}
  int printf(const char *, ...) { return 0; }
};
extern SerialStub Serial;

#ifndef __APPLE__
size_t strlcpy(char *dst, const char *src, size_t size);
#endif
