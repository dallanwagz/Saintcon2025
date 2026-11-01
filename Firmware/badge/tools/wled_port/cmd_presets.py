"""wledport presets - build the badge's mirror table (wled_presets*.json) from a strip snapshot + the HA menu.

Each HA Menu label (parsed from CONFIG_BADGE_MENU exactly like components/ui/.../hamenu.c) is matched to a
WLED preset of the same name (case-insensitive; the preset name clipped to 19 characters also counts).
Unmatched labels go through the auto rules:
  "Toggle" -> toggle, "Off" -> off, "On" -> on, "NN%" -> bri NN,
  "<anything> NNNNK" -> preset: fx 0 Solid in colour temperature NNNN K (WLED colorKtoRGB, see kelvin_rgb)
Entries already in --out marked "manual": true are kept as they are (and win over generated ones).
The output holds household data: name it wled_presets*.json (git-ignored).
"""
import json
import math
import os
import re

from wp_common import load_manifest, require_git_ignored
from cmd_snapshot import load

HELP = "match HA menu labels to a strip snapshot's presets and write wled_presets*.json"

CMD_MAX = 19          # BTHOME_CMD_MAX (components/hid/include/bthome.h)
MAX_ROWS, MAX_ENTRIES, ENTRY_ROWS = 32, 9, 24   # hamenu.c HAMENU_MAX_ROWS / HAMENU_MAX_ENTRIES / popup rows
DEF = dict(fx=0, pal=0, sx=128, ix=128, c1=128, c2=128, c3=16, o1=False, o2=False, o3=False)  # WLED seg defaults


def add_args(p):
    p.add_argument("--snapshot", required=True, help="dir written by 'wledport.py snapshot'")
    p.add_argument("--menu", required=True, help="sdkconfig (or any file) with CONFIG_BADGE_MENU=\"...\"")
    p.add_argument("--out", required=True, help="output json, e.g. wled_presets.json (git-ignored)")
    p.add_argument("--manual", action="append", default=[], metavar="MENU=ACTION[:VALUE]",
                   help='add/replace a hand-written entry, e.g. "Good night=off" or "Brightness/5%%=bri:5"')


# ------------------------------------------------------------------ badge menu (mirror of hamenu.c parse_rows)
def read_menu(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r'^CONFIG_BADGE_MENU="(.*)"\s*$', line)
            if m:
                return re.sub(r'\\(.)', r'\1', m.group(1))
    raise SystemExit(f"presets: no CONFIG_BADGE_MENU in {path}")


def clip(t):
    """hamenu.c clip(): skips leading spaces only for the empty test and the cut, the label keeps them.
    The C code cuts at 19 BYTES (it can split a UTF-8 sequence); a split character is dropped here, which
    differs from the badge's half-character key. ponytail: such a label never matches, so avoid it."""
    b = t.encode("utf-8")
    lead = len(b) - len(b.lstrip(b" "))
    cut = lambda x: x.decode("utf-8", errors="ignore")
    return cut(b[:lead + CMD_MAX]), cut(b[lead:lead + CMD_MAX])


def menu_labels(s):
    """[(menu key, label to match)] in menu order: 'Row/Entry' for popup entries, 'Row' for plain rows."""
    out, rows = [], 0
    for row in (r for r in s.split(";") if r):   # strtok_r skips empty tokens
        if rows >= MAX_ROWS:
            break
        parts = [p for p in row.split("|") if p]
        if not parts:
            continue
        label, test = clip(parts[0])
        if not test:
            continue
        entries = []
        for p in parts[1:]:
            if len(entries) >= MAX_ENTRIES:
                break
            e, et = clip(p)
            if et:
                entries.append(e)
        if entries and rows >= ENTRY_ROWS:
            entries = []
        rows += 1
        if entries:
            out += [(f"{label}/{e}", e) for e in entries]
        else:
            out.append((label, label))
    return out


