<!-- SPDX-License-Identifier: EUPL-1.2 -->
# wled_fx

WLED v16.0.1 effects and palettes running on the badge's 20 LEDs, as one 1D segment.
146 effects (122 plain 1D effects plus 24 audio effects driven by WLED's built-in sound simulation)
and 72 palettes. Licence: EUPL-1.2, see [NOTICE.md](NOTICE.md).

## How it works

`FX.cpp` and the other files in `upstream/` compile **unmodified**. They `#include "wled.h"`, and
`shim/wled.h` provides only what effect code needs (Arduino basics, globals, `strip`), with no network,
file system or bus drivers. The `Segment`/`WS2812FX` methods the effects call come from upstream
`FX_fcn.cpp`/`util.cpp`, carved out function by function into `gen/`. A small hand-written runtime
(`shim/wled_fx_rt.cpp`) replaces the parts tied to busses and transitions.

```
LED task ──wled_fx_frame(now, out, 20)──► virtual 42 Hz clock ─► strip.service() x1..3
                                           │                          (effect writes segment)
UI task ──wled_fx_apply(&params)──► double buffer (portMUX), picked up at the next frame
                                           ▼
                        strip pixels (max(20, vlen)) ─► gamma32 ─► box average ─► out[20][3]
```

- **Timing.** WLED effects expect 42 FPS, and some advance a step per call. The badge renders every
  40 ms, so each `wled_fx_frame()` runs every 42 Hz step that came due (1 or 2, catch-up capped at 3),
  setting `millis()`/`strip.now` to each step's time, and outputs the last result.
- **Virtual length.** `params.vlen` (for example the LED count of a real WLED strip) makes the segment
  that long, up to 300. The result is box-averaged down to the physical pixels after gamma, so a
  preset copied from a long strip looks the same, only coarser.
- **Colour.** WLED's `gamma32` (2.2) is applied on output. Brightness is not; the badge scales the
  result (`led_set_scaled`).
- **Threads.** Only the LED render task calls `wled_fx_init`/`wled_fx_frame`. `wled_fx_apply` may be
  called from any task.
- **Effects** are identified by the stock WLED `FX_MODE_*` id (built with `WLED_PS_DONT_REPLACE_FX`) and
  by name. `wled_fx_count()`/`wled_fx_id_at()` list them; `wled_fx_defaults()` gives WLED's
  per-effect slider defaults (what `setMode(fx, true)` loads).

Build switches (set in `CMakeLists.txt`, mirrored in `tools/wled_port/host/Makefile`):
`WLED_DISABLE_2D WLED_DISABLE_PARTICLESYSTEM2D WLED_DISABLE_PARTICLESYSTEM1D WLED_PS_DONT_REPLACE_FX
BOARD_HAS_PSRAM`. Particle system 1D stays off because upstream fx 204 has an out-of-bounds write.

Cost on the ESP32-S3 (measured with everything linked): about 74 KB in `libwled_fx.a` (64 KB code,
10 KB rodata, under 1 KB static RAM), about 80 KB of total flash including pulled-in libstdc++. Heap
use is about 2 KB for the effect registry plus 4 bytes per virtual LED, plus effect data.

## Re-importing / updating WLED

```sh
git clone --depth 1 --branch v16.0.1 https://github.com/wled/WLED /tmp/WLED
python3 tools/wled_port/wledport.py import --src /tmp/WLED --tag v16.0.1
python3 tools/wled_port/wledport.py sweep      # host build with ASan/UBSan, every effect, golden frames
idf.py build
```

`import` rewrites `upstream/`, `gen/`, `LICENSE` and `manifest.json` and nothing else. Running it twice
gives no diff. For a newer tag, if carving fails ("not found"), adjust the `CARVE` lists in
`tools/wled_port/cmd_import.py`; if linking fails, add the missing function to `CARVE` or implement it
in `shim/wled_fx_rt.cpp`. Never edit `upstream/` or `gen/` by hand. If the golden frames change
on purpose, run `make -C tools/wled_port/host golden`, then update the date list in `NOTICE.md`.

## Community effects

Effects that are not in the imported WLED tag (a newer WLED, `usermods/user_fx`, forum posts) are added
with `tools/wled_port/wledport.py add`. They go into `community/`, get the ids after the stock
table (`MODE_COUNT`, 220 for v16.0.1), and are registered by `wled_fx_init()` through the generated
`community/wled_fx_community.h`. See `tools/wled_port/README.md`.

## Known upstream quirks (v16.0.1)

- `Segment::loadPalette()` copies 72 bytes from gradient palettes that are shorter. It is a read-only
  over-read of flash and harmless on the ESP32. The host ASan build copies those bytes without
  instrumentation (`memcpy_P` in `shim/Arduino.h`).
- Some effects shift negative ints left or cast negative floats to unsigned (Starburst, PacMan, audio
  effects). Stock WLED does the same on ESP32. Those two UBSan checks are off in the host sweep.
