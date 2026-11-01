"""wledport import - (re)generate components/wled_fx/{upstream,gen,manifest.json} from a WLED checkout.

Three classes of source:
  upstream/ : byte-for-byte copies of WLED files that compile whole behind shim/ (FX.cpp, FX.h, ...)
  gen/      : function bodies carved verbatim (by name, brace matching) out of files that cannot
              compile whole (util.cpp, FX_fcn.cpp, colors.cpp minus its LittleFS loader), plus
              tables derived from upstream data (effect list, palette names)
  shim/     : hand written, not touched here
Re-running against a newer tag is the whole "port"; running twice gives no diff.
"""
import os
import re
import shutil
import subprocess
import sys
import json

from wp_common import COMPONENT_DIR, BUILD_DEFINES, sha256, write_if_changed, community_sync

HELP = "import WLED sources into components/wled_fx (upstream/, gen/, manifest.json)"

VERBATIM = ["FX.cpp", "FX.h", "colors.h", "palettes.cpp", "wled_math.cpp", "prng.h", "const.h",
            "src/dependencies/fastled_slim/fastled_slim.h",
            "src/dependencies/fastled_slim/fastled_slim.cpp",
            "src/dependencies/fastled_slim/LICENSE.txt"]

# (source file, [function names | ('raw', start_regex, end_regex)])
CARVE = {
    "util.cpp": [
        "extractModeDefaults", "beat88", "beat16", "beat8", "beatsin88_t", "beatsin16_t", "beatsin8_t",
        ("raw", r"^typedef enum UM_SoundSimulations", r"^\} um_soundSimulations_t;"),
        "simulateSound", "get_random_wheel_index", "mapf", "hashInt", "hw_random",
        ("raw", r"^#define PERLIN_SHIFT", r"^#define PERLIN_SHIFT"),
        "hashToGradient", "gradient1D", "gradient2D", "gradient3D", "smoothstep", "lerpPerlin",
        "perlin1D_raw", "perlin2D_raw", "perlin3D_raw", "perlin16", "perlin8",
    ],
    "FX_fcn.cpp": [
        ("raw", r"^unsigned\s+Segment::_usedSegmentData", r"^uint8_t\s+Segment::_clipStopY"),
        "Segment::Segment(const Segment", "Segment::Segment(Segment &&", "Segment::operator=",
        "Segment::allocateData", "Segment::deallocateData", "Segment::resetIfRequired",
        "Segment::loadPalette", "Segment::stopTransition", "Segment::updateTransitionProgress",
        "Segment::currentCCT", "Segment::currentBri", "Segment::beginDraw", "Segment::handleRandomPalette",
        "Segment::setColor", "Segment::setOption", "Segment::setMode", "Segment::setPalette",
        "Segment::virtualWidth", "Segment::virtualHeight", "Segment::virtualLength",
        "Segment::isPixelClipped", "Segment::setPixelColor(int", "Segment::getPixelColor(int",
        "Segment::fill", "Segment::fade_out", "Segment::fadeToSecondaryBy", "Segment::fadeToBlackBy",
        "Segment::blur", "Segment::color_wheel", "Segment::color_from_palette",
        "WS2812FX::getActiveSegmentsNum", "WS2812FX::getLengthTotal", "WS2812FX::getSegment",
    ],
    # colors.cpp is verbatim except loadCustomPalettes() (LittleFS + ArduinoJson)
    "colors.cpp": ["*except*", "loadCustomPalettes"],
}

# Effects that compile but make no sense on a single 1D segment (bucket OTHER_BLOCKER is excluded too).
EXCLUDE = {77: "Copy Segment needs a second segment to copy from"}

SPDX = "// SPDX-License-Identifier: EUPL-1.2\n"


def add_args(p):
    p.add_argument("--src", required=True, help="WLED checkout (repo root or its wled00/)")
    p.add_argument("--tag", default="v16.0.1", help="expected git tag of the checkout (default v16.0.1)")
    p.add_argument("--dst", default=COMPONENT_DIR, help="component dir (default components/wled_fx)")