# ------------------------------------------------------------------ conversions
def kelvin_rgb(k):
    """WLED colorKtoRGB() (colors.cpp), i.e. Tanner Helland's blackbody fit; HA's
    color_temperature_to_rgb() uses the same fit, so badge, strip and HA agree."""
    t = k / 100.0
    if t <= 66:
        r = 255
        g = round(99.4708025861 * math.log(t) - 161.1195681661)
        b = 0 if t <= 19 else round(138.5177312231 * math.log(t - 10) - 305.0447927307)
    else:
        r = round(329.698727446 * (t - 60) ** -0.1332047592)
        g = round(288.1221695283 * (t - 60) ** -0.0755148492)
        b = 255
    return [max(0, min(255, v)) for v in (r, g, b)]


def rgb(c):
    """WLED colour (list [r,g,b(,w)] or hex string) -> [r,g,b]. The badge has no white channel: W is
    added to R, G and B (ponytail: neutral white, good enough for the occasional RGBW preset)."""
    if isinstance(c, str):
        c = c.lstrip("#")
        c = [int(c[i:i + 2], 16) for i in range(0, len(c), 2)] if c else []
    c = list(c or []) + [0, 0, 0, 0]
    w = c[3] if len(c) > 3 else 0
    return [min(255, int(v) + int(w)) for v in c[:3]]


def first_seg(preset):
    seg = preset.get("seg")
    if isinstance(seg, list):
        seg = next((s for s in seg if isinstance(s, dict) and s.get("stop", 1) != 0), seg[0] if seg else {})
    return seg if isinstance(seg, dict) else {}


def seg_params(seg, eff_names, by_name, leds, notes):
    p = dict(DEF)
    p.update({k: seg[k] for k in DEF if k in seg})
    # map the strip's fx id to the badge's by name (ids differ for usermod/community effects)
    sname = eff_names[p["fx"]] if p["fx"] < len(eff_names) else None
    e = by_name.get((sname or "").lower())
    if not e or not e["included"]:
        return None, f"fx {p['fx']} '{sname}' is not available on the badge"
    p["fx"] = e["id"]
    if p["pal"] >= 72:
        notes.append(f"custom palette {p['pal']} -> 0")
        p["pal"] = 0
    p["c3"] = min(31, p["c3"])
    cols = seg.get("col") or []
    p["col"] = [rgb(cols[i]) if i < len(cols) else [0, 0, 0] for i in range(3)]
    # vlen = the segment's virtual length, which is what the effect renders on the strip
    start, stop = seg.get("start", 0), seg.get("stop", leds)
    vlen = max(1, stop - start)
    grp = max(1, seg.get("grp", 1)) + max(0, seg.get("spc", 0))
    vlen = (vlen + grp - 1) // grp
    if seg.get("mi"):
        vlen = (vlen + 1) // 2
        notes.append("mirror dropped (vlen halved)")
    if seg.get("rev"):
        notes.append("reverse dropped")
    p["vlen"] = min(300, vlen)
    return p, None


AUTO = [
    (re.compile(r"^toggle$", re.I), lambda m: {"action": "toggle"}),
    (re.compile(r"^off$", re.I), lambda m: {"action": "off"}),
    (re.compile(r"^on$", re.I), lambda m: {"action": "on"}),
    (re.compile(r"^(\d{1,3})\s*%$"), lambda m: {"action": "bri", "value": max(0, min(100, int(m.group(1))))}),
    (re.compile(r"^.*?(\d{4,5})\s*K$", re.I), None),  # kelvin, handled below (needs the strip LED count)
]


def auto(label, leds):
    t = label.strip()
    for rx, fn in AUTO:
        m = rx.match(t)
        if m and fn:
            return fn(m)
        if m:
            k = max(1000, min(40000, int(m.group(1))))
            p = dict(DEF, col=[kelvin_rgb(k), [0, 0, 0], [0, 0, 0]], vlen=leds)
            return {"action": "preset", "params": p}
    return None


def parse_manual(spec):
    menu, _, act = spec.partition("=")
    act, _, val = act.partition(":")
    if act not in ("toggle", "off", "on", "bri") or not menu:
        raise SystemExit(f"presets: --manual '{spec}': use MENU=toggle|off|on|bri:NN (presets: edit the json)")
    e = {"menu": menu.strip(), "action": act}
    if act == "bri":
        e["value"] = int(val or 100)
    e["manual"] = True
    return e


