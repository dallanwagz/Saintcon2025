"""wledport snapshot - save a WLED strip's state/config (read-only HTTP GETs) and report what matters to the badge.

Fetches /json/info, /json/eff, /json/fxdata, /json/pal, /json/state, /presets.json, /cfg.json and custom
palettes /paletteN.json into --out (raw bytes, one file each). Endpoints that fail (404, auth) are noted
and skipped. Without --host it only re-prints the report of an existing snapshot dir.
The output dir holds household data: keep it under wled_snapshot/ (git-ignored).
"""
import datetime
import json
import os
import urllib.error
import urllib.request

from wp_common import load_manifest, require_git_ignored

HELP = "save a WLED strip's info/effects/presets/config (GET only) and report fx id/name diffs vs the badge"

GAMMA_NOTE = "gamma 2.2 on colour, none on brightness"
FILES = {"info.json": "/json/info", "eff.json": "/json/eff", "fxdata.json": "/json/fxdata",
         "pal.json": "/json/pal", "state.json": "/json/state", "presets.json": "/presets.json",
         "cfg.json": "/cfg.json"}


def add_args(p):
    p.add_argument("--host", help="strip address, e.g. <strip-ip> or http://wled.local (omit to only report)")
    p.add_argument("--out", required=True, help="snapshot dir, e.g. wled_snapshot/<name> (git-ignored)")
    p.add_argument("--timeout", type=float, default=8.0)


def get(base, path, timeout):
    req = urllib.request.Request(base + path, method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def load(d, name, default=None):
    p = os.path.join(d, name)
    if not os.path.exists(p):
        return default
    with open(p, encoding="utf-8") as f:
        try:
            return json.load(f)
        except ValueError:
            return default


def report(d):
    info, eff, cfg = load(d, "info.json", {}), load(d, "eff.json", []), load(d, "cfg.json", {})
    meta = load(d, "snapshot.json", {})
    leds = info.get("leds", {})
    light = cfg.get("light", {})
    gc = light.get("gc", {})
    print(f"snapshot {d}  ({meta.get('host', '?')}, {meta.get('snapshot', '?')})")
    print(f"  WLED {info.get('ver', '?')} (vid {info.get('vid', '?')}, {info.get('release', '?')}, repo {info.get('repo', '?')})")
    print(f"  LEDs {leds.get('count', '?')}  rgbw {leds.get('rgbw', '?')}  cct {leds.get('cct', '?')}  "
          f"segments {len(load(d, 'state.json', {}).get('seg', []))}")
    print(f"  fps target {cfg.get('hw', {}).get('led', {}).get('fps', '?')} (cfg hw.led.fps; measured now {leds.get('fps', '?')})")
    if cfg:
        onoff = lambda v: "?" if v is None else ("on" if float(v) > 1.0 else "off")  # WLED: on when > 1.0
        print(f"  gamma: colour {onoff(gc.get('col'))} brightness {onoff(gc.get('bri'))} value {gc.get('val', '?')}"
              f"  (badge: {GAMMA_NOTE})")
        print(f"  paletteBlend (light.pal-mode) {light.get('pal-mode', '?')}  (badge: 0)")
    else:
        print("  cfg.json not available: gamma/paletteBlend/fps target unknown")
    cp = sorted(f for f in os.listdir(d) if f.startswith("palette") and f.endswith(".json"))
    print(f"  custom palettes: {info.get('cpalcount', '?')} ({', '.join(cp) if cp else 'none saved'})"
          + ("  (badge has none: presets using them fall back to palette 0)" if cp else ""))
    for k in meta.get("failed", []):
        print(f"  not fetched: {k}")

    man = load_manifest()
    badge = {e["id"]: e for e in man["effects"]}
    names = {e["name"].lower(): e for e in man["effects"]}
    print(f"  effects: strip {len(eff)} (fxcount {info.get('fxcount', '?')}), badge manifest {len(badge)} "
          f"(WLED {man['wled']['tag']}, {man['counts']['included']} on the badge)")
    diff = 0
    for i, n in enumerate(eff):
        if n == "RSVD" or not n:
            continue
        b = badge.get(i)
        if b is None or b["name"] != n:
            other = names.get(n.lower())
            where = f", badge has '{n}' as fx {other['id']}" if other else ", not on the badge"
            print(f"  ! fx {i}: strip '{n}' vs badge '{b['name'] if b else '-'}'{where}")
            diff += 1
    for i, b in sorted(badge.items()):
        if b.get("bucket") != "COMMUNITY" and i >= len(eff):
            print(f"  ! fx {i}: badge '{b['name']}' has no strip counterpart")
            diff += 1
    print(f"  fx id/name diff: {diff or 'none'}")
    # presets the badge cannot render as-is
    pre = load(d, "presets.json", {}) or {}
    for k, v in sorted(pre.items(), key=lambda kv: int(kv[0]) if kv[0].isdigit() else 0):
        if not v or "playlist" in v:
            continue
        seg = v.get("seg")
        seg = seg[0] if isinstance(seg, list) and seg else seg if isinstance(seg, dict) else {}
        fx = seg.get("fx")
        if isinstance(fx, int) and fx < len(eff):
            e = names.get(eff[fx].lower())
            if not e or not e["included"]:
                print(f"  preset {k} '{v.get('n', '')}': fx {fx} '{eff[fx]}' is not available on the badge")
    return 0


def run(args):
    out = args.out
    if args.host:
        require_git_ignored(out, "snapshot")
        base = args.host if "://" in args.host else "http://" + args.host
        base = base.rstrip("/")
        os.makedirs(out, exist_ok=True)
        failed = []
        for fn, path in FILES.items():
            try:
                data = get(base, path, args.timeout)
                with open(os.path.join(out, fn), "wb") as f:
                    f.write(data)
            except (urllib.error.URLError, OSError) as e:
                failed.append(f"{path} ({e})")
        if "/json/info (" in " ".join(failed):
            raise SystemExit(f"snapshot: {base} did not answer /json/info: {failed[0]}")
        info = load(out, "info.json", {})
        # custom palettes are /palette<N>.json; probe until cpalcount found (ids may have gaps)
        want, found = int(info.get("cpalcount", 0) or 0), 0
        for i in range(int(info.get("cpalmax", 10) or 10)):
            if found >= want:
                break
            try:
                data = get(base, f"/palette{i}.json", args.timeout)
                with open(os.path.join(out, f"palette{i}.json"), "wb") as f:
                    f.write(data)
                found += 1
            except (urllib.error.URLError, OSError):
                pass
        if found < want:
            failed.append(f"custom palettes: found {found} of {want}")
        meta = {"host": args.host, "wled": info.get("ver"),
                "snapshot": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "failed": failed}
        with open(os.path.join(out, "snapshot.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=1)
            f.write("\n")
    elif not os.path.isdir(out):
        raise SystemExit(f"snapshot: {out} does not exist (pass --host to take one)")
    return report(out)
