"""wledport add - bring a community / newer WLED 1D effect into components/wled_fx/community/.

  wledport.py add <file.cpp|https://...>[:mode_fn] [--name "New Name"] [--licence SPDX --source URL]

Carves the effect function, its metadata string (_data_...), the comment above it and whatever macros and
helper functions of the same file it uses (transitively) into community/<slug>.cpp, keeping the file's own
header comment. Old-API effects (uint16_t mode_x() returning a frame delay) are rewritten to the v16 form
(void, plain return). It then refuses 2D / particle-system / strip-feature effects, gives the effect the next
free id after the stock table (matched by name everywhere else), regenerates the registry, compiles the host
harness and runs the sanitizer sweep for it. Any failure rolls everything back.
"""
import datetime
import os
import re
import subprocess
import sys
import tempfile
import urllib.request

from wp_common import COMMUNITY_DIR, HOST_DIR, load_manifest, save_manifest, community_sync
from cmd_import import find_defs, strip_cs, parse_meta, PS, OTHER, AUD

HELP = "add a community/newer 1D effect (file.cpp[:mode_fn]) to wled_fx, register it and sweep it"

# Licences that may sit in the EUPL-1.2 directory (each file keeps its own). Anything else needs --i-know-licence.
OK_LICENCES = re.compile(r"^(EUPL-1\.2|MIT|BSD-[23]-Clause|Apache-2\.0|Zlib|CC0-1\.0|Unlicense|public-domain)$")
SPDX_LINE = re.compile(r"SPDX-License-Identifier:\s*([\w.+-]+)")
# ponytail: word guesses for headers without an SPDX line, first hit wins; GPL comes first so "LGPL" etc. never pass
GUESSES = [(r"\bL?GPL|General Public License", "GPL"), (r"EUPL", "EUPL-1.2"), (r"MIT License|\bMIT\b", "MIT"),
           (r"Apache License", "Apache-2.0"), (r"\bBSD\b", "BSD-3-Clause"), (r"\bzlib\b", "Zlib"),
           (r"public domain", "public-domain")]


def detect_licence(head):
    m = SPDX_LINE.search(head)
    if m:
        return m.group(1)
    return next((lic for rx, lic in GUESSES if re.search(rx, head, re.I)), None)
# Old (0.14/0.15) API names -> v16. Applied to the carved code; extend when an import trips over another rename.
RENAMES = [(r"\brandom8\(", "hw_random8("), (r"\brandom16\(", "hw_random16("), (r"\brandom\(", "hw_random("),
           (r"\bstrip\.paletteBlend\b", "paletteBlend")]
TOPFUNC = re.compile(r"^(?!\s)(?:static[ \t]+)?(?:inline[ \t]+)?(?:const[ \t]+)?[A-Za-z_][\w:<>]*[ \t\*&]+(\w+)\s*\([^;{)]*\)\s*(?:const\s*)?\{", re.M)


def add_args(p):
    p.add_argument("src", help="file.cpp or URL, optionally :mode_fn when it holds several effects")
    p.add_argument("--name", help="register under this name (needed when the name already exists)")
    p.add_argument("--licence", help="SPDX id to record when the file carries no licence text (e.g. EUPL-1.2)")
    p.add_argument("--source", help="where it came from (URL, commit); recorded in the file and the manifest")
    p.add_argument("--i-know-licence", action="store_true", help="proceed without any licence/attribution info")
    p.add_argument("--keep", action="store_true", help="keep the files even if the build or sweep fails")


def fetch(src):
    path, fn = src, None
    m = re.match(r"^(.*\.(?:cpp|h|ino|c))(?::(\w+))?$", src)
    if m:
        path, fn = m.group(1), m.group(2)
    if re.match(r"^https?://", path):
        tmp = os.path.join(tempfile.mkdtemp(prefix="wledport-add-"), os.path.basename(path))
        with open(tmp, "wb") as f:
            f.write(urllib.request.urlopen(path, timeout=30).read())
        return tmp, fn, path
    if not os.path.isfile(path):
        raise SystemExit(f"add: {path} not found")
    return path, fn, None