def dump(doc):
    """One entry per line, like wled_presets.example.json."""
    lines = ["{", f' "source": {json.dumps(doc["source"])},', ' "entries": [']
    lines += ["  " + json.dumps(e) + ("," if i < len(doc["entries"]) - 1 else "") for i, e in enumerate(doc["entries"])]
    lines += [" ]", "}"]
    return "\n".join(lines) + "\n"


def run(args):
    d = args.snapshot
    info, eff, pre, cfg = load(d, "info.json", {}), load(d, "eff.json", []), load(d, "presets.json", {}), load(d, "cfg.json", {})
    snap = load(d, "snapshot.json", {})
    if not info or not eff:
        raise SystemExit(f"presets: {d} has no info.json/eff.json (run 'wledport.py snapshot' first)")
    leds = int(info.get("leds", {}).get("count", 20))
    man = load_manifest()
    by_name = {e["name"].lower(): e for e in man["effects"]}
    by_preset = {}
    for k, v in (pre or {}).items():
        n = (v or {}).get("n")
        if n:
            by_preset.setdefault(n.lower(), (k, v))
            by_preset.setdefault(n[:CMD_MAX].lower(), (k, v))

    require_git_ignored(args.out, "presets")
    old = {}
    if os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as f:
            old = json.load(f)
    manual = {e["menu"]: e for e in old.get("entries", []) if e.get("manual")}
    for spec in args.manual:
        e = parse_manual(spec)
        manual[e["menu"]] = e

    entries, rep = [], {"matched": [], "auto": [], "manual": [], "unmatched": []}
    for menu, label in menu_labels(read_menu(args.menu)):
        if menu in manual:
            entries.append(manual.pop(menu))
            rep["manual"].append(f"{menu}: {entries[-1]['action']}")
            continue
        hit = by_preset.get(label.strip().lower())
        if hit:
            k, v = hit
            notes = []
            if "playlist" in v:
                rep["unmatched"].append(f"{menu}: preset {k} '{v.get('n')}' is a playlist (not mirrored)")
                continue
            if v.get("on") is False:
                entries.append({"menu": menu, "action": "off"})
                rep["matched"].append(f"{menu}: preset {k} '{v.get('n')}' (on:false) -> off")
                continue
            p, err = seg_params(first_seg(v), eff, by_name, leds, notes)
            if err:
                rep["unmatched"].append(f"{menu}: preset {k} '{v.get('n')}': {err}")
                continue
            entries.append({"menu": menu, "action": "preset", "params": p})
            rep["matched"].append(f"{menu}: preset {k} '{v.get('n')}' -> fx {p['fx']} {by_name_id(man, p['fx'])}"
                                  f" pal {p['pal']} vlen {p['vlen']}" + (f" ({'; '.join(notes)})" if notes else ""))
            continue
        a = auto(label, leds)
        if a:
            entries.append(dict({"menu": menu}, **a))
            extra = f" {a['value']}%" if "value" in a else (f" Solid rgb{tuple(a['params']['col'][0])}" if "params" in a else "")
            rep["auto"].append(f"{menu}: {a['action']}{extra}")
        else:
            rep["unmatched"].append(f"{menu}: no preset or rule")
    for menu, e in manual.items():   # hand-written entries for labels no longer in the menu: keep, but say so
        entries.append(e)
        rep["manual"].append(f"{menu}: {e['action']} (label not in the current menu)")

    doc = {"source": {"host": snap.get("host", ""), "wled": info.get("ver", ""), "leds": leds,
                      "fps": cfg.get("hw", {}).get("led", {}).get("fps", info.get("leds", {}).get("fps", 42)),
                      "snapshot": snap.get("snapshot", "")},
           "entries": entries}
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(dump(doc))
    for k in ("matched", "auto", "manual", "unmatched"):
        print(f"{k} ({len(rep[k])}):")
        for line in rep[k]:
            print("  " + line)
    print(f"wrote {args.out}: {len(entries)} entries")
    return 0


def by_name_id(man, fid):
    return next((f"'{e['name']}'" for e in man["effects"] if e["id"] == fid), "")