# ------------------------------------------------------------------ carving
def strip_code(line, state):
    """Line with comments/strings blanked so braces can be counted. state['c'] = in block comment."""
    out, i = [], 0
    while i < len(line):
        if state["c"]:
            j = line.find("*/", i)
            if j < 0:
                return "".join(out)
            state["c"] = False
            i = j + 2
            continue
        ch = line[i]
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            state["c"] = True
            i += 2
            continue
        if ch in "\"'":
            q = ch
            i += 1
            while i < len(line) and line[i] != q:
                i += 2 if line[i] == "\\" else 1
            i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def find_defs(lines, name):
    """All top-level definitions of `name` (overloads included) as inclusive (start, end) line spans."""
    pat = re.compile(r"^(?:[A-Za-z_\[][^;]*?\b)?" + re.escape(name) + ("" if "(" in name else r"\s*\("))
    spans, i = [], 0
    while i < len(lines):
        if pat.match(lines[i]) and not lines[i].rstrip().endswith(";"):
            state, depth, opened, j = {"c": False}, 0, False, i
            while j < len(lines):
                code = strip_code(lines[j], state)
                if not opened and ";" in code and "{" not in code and j > i + 3:
                    break  # a prototype
                depth += code.count("{") - code.count("}")
                opened = opened or "{" in code
                if opened and depth == 0:
                    spans.append((i, j))
                    break
                j += 1
            i = j + 1
        else:
            i += 1
    return spans


def carve(src_lines, items, fname):
    out = []
    for it in items:
        if isinstance(it, tuple):
            _, a, b = it
            s = next(k for k, l in enumerate(src_lines) if re.match(a, l))
            e = next(k for k in range(s, len(src_lines)) if re.match(b, src_lines[k]))
            out.append(f"// ---- {fname}:{s+1}-{e+1}\n" + "".join(src_lines[s:e + 1]))
            continue
        spans = find_defs(src_lines, it)
        if not spans:
            sys.exit(f"carve: {it} not found in {fname}")
        for s, e in spans:
            out.append(f"// ---- {fname}:{s+1}-{e+1}  {it}\n" + "".join(src_lines[s:e + 1]))
    return "\n".join(out)


# ------------------------------------------------------------------ classification
def strip_cs(t):
    out, i, n = [], 0, len(t)
    while i < n:
        if t.startswith("//", i):
            j = t.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif t.startswith("/*", i):
            j = t.find("*/", i) + 2
            out.append("".join(c if c == "\n" else " " for c in t[i:j])); i = j
        elif t[i] in "\"'":
            q, j = t[i], i + 1
            while t[j] != q:
                j += 2 if t[j] == "\\" else 1
            out.append(q + " " * (j - i - 1) + q); i = j + 1
        else:
            out.append(t[i]); i += 1
    return "".join(out)


def eval_guard(line, defs):
    """Evaluate a #if/#ifdef/#ifndef line against the set of defined macros."""
    m = re.match(r"#\s*ifdef\s+(\w+)", line)
    if m:
        return m.group(1) in defs
    m = re.match(r"#\s*ifndef\s+(\w+)", line)
    if m:
        return m.group(1) not in defs
    expr = re.sub(r"#\s*if\s+", "", line).split("//")[0]
    expr = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda mm: str(mm.group(1) in defs), expr)
    expr = expr.replace("||", " or ").replace("&&", " and ").replace("!", " not ")
    return bool(eval(expr, {"__builtins__": {}}))


def effective_defines(fx_src):
    """BUILD_DEFINES plus what FX.cpp's own preamble derives from them (WLED_PS_DONT_REPLACE_*)."""
    d = set(BUILD_DEFINES)
    if "WLED_DISABLE_PARTICLESYSTEM2D" in d and "WLED_DISABLE_PARTICLESYSTEM1D" in d or "WLED_PS_DONT_REPLACE_FX" in d:
        d |= {"WLED_PS_DONT_REPLACE_1D_FX", "WLED_PS_DONT_REPLACE_2D_FX"}
    return d


def parse_meta(s):
    name, _, rest = s.partition("@")
    parts = rest.split(";") if rest else []
    while len(parts) < 5:
        parts.append("")
    return name, parts[3]


AUD = re.compile(r"getAudioData|um_data|simulateSound|volumeSmth|fftResult")
PS = re.compile(r"ParticleSystem|PartSys|initParticleSystem")
OTHER = re.compile(r"renderImageToSegment|localTime|\bhour\(|\bminute\(|drawCharacter|fontmanager|FontManager|"
                   r"strip\._segments|strip\.getSegment\(|strip\.getMainSegment|WiFi|udp|File\b|WLED_FS|Network")


