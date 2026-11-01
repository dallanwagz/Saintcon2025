# wled_port: WLED on the badge, as a tool

`wledport.py` maintains `components/wled_fx` (WLED effects and palettes on the badge's 20 LEDs) and the
local preset table that lets the badge copy what a real WLED strip shows. Python 3, standard library
only. Run every command from `Firmware/badge`:

```sh
python3 tools/wled_port/wledport.py <command> --help
```

| command | what it does |
|---|---|
| `import --src <WLED checkout> [--tag vX]` | copy/carve WLED sources into `components/wled_fx` (`upstream/`, `gen/`, `manifest.json`). Re-running it changes nothing. |
| `sweep` | builds `components/wled_fx` for the host with ASan/UBSan, renders every effect × 6 palettes × 2 lengths, and compares the golden frames |
| `snapshot --host <strip-ip> --out wled_snapshot/<name>` | read-only GETs of a strip's info, effects, palettes, presets and cfg; prints version, LED count, fps target, gamma, palette blend, custom palettes, and any fx id/name differences from the badge |
| `presets --snapshot <dir> --menu sdkconfig --out wled_presets.json` | match the HA Menu labels to the strip's presets and write the mirror table |
| `add <file.cpp or URL>[:mode_fn]` | bring in a 1D effect from a newer WLED, a usermod or a community post |
| `remove <name or id>` | take an added effect out again |
| `diff --from A --to B` | effect id/name changes between WLED versions, the badge manifest or a strip |

Household data stays local. `wled_snapshot/` and `wled_presets*.json` are git-ignored, apart from
`wled_presets.example.json`. Never commit a strip's address, preset names or menu.

A new command is just a new `cmd_<name>.py` file next to `wledport.py`, with `HELP`, `add_args(p)` and
`run(args)`.

## Mirror my strip

The badge copies what the strip shows. When you pick a strip look in the HA Menu, the badge renders the
same WLED effect, palette, speeds and colours on its 20 LEDs. A long strip is scaled down rather than
cropped.

1. Put the strip's looks in WLED presets, named exactly like the HA Menu entries (for example a
   `Living room|Toggle|Aurora|Pacifica` row needs presets called `Aurora` and `Pacifica`). Matching
   ignores case, and a preset name cut to 19 characters also matches.
2. `python3 tools/wled_port/wledport.py snapshot --host <strip-ip> --out wled_snapshot/<room>`
   Check the report. `fx id/name diff: none` means the strip and badge use the same effect ids.
3. `python3 tools/wled_port/wledport.py presets --snapshot wled_snapshot/<room> --menu sdkconfig --out wled_presets.json`
   - Labels that match a preset become `preset` entries. The values come from the preset's first
     segment, and `vlen` is the segment's length on the strip.
   - Labels that match no preset follow these rules: `Toggle`, `Off`, `On`, `NN%` (brightness) and
     `<anything> NNNNK` (Solid in that colour temperature, using WLED's `colorKtoRGB`, the same
     Tanner Helland fit Home Assistant uses).
   - Add hand-written entries with `--manual "Good night=off"`. They are saved with `"manual": true`
     and kept every time you regenerate. You can also edit the json directly.
   - The report lists matched, auto, manual and unmatched labels. Unmatched labels are usually
     non-strip rows such as lights or blinds, and the badge ignores them.
4. On the badge, go to Settings → Wrench LEDs and turn on "Mirror the strip from HA Menu".
5. `idf.py build flash`

The table is compiled in from `CONFIG_WLED_PRESETS_FILE` (default `wled_presets.json`; a missing file
gives an empty table). Things that do not carry over:

- Mirror and reverse: mirror is dropped and `vlen` is halved, so the pattern keeps its scale.
- The RGBW white channel: it is added to R, G and B as neutral white.
- Custom palettes: they fall back to palette 0.
- Playlists: not mirrored.
- Preset brightness: the badge uses its own brightness, scaled by `bri` entries.

## Import a newer WLED

1. `git clone --depth 1 --branch <tag> https://github.com/wled/WLED /tmp/WLED`
2. `python3 tools/wled_port/wledport.py diff --to /tmp/WLED --presets wled_presets.json`
   This shows ids that changed, effects that were added or removed, and which presets are affected.
3. `python3 tools/wled_port/wledport.py import --src /tmp/WLED --tag <tag>`
   If carving fails, adjust `CARVE` in `cmd_import.py`. If linking fails, add the function to `CARVE`
   or to `shim/wled_fx_rt.cpp` (see `components/wled_fx/README.md`). Added effects are kept and
   renumbered after the new stock table.
4. `python3 tools/wled_port/wledport.py sweep`. If golden frames changed on purpose, run
   `make -C tools/wled_port/host golden`.
5. Update the tag, commit and date in `components/wled_fx/NOTICE.md`.
6. Re-run `snapshot` and `presets` (see "What if fx ids changed").
7. `idf.py build flash`

## Add a community effect

1. Find the effect: a newer WLED `wled00/FX.cpp`, `usermods/user_fx/user_fx.cpp`, or a forum or
   gist post. It must be 1D. 2D, particle-system and network/clock effects are refused with a reason.
2. Check the licence. The file's header comment is kept. `add` reads the licence from an SPDX line or
   the header words. It refuses when it finds none (pass `--licence <SPDX>`; code from the WLED repo is
   `EUPL-1.2`), when the licence is not EUPL-1.2 or a permissive one (MIT, BSD, Apache-2.0, Zlib, CC0,
   public domain), and when a local file is added without `--source <url/commit>`.
   `--i-know-licence` overrides all three after you have checked it yourself.
3. Run `add`, for example:
   `python3 tools/wled_port/wledport.py add https://raw.githubusercontent.com/wled/WLED/<commit>/usermods/user_fx/user_fx.cpp:mode_dissolveplus --licence EUPL-1.2 --source https://github.com/wled/WLED/blob/<commit>/usermods/user_fx/user_fx.cpp`
   - It carves the function, its `_data_` metadata, its comments and the macros, constants, structs and
     helpers it uses into `components/wled_fx/community/<name>.cpp`.
   - Old-API effects (`uint16_t mode_x()` returning `FRAMETIME`) are rewritten to `void` with a plain
     `return;`. Renamed calls (`random8` → `hw_random8`, ...) are fixed.
   - The effect gets the next id after the stock table (220, 221, ...) and is matched by name
     everywhere else. It is added to `manifest.json`, `community/wled_fx_community.h` and
     `community.cmake`, then compiled and swept with ASan/UBSan.
   - Any failure rolls everything back. Unknown identifiers are listed as "unknown dependencies".
   - Use `--name "New name"` if the name is already taken.
4. If the effect has a licence other than EUPL-1.2, add a line for it to
   `components/wled_fx/NOTICE.md`.
5. `idf.py build flash`. The effect shows up in the LEDs app.

To undo: `python3 tools/wled_port/wledport.py remove "<name>"`.

## What if fx ids changed

The badge stores looks by fx id: the generated preset table, and the look saved from the LEDs app.

1. `python3 tools/wled_port/wledport.py diff --to wled_snapshot/<room>` compares the badge with the
   strip. `diff --from <old checkout> --to <new checkout>` compares two WLED versions. Add
   `--presets wled_presets.json` to see which entries move.
2. Re-run `snapshot` and then `presets`. Strip effects are mapped to badge effects by name, so the
   strip and the badge can use different ids, for example for usermod effects.
3. Fix `"manual": true` entries by hand. The diff lists them.
4. `idf.py build flash`. If the look saved in the LEDs app changed effect, pick it again.
