<!-- SPDX-License-Identifier: EUPL-1.2 -->
# NOTICE: wled_fx

This directory contains code from **WLED** and code derived from it. It is licensed under the
**European Union Public Licence v. 1.2** (EUPL-1.2), the same licence WLED uses; the full text is in
[LICENSE](LICENSE). The rest of the badge firmware, including the glue that drives this component
(`components/wled_mirror`, the LEDs UI), is not derived from WLED and keeps its own licence.

## Upstream

| | |
|---|---|
| Project | WLED, https://github.com/wled/WLED |
| Version | tag `v16.0.1`, commit `29b389df1c1aaec6ff53aea742d17063b985906c` |
| Copyright | (c) 2016-present Christian Schwinne and individual WLED contributors |
| Licence | EUPL v. 1.2 or later |

Effect code (`FX.cpp`, `FX.h`) is "Copyright (c) 2016 Harm Aldick, licensed under the EUPL v. 1.2 or
later, adapted from code originally licensed under the MIT license" (WS2812FX), and was heavily
modified by the WLED contributors.

### MIT-licensed sub-origins

- `upstream/wled00/src/dependencies/fastled_slim/`: a cut-down FastLED ("Copyright (c) 2013 FastLED
  modified by @dedehai"), MIT licence, kept with its own `LICENSE.txt` next to it.
- `colors.h` / `colors.cpp`: some functions derived from FastLED (MIT, see the fastled_slim licence).
- `palettes.cpp`: palettes imported from FastLED 3.6.0 (MIT); several gradient palettes inspired by
  Mark Kriegsman's gists.
- `FX.cpp`: several effects are based on Mark Kriegsman's FastLED demos (Pride 2015, ColorWaves,
  Fire2012, TwinkleFOX, Pacifica with Mary Corey March, BPM, Glitter, ...), as credited in the
  comments above each effect.

## What is here and how it differs from upstream

All of it is produced by `tools/wled_port/wledport.py import --src <WLED checkout> --tag v16.0.1`.

- `upstream/wled00/`: **unmodified** byte-for-byte copies of `FX.cpp`, `FX.h`, `colors.h`,
  `palettes.cpp`, `wled_math.cpp`, `prng.h`, `const.h` and `src/dependencies/fastled_slim/*`.
  Their sha256 sums are in `manifest.json`.
- `LICENSE`: upstream `LICENSE`, unmodified.
- `gen/carved_util.cpp`, `gen/carved_FX_fcn.cpp`: function bodies copied verbatim out of upstream
  `util.cpp` and `FX_fcn.cpp` (selected by name; each block is tagged with its source line range).
  The rest of those files (network, file system, JSON, bus drivers) is not used.
- `gen/colors.cpp`: upstream `colors.cpp` with `loadCustomPalettes()` (LittleFS + ArduinoJson) removed.
- `gen/wled_fx_tables.h`: effect id/name table and palette names, generated from `FX.cpp`, `FX.h` and
  `JSON_palette_names` in `FX_fcn.cpp`.
- `manifest.json`: tag, commit, source hashes, and per-effect id, name, bucket and whether it is
  included in the badge build.
- `shim/`: hand-written replacements for `wled.h`, `fcn_declare.h` (prototypes copied from upstream),
  `fontmanager.h`, `Arduino.h`, `pgmspace.h`, and `wled_fx_rt.cpp`, which re-implements the
  WS2812FX/Segment members tied to busses and transitions (`finalizeInit`, `service`, `show`,
  `startTransition`, ...) for a single 1D segment, modelled on upstream `FX_fcn.cpp`.
- `src/wled_fx_api.cpp`, `include/wled_fx.h`: the badge-facing C API (virtual 42 Hz clock, virtual
  length box-downsampling, gamma on output).

- `community/`: effects added later with `tools/wled_port/wledport.py add` (none at the moment). Each
  file keeps its original header and attribution comments and records its source, licence and date in
  its `// wledport-add:` lines and in `manifest.json` (bucket `COMMUNITY`). `wled_fx_community.h` and
  `community.cmake` there are generated. A file under a licence other than EUPL-1.2 keeps that licence;
  list it here when one is added.

Functional changes compared with stock WLED: single segment only, no transitions, no 2D, no particle
system, no audio usermod (audio effects run on WLED's own `simulateSound()`), no custom palettes,
and one effect (77 Copy Segment) is hidden because it needs a second segment.

## Dates

- 2026-11-01: first import of WLED v16.0.1 (Dallan Wagner, with Claude Code).
- 2026-11-01: community effect registry (`community/`, `src/wled_fx_api.cpp`, `shim/wled_fx_rt.h`) and
  `manifest.json` `mode_count`.