def classify(w):
    src = open(os.path.join(w, "FX.cpp"), encoding="utf-8").read()
    hdr = open(os.path.join(w, "FX.h"), encoding="utf-8").read()
    lines = src.split("\n")
    clean = strip_cs(src)
    ids = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(FX_MODE_\w+)\s+(\d+)", hdr)}
    meta = {m.group(1): m.group(2) for m in
            re.finditer(r'static const char (_data_FX_MODE_\w+)\[\]\s*PROGMEM\s*=\s*"((?:[^"\\]|\\.)*)"', src)}
    funcs = {}
    for m in re.finditer(r"^(?!\s)(?:static[ \t]+)?(?:inline[ \t]+)?(?:const[ \t]+)?[A-Za-z_][\w:<>]*[ \t\*]+(\w+)\s*\([^;{)]*\)\s*(?:const\s*)?\{", clean, re.M):
        i, depth = m.end(), 1
        while depth and i < len(clean):
            depth += (clean[i] == "{") - (clean[i] == "}")
            i += 1
        funcs.setdefault(m.group(1), clean[m.start():i])

    def closure(fn, seen):
        if fn in seen or fn not in funcs:
            return seen
        seen.add(fn)
        for tok in set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", funcs[fn])):
            if tok != fn and tok in funcs and tok not in ("addEffect", "setupEffectData"):
                closure(tok, seen)
        return seen

    defs = effective_defines(src)
    start = next(i for i, l in enumerate(lines) if "void WS2812FX::setupEffectData" in l)
    stack, regs = [], [dict(sym="FX_MODE_STATIC", fn="mode_static", data="_data_FX_MODE_STATIC", guards=[], on=True)]
    for i in range(start, len(lines)):
        l = lines[i].strip()
        if l.startswith("#if"):
            stack.append((l, eval_guard(l, defs)))
        elif l.startswith("#else"):
            g, v = stack[-1]
            stack[-1] = ("!(" + g + ")", not v)
        elif l.startswith("#endif"):
            stack.pop()
        m = re.match(r"addEffect\((FX_MODE_\w+),\s*&(\w+),\s*(_data_FX_MODE_\w+)\)", l)
        if m:
            regs.append(dict(sym=m.group(1), fn=m.group(2), data=m.group(3),
                             guards=[g for g, _ in stack], on=all(v for _, v in stack)))
        if lines[i].startswith("}") and i > start:
            break
    out = {}
    for r in regs:
        fid = ids[r["sym"]]
        body = "\n".join(funcs[f] for f in sorted(closure(r["fn"], set())))
        name, flags = parse_meta(meta[r["data"]])
        g = " ".join(r["guards"])
        if PS.search(body) or "PARTICLESYSTEM" in g:
            b = "PARTICLE_SYSTEM"
        elif AUD.search(body) or "v" in flags or "f" in flags:
            b = "NEEDS_AUDIO"   # runs on simulateSound() when no audio usermod is present
        elif "WLED_DISABLE_2D" in g or ("2" in flags and "1" not in flags):
            b = "2D_ONLY"
        elif OTHER.search(body):
            b = "OTHER_BLOCKER"
        else:
            b = "PORTABLE_1D"
        if not r["on"]:
            inc, why = False, "not compiled with " + ",".join(BUILD_DEFINES)
        elif fid in EXCLUDE:
            inc, why = False, EXCLUDE[fid]
        elif b == "OTHER_BLOCKER":
            inc, why = False, "needs strip features the badge lacks"
        else:
            inc, why = True, ""
        if fid in out:  # same id registered in both #ifdef/#else arms: keep the compiled one
            if not inc and out[fid]["compiled"]:
                continue
        out[fid] = dict(id=fid, sym=r["sym"], fn=r["fn"], name=name, bucket=b, compiled=r["on"],
                        included=inc, **({"why": why} if why else {}))
    mc = re.search(r"#define\s+MODE_COUNT\s+(\d+)", hdr)
    return [out[k] for k in sorted(out)], int(mc.group(1)) if mc else None


def palette_names(w):
    src = open(os.path.join(w, "FX_fcn.cpp"), encoding="utf-8").read()
    m = re.search(r'JSON_palette_names\[\]\s*PROGMEM\s*=\s*R"=====\((.*?)\)====="', src, re.S)
    return json.loads(m.group(1))


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