def macro_spans(lines):
    """{name: (start, end)} for every #define (with \\ continuations), top-level const/constexpr and struct."""
    out, i = {}, 0
    while i < len(lines):
        m = re.match(r"\s*#\s*define\s+(\w+)", lines[i])
        if m:
            j = i
            while lines[j].rstrip().endswith("\\") and j + 1 < len(lines):
                j += 1
            out.setdefault(m.group(1), (i, j))
            i = j
        # top-level constants and struct/class/enum definitions the effect may use
        m = re.match(r"(?:static\s+)?(?:constexpr|const)\s+[\w:<>]+\s+(\w+)\s*(?:=|\[)", lines[i])
        if m and lines[i].rstrip().endswith(";"):
            out.setdefault(m.group(1), (i, i))
        m = re.match(r"(?:typedef\s+)?(?:struct|class|enum(?:\s+class)?)\s+(\w+)[^;]*$", lines[i])
        if m:
            j, depth, opened = i, 0, False
            while j < len(lines):
                depth += lines[j].count("{") - lines[j].count("}")
                opened = opened or "{" in lines[j]
                if opened and depth == 0:
                    break
                j += 1
            out.setdefault(m.group(1), (i, j))
            i = j
        i += 1
    return out


def comment_above(lines, start):
    """Nearest comment block above a definition (skipping blank lines and #defines in between)."""
    i, block = start - 1, []
    while i >= 0:
        t = lines[i].strip()
        if t.endswith("*/"):                      # walk a /* ... */ block back to its opening line
            k = i
            while k > 0 and "/*" not in lines[k]:
                k -= 1
            block += range(k, i + 1)
            i = k - 1
            continue
        if t.startswith("//"):
            block.append(i)
        elif block and not t:
            break
        elif t and not t.startswith("#") and not lines[i].rstrip().endswith("\\"):
            break
        i -= 1
    return (min(block), max(block)) if block else None


def top_comment(lines):
    """The file's leading comment (licence header), if it starts with one."""
    i = 0
    while i < len(lines) and not lines[i].strip():
        i += 1
    if i >= len(lines):
        return None
    if lines[i].lstrip().startswith("/*"):
        j = i
        while j < len(lines) and "*/" not in lines[j]:
            j += 1
        return (0, j)
    j = i
    while j < len(lines) and lines[j].lstrip().startswith("//"):
        j += 1
    return (0, j - 1) if j > i else None


def rollback(path, why):
    if path and os.path.exists(path):
        os.remove(path)
    man = load_manifest()
    community_sync(man, man["mode_count"])
    save_manifest(man)
    raise SystemExit(f"add: {why} (rolled back)")


