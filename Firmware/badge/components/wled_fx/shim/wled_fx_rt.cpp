// SPDX-License-Identifier: EUPL-1.2
// wled_fx runtime: the hand-written half of the shim.
// Implements the WS2812FX/Segment members that upstream ties to busses, JSON, FS and transitions,
// for a single 1D segment rendered into a uint32_t[len] frame buffer. Modelled on WLED v16.0.1
// FX_fcn.cpp (finalizeInit/service/show), cut down to one segment.
#ifdef ESP_PLATFORM
#include "esp_random.h"   // before wled.h: the Arduino shim defines short macros (F, PSTR, ...)
#endif
#include "wled.h"
#include "wled_fx_rt.h"

// ---------------- globals upstream defines with WLED_GLOBAL in wled.h
bool     gammaCorrectCol          = true;   // WLED default: gamma on colors
bool     gammaCorrectBri          = false;
float    gammaCorrectVal          = 2.2f;   // WLED default (cfg "gc.val")
byte     paletteBlend             = 0;
uint8_t  blendingStyle            = TRANSITION_FADE;
byte     realtimeMode             = REALTIME_MODE_INACTIVE;
bool     realtimeRespectLedMaps   = true;
byte     errorFlag                = 0;
bool     stateChanged             = false;
uint8_t  randomPaletteChangeTime  = 5;      // WLED default (seconds)
bool     useHarmonicRandomPalette = true;   // WLED default
byte     lastRandomIndex          = 0;
std::vector<CRGBPalette16>  customPalettes;
std::vector<UsermodPalette> usermodPalettes;
SerialStub Serial;

WS2812FX strip;  // ctor runs setupEffectData() -> registers every compiled effect

// ---------------- clock + RNG
uint32_t wled_fx_clock_ms = 0;      // set by wled_fx_frame() per virtual 42 Hz sub-step
uint16_t wled_fx_rt_len = 20;
uint32_t millis() { return wled_fx_clock_ms; }
uint32_t micros() { return wled_fx_clock_ms * 1000u; }

#ifdef ESP_PLATFORM
uint32_t wled_core_hw_rnd() { return esp_random(); }
#else
static uint32_t s_rng = 0x12345678u;  // deterministic on host so frames are reproducible
uint32_t wled_core_hw_rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
void wled_fx_rt_seed(uint32_t s) { s_rng = s ? s : 0x12345678u; }
#endif

// ponytail: plain malloc for every BFRALLOC_* flavour; buffers are a few hundred bytes for <= 300 LEDs
void *allocate_buffer(size_t size, uint32_t type) {
  void *p = malloc(size ? size : 1);
  if (p && (type & BFRALLOC_CLEAR)) memset(p, 0, size);
  return p;
}

// ---------------- Segment pieces we deliberately replace
// Transitions: wled_fx never cross-fades effects (the badge snaps to the new look; the old
// segment copy alone would double RAM and CPU). Keeping _t == nullptr makes every
// isInTransition()/progress() path in the carved code collapse to "no transition".
void Segment::startTransition(uint16_t, bool) {}
void Segment::refreshLightCapabilities() const { _capabilities = SEG_CAPABILITY_RGB; }
Segment &Segment::setCCT(uint16_t k) { cct = k > 255 ? 127 : k; return *this; }
Segment &Segment::setOpacity(uint8_t o) { opacity = o; return *this; }
Segment &Segment::setName(const char *) { return *this; }

// ---------------- WS2812FX pieces
// (Re)build the strip as one segment of wled_fx_rt_len LEDs. Called again whenever vlen changes.
void WS2812FX::finalizeInit() {
  _length = wled_fx_rt_len;
  Segment::maxWidth = _length;
  Segment::maxHeight = 1;
  _segments.clear();
  _segments.emplace_back(0, _length);
  _segments[0].refreshLightCapabilities();
  _segments[0].setMode(DEFAULT_MODE, true);
  _segments[0].colors[0] = DEFAULT_COLOR;
  _mainSegment = 0;
  _currentSegment = &_segments[0];
  p_free(_pixels);
  _pixels = static_cast<uint32_t *>(allocate_buffer(_length * sizeof(uint32_t), BFRALLOC_CLEAR));
  NeoGammaWLEDMethod::calcGammaTable(gammaCorrectVal);
}

void WS2812FX::setTargetFps(unsigned fps) {
  if (fps <= 250) _targetFps = fps;
  if (_targetFps > 0) _frametime = 1000 / _targetFps;
  else _frametime = MIN_FRAME_DELAY;
}

void WS2812FX::setBrightness(uint8_t b, bool) { _brightness = b; }

// One frame. Mirrors upstream service() minus multi-segment, freeze, transitions and old-mode blending.
void WS2812FX::service() {
  now = millis() + timebase;
  _isServicing = true;
  Segment &seg = _segments[0];
  _segment_index = 0;
  seg.resetIfRequired();
  if (seg.isActive() && !seg.freeze) {
    seg.beginDraw();
    _currentSegment = &seg;
    _mode[seg.mode]();
    seg.call++;
  }
  Segment::handleRandomPalette();
  show();
  _isServicing = false;
}

// Upstream blendSegment()+show(): segment buffer -> strip buffer applying reverse/mirror/grouping/
// spacing/offset and segment opacity. Gamma is applied by wled_fx_frame(), brightness by the badge.
void WS2812FX::show() {
  const Segment &s = _segments[0];
  const int len = s.length();
  const int vlen = s.virtualLength();
  const int grp = s.groupLength();
  for (int i = 0; i < _length; i++) _pixels[i] = BLACK;
  if (!s.on) return;
  uint8_t op = s.opacity;
  if (gammaCorrectCol) op = gamma8inv(op);
  for (int v = 0; v < vlen; v++) {
    uint32_t c = s.getPixelColorRaw(v);
    if (op < 255) c = color_fade(c, op);
    for (int g = 0; g < s.grouping; g++) {
      int p = v * grp + g;
      if (p >= len) break;
      int phys = s.reverse ? (s.mirror ? (len / 2 - 1 - p) : (len - 1 - p)) : p;
      if (s.offset) phys = (phys + s.offset) % len;
      _pixels[s.start + phys] = c;
      if (s.mirror) _pixels[s.start + len - 1 - phys] = c;
    }
  }
}