# ------------------------------------------------------------------ main
def git(src, *a):
    try:
        return subprocess.run(["git", "-C", src, *a], capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return ""


def sync_tree(root, produced):
    """Delete files under root that this run did not produce (keeps reruns exact)."""
    for dp, dns, fns in os.walk(root, topdown=False):
        for fn in fns:
            p = os.path.join(dp, fn)
            if p not in produced:
                os.remove(p)
        if not os.listdir(dp):
            os.rmdir(dp)


def run(args):
    src = os.path.abspath(args.src)
    repo = os.path.dirname(src) if os.path.basename(src) == "wled00" else src
    w = os.path.join(repo, "wled00")
    if not os.path.isfile(os.path.join(w, "FX.cpp")):
        sys.exit(f"import: {w}/FX.cpp not found")
    tag = git(repo, "describe", "--tags", "--exact-match")
    commit = git(repo, "rev-parse", "HEAD")
    if args.tag and tag and tag != args.tag:
        sys.exit(f"import: checkout is at {tag}, expected {args.tag} (pass --tag {tag} to accept)")
    tag = tag or args.tag
    dst = os.path.abspath(args.dst)
    up_dir, gen_dir = os.path.join(dst, "upstream", "wled00"), os.path.join(dst, "gen")
    produced, changed = set(), 0
    hdr = f"// Generated by tools/wled_port/wledport.py import from WLED {tag} ({commit[:12]}) wled00/%s - do not edit\n"

    # 1. verbatim copies (+ WLED LICENSE at component root)
    for f in VERBATIM:
        d = os.path.join(up_dir, f)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if not os.path.exists(d) or sha256(d) != sha256(os.path.join(w, f)):
            shutil.copyfile(os.path.join(w, f), d)
            changed += 1
        produced.add(d)
    lic = os.path.join(dst, "LICENSE")
    if not os.path.exists(lic) or sha256(lic) != sha256(os.path.join(repo, "LICENSE")):
        shutil.copyfile(os.path.join(repo, "LICENSE"), lic)
        changed += 1

    # 2. carved sources
    for fname, items in CARVE.items():
        lines = open(os.path.join(w, fname), encoding="utf-8").readlines()
        if items and items[0] == "*except*":
            drop = set()
            for it in items[1:]:
                for s, e in find_defs(lines, it):
                    drop.update(range(s, e + 1))
            body = "".join(l for k, l in enumerate(lines) if k not in drop)
            out = os.path.join(gen_dir, fname)
            note = f"// (verbatim minus: {', '.join(items[1:])})\n"
        else:
            body = '#include "wled.h"\n#include "colors.h"\n\n' + carve(lines, items, fname)
            out = os.path.join(gen_dir, "carved_" + fname)
            note = ""
        changed += write_if_changed(out, SPDX + hdr % fname + note + body)
        produced.add(out)

    # 3. effect table + palette names
    fx, mode_count = classify(w)
    pals = palette_names(w)
    inc = [e for e in fx if e["included"]]
    t = [SPDX, hdr % "FX.cpp FX.h FX_fcn.cpp (effect registry, palette names)",
         "#pragma once\n#include <stdint.h>\n\n",
         "typedef struct { uint8_t id; const char *name; } wled_fx_entry_t;\n",
         f"static const wled_fx_entry_t WLED_FX_TABLE[{len(inc)}] = {{\n"]
    t += [f"  {{{e['id']:3d}, {c_str(e['name'])}}},\n" for e in inc]
    t += ["};\n\n", f"static const char *const WLED_PAL_NAMES[{len(pals)}] = {{\n"]
    t += [f"  {c_str(n)},\n" for n in pals]
    t += ["};\n"]
    p = os.path.join(gen_dir, "wled_fx_tables.h")
    changed += write_if_changed(p, "".join(t))
    produced.add(p)

    sync_tree(os.path.join(dst, "upstream"), produced)
    sync_tree(gen_dir, produced)

    # 4. manifest
    srcs = sorted(set(VERBATIM) | set(CARVE))
    man = {
        "wled": {"tag": tag, "commit": commit, "repo": "https://github.com/wled/WLED"},
        "defines": BUILD_DEFINES,
        "mode_count": mode_count,
        "palettes": len(pals),
        "counts": {"registered": len(fx), "included": len(inc)},
        "sources": {f"wled00/{f}": sha256(os.path.join(w, f)) for f in srcs},
        "effects": fx,
    }
    if os.path.abspath(dst) == os.path.abspath(COMPONENT_DIR):
        changed += community_sync(man, mode_count)  # keep add-ed effects, renumbered after the new MODE_COUNT
    changed += write_if_changed(os.path.join(dst, "manifest.json"), json.dumps(man, indent=1) + "\n")
    print(f"import: WLED {tag} {commit[:12]}: {len(VERBATIM)} verbatim, {len(CARVE)} carved, "
          f"{len(fx)} effects ({len(inc)} included), {len(pals)} palettes; {changed} file(s) changed")
    return 0