def run(args):
    path, fn, url = fetch(args.src)
    text = open(path, encoding="utf-8").read()
    lines = text.splitlines(keepends=True)
    clean = strip_cs(text)
    clines = clean.splitlines(keepends=True)

    # which effect
    data_of = {m.group(1): m.group(2) for m in re.finditer(r"addEffect\([^,]+,\s*&?(\w+),\s*(\w+)\)", clean)}
    modes = [m.group(1) for m in re.finditer(r"^(?:static\s+)?(?:void|uint16_t)\s+(mode_\w+)\s*\(\s*(?:void)?\s*\)\s*\{", clean, re.M)]
    if not fn:
        cands = [m for m in modes if m != "mode_static"]
        if len(cands) != 1:
            raise SystemExit("add: pick one effect with <file>:<mode_fn>; candidates: " + ", ".join(cands))
        fn = cands[0]
    if fn not in modes:
        raise SystemExit(f"add: no effect function {fn}() in {path}")
    span = find_defs(lines, fn)[0]
    datas = re.findall(r"^(?:static\s+)?const\s+char\s+(_data_\w+)\s*\[\]", clean, re.M)
    data = data_of.get(fn) or next((d for d in datas if d.upper() == "_DATA_FX_MODE_" + fn[5:].upper()), None)
    if not data:
        after = [d for d in datas if clean.find(d) > clean.find(fn + "(")]
        data = after[0] if after else None
    if not data:
        raise SystemExit(f"add: no metadata string (_data_...) found for {fn}")
    di = next(i for i, l in enumerate(clines) if re.search(r"const\s+char\s+" + data + r"\s*\[\]", l))
    dj = di
    while ";" not in clines[dj]:
        dj += 1
    meta_str = re.search(r'"((?:[^"\\]|\\.)*)"', "".join(lines[di:dj + 1])).group(1)

    # dependency closure over this file's macros and functions
    macros = macro_spans(clines)
    funcs = {m.group(1) for m in TOPFUNC.finditer(clean)}
    pieces, todo, seen = {span, (di, dj)}, [fn], set()
    done_text = "".join(clines[span[0]:span[1] + 1])
    while todo:
        cur = todo.pop()
        if cur in seen:
            continue
        seen.add(cur)
        body = done_text if cur == fn else "".join("".join(clines[s:e + 1]) for s, e in
                                                   ([macros[cur]] if cur in macros else find_defs(clines, cur)))
        for tok in set(re.findall(r"\b[A-Za-z_]\w*\b", body)):
            if tok in seen or tok == fn:
                continue
            if tok in macros:
                pieces.add(macros[tok]); todo.append(tok)
            elif tok in funcs and tok not in ("setup", "loop", "addEffect"):
                for s in find_defs(lines, tok):
                    pieces.add(s)
                todo.append(tok)
    keep = set()
    for s, e in pieces:
        keep.update(range(s, e + 1))
    for c in filter(None, [comment_above(lines, span[0]), top_comment(lines)]):
        keep.update(range(c[0], c[1] + 1))

    # licence / attribution
    cm = [comment_above(lines, span[0]), top_comment(lines)]
    head = "".join("".join(lines[c[0]:c[1] + 1]) for c in cm if c)
    found = detect_licence(head)
    eff = args.licence or found
    if not args.i_know_licence:
        if not eff:
            raise SystemExit(f"add: no licence text around {fn} in {os.path.basename(path)}.\n"
                             "  Pass --licence <SPDX id> --source <url> (e.g. EUPL-1.2 for code from the WLED repo),\n"
                             "  or --i-know-licence if you have checked it yourself.")
        if not OK_LICENCES.match(eff):
            raise SystemExit(f"add: licence '{eff}' is not one that can go into components/wled_fx (EUPL-1.2 or\n"
                             "  MIT/BSD/Apache-2.0/Zlib/CC0/public domain). Pass --i-know-licence if you have checked it.")
        if not url and not args.source:
            raise SystemExit(f"add: {os.path.basename(path)} is a local file; pass --source <url/commit> so its origin is recorded")
    if eff and eff != "EUPL-1.2":
        print(f"add: note: licence {eff} - add a line for this file to components/wled_fx/NOTICE.md")

    # classify (on the carved code)
    carved_clean = "".join(clines[i] for i in sorted(keep))
    name, flags = parse_meta(meta_str)
    if PS.search(carved_clean):
        raise SystemExit(f"add: {fn} uses the particle system, which is off on the badge (WLED_DISABLE_PARTICLESYSTEM*)")
    uses_2d = re.search(r"\bSEG_[WH]\b|is2D\s*\(|isMatrix|\bXY\s*\(|virtualHeight", carved_clean)
    if "2" in flags and "1" not in flags or uses_2d and "1" not in flags:
        raise SystemExit(f"add: {fn} ('{name}') is a 2D effect (metadata flags '{flags}'"
                         + (f", uses {uses_2d.group(0)}" if uses_2d else "") + "); the badge is one 1D strip")
    if OTHER.search(carved_clean):
        raise SystemExit(f"add: {fn} needs strip features the badge lacks: {OTHER.search(carved_clean).group(0)}")
    if AUD.search(carved_clean):
        print(f"add: note: {fn} is audio-reactive; it compiles only if it uses simulateSound/getAudioData like FX.cpp")

    # rewrites. Old API (<= 0.15): mode functions return a uint16_t frame delay, and so do helpers they
    # 'return helper(...)'. Those become void with plain returns (v16 runs every effect every frame).
    old_api = set()
    for i in sorted(keep):
        m = re.match(r"^(?:static\s+)?uint16_t\s+(mode_\w+)\s*\(", lines[i])
        if m:
            old_api.add(m.group(1))
    grow = True
    while grow:
        grow = False
        for f in list(old_api):
            for s0, e0 in find_defs(clines, f):
                for g in re.findall(r"\breturn\s+(\w+)\s*\(", "".join(clines[s0:e0 + 1])):
                    if g not in old_api and any(re.match(r"^(?:static\s+)?uint16_t\s+" + g + r"\s*\(", lines[a])
                                                for a, _ in find_defs(clines, g)):
                        old_api.add(g)
                        grow = True
    res = [sp for f in old_api for sp in find_defs(clines, f)]
    out, notes, renamed, nret = [], [], set(), 0
    for i in sorted(keep):
        l = lines[i]
        l = re.sub(r"^(static\s+)?(?:void|uint16_t)\s+(mode_\w+)\s*\(\s*(void)?\s*\)", r"static void \2(void)", l)
        for f in old_api:
            l = re.sub(r"^(static\s+)?uint16_t\s+(" + f + r")\s*\(", r"static void \2(", l)
        if any(s0 < i <= e0 for s0, e0 in res):
            l2 = re.sub(r"\breturn\s+(?!(?:" + "|".join(old_api) + r")\s*\()[^;]+;", "return;", l)
            nret += l2 != l
            l = l2
        for rx, rep in RENAMES:
            l2 = re.sub(rx, rep, l)
            if l2 != l and not l.lstrip().startswith(("//", "*", "/*")):
                renamed.add(rep.rstrip("("))
                l = l2
        if i == di and not l.lstrip().startswith("static"):
            l = "static " + l.lstrip()
        if i == di and args.name:
            l = l.replace('"' + name, '"' + args.name, 1)
        out.append((i, l))
    if old_api:
        notes.append(f"old API: {', '.join(sorted(old_api))} made void, {nret} 'return <delay>;' -> 'return;'")
    if renamed:
        notes.append("renamed calls to " + ", ".join(sorted(renamed)))
    if args.name:
        notes.append(f"renamed '{name}' -> '{args.name}'")
        name = args.name

    man = load_manifest()
    if any(e["name"].lower() == name.lower() and e["included"] for e in man["effects"]):
        raise SystemExit(f"add: an effect named '{name}' already exists on the badge; pass --name")
    slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    dst = os.path.join(COMMUNITY_DIR, slug + ".cpp")
    if os.path.exists(dst):
        raise SystemExit(f"add: {dst} exists (wledport.py remove '{name}' first)")
    lic = eff or "NOASSERTION"  # recorded in the manifest
    source = (args.source or url or os.path.basename(path)).replace(";", ",")
    spdx = None if SPDX_LINE.search(head) and not args.licence else lic
    hdr = ([f"// SPDX-License-Identifier: {spdx}\n"] if spdx else []) + [
        f"// wledport-add: name={name}; fn={fn}; data={data}\n",
        f"// wledport-add: source={source}; licence={lic}; added={datetime.date.today().isoformat()}\n",
        f"// Carved by tools/wled_port/wledport.py add from {os.path.basename(path)} (original comments kept)."
        + (f" {'; '.join(notes)}." if notes else "") + "\n",
        '#include "wled.h"\n\n']
    body, prev = [], None
    for i, l in out:
        if prev is not None and i != prev + 1:
            body.append("\n")
        body.append(l)
        prev = i
    if not body[-1].endswith("\n"):
        body.append("\n")
    reg = (f"\n// ---- wledport add: registration (read by wled_fx_init via community/wled_fx_community.h)\n"
           f'#include "wled_fx_rt.h"\nextern const wled_fx_comm_t wled_fx_comm_{slug} = {{&{fn}, {data}}};\n')
    os.makedirs(COMMUNITY_DIR, exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write("".join(hdr + body) + reg)
    community_sync(man, man["mode_count"])
    save_manifest(man)
    fid = next(e["id"] for e in man["effects"] if e.get("file") == f"community/{slug}.cpp")
    print(f"add: {name} -> components/wled_fx/community/{slug}.cpp as fx {fid}" + (f" ({'; '.join(notes)})" if notes else ""))

    # build + sweep on the host (ASan/UBSan)
    b = subprocess.run(["make", "-C", HOST_DIR, "-j8", "out/wledsim"], capture_output=True, text=True)
    if b.returncode:
        errs = [l for l in (b.stdout + b.stderr).splitlines() if "error:" in l]
        unk = sorted({m.group(1) for l in errs for m in [re.search(r"undeclared identifier '(\w+)'|"
                                                                     r"no member named '(\w+)'", l)] if m and m.group(1)})
        print("\n".join(errs[:15]), file=sys.stderr)
        msg = "host build failed" + (f": unknown dependencies {', '.join(unk)}" if unk else "")
        if args.keep:
            raise SystemExit("add: " + msg + " (kept, --keep)")
        rollback(dst, msg)
    s = subprocess.run(["make", "-C", HOST_DIR, "sweep", f"ONLY={fid}"], capture_output=True, text=True)
    print("\n".join(l for l in s.stdout.splitlines() if "manifest check" in l or "sweep" in l and "OK" in l))
    if s.returncode:
        print((s.stdout + s.stderr)[-3000:], file=sys.stderr)
        if args.keep:
            raise SystemExit("add: sweep failed (kept, --keep)")
        rollback(dst, "sanitizer sweep failed")
    print(f"add: OK. Next: idf.py build (the community list is picked up by CMake by itself).")
    return 0
