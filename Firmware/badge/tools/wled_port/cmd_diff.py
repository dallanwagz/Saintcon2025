"""wledport diff - compare the effect id/name tables of two WLED versions (or the badge manifest, or a strip).

A side (--from / --to) is one of:
  manifest            components/wled_fx/manifest.json (default for --from)
  <path>.json         another manifest
  <dir>               a WLED checkout (repo root or wled00/), or a snapshot dir (has eff.json)
  <tag or ref>        a git ref in --repo (e.g. v16.0.1, main), read with git show (no checkout)
  gh:<ref>            FX.cpp/FX.h of github.com/wled/WLED at <ref>, downloaded to a temp dir
--presets <json> additionally flags wled_presets*.json entries whose fx id would change name.
"""
import json
import os
import subprocess
import tempfile
import urllib.request

from wp_common import MANIFEST
from cmd_import import classify

HELP = "effect id/name changes between two WLED versions, the badge manifest or a strip snapshot"


def add_args(p):
    p.add_argument("--from", dest="a", default="manifest")
    p.add_argument("--to", dest="b", required=True)
    p.add_argument("--repo", help="WLED git checkout for tag/ref sides")
    p.add_argument("--presets", help="wled_presets*.json to check against the change")


def from_dir(w):
    fx, _ = classify(w)
    return {e["id"]: e["name"] for e in fx}


def side(spec, repo):
    if spec == "manifest":
        spec = MANIFEST
    if spec.endswith(".json") and os.path.isfile(spec):
        with open(spec, encoding="utf-8") as f:
            return {e["id"]: e["name"] for e in json.load(f)["effects"]}, spec
    if os.path.isdir(spec):
        if os.path.isfile(os.path.join(spec, "eff.json")):   # strip snapshot: list index = id
            with open(os.path.join(spec, "eff.json"), encoding="utf-8") as f:
                return {i: n for i, n in enumerate(json.load(f)) if n and n != "RSVD"}, spec
        w = os.path.join(spec, "wled00") if os.path.isdir(os.path.join(spec, "wled00")) else spec
        return from_dir(w), spec
    tmp = tempfile.mkdtemp(prefix="wledport-diff-")
    for f in ("FX.cpp", "FX.h"):
        if spec.startswith("gh:"):
            url = f"https://raw.githubusercontent.com/wled/WLED/{spec[3:]}/wled00/{f}"
            data = urllib.request.urlopen(url, timeout=30).read()
        else:
            if not repo:
                raise SystemExit(f"diff: '{spec}' is not a file/dir; pass --repo <WLED checkout> for git refs")
            data = subprocess.run(["git", "-C", repo, "show", f"{spec}:wled00/{f}"], capture_output=True,
                                  check=True).stdout
        with open(os.path.join(tmp, f), "wb") as o:
            o.write(data)
    return from_dir(tmp), spec


def run(args):
    a, an = side(args.a, args.repo)
    b, bn = side(args.b, args.repo)
    ra = {n.lower(): i for i, n in a.items()}
    rb = {n.lower(): i for i, n in b.items()}
    print(f"diff {an} -> {bn}: {len(a)} -> {len(b)} effects")
    changed = 0
    for i in sorted(set(a) | set(b)):
        x, y = a.get(i), b.get(i)
        if x == y:
            continue
        changed += 1
        if x and y:
            print(f"  ~ fx {i}: '{x}' -> '{y}'" + (f"  ('{x}' now fx {rb[x.lower()]})" if x.lower() in rb else ""))
        elif y:
            print(f"  + fx {i}: '{y}'" + (f"  (was fx {ra[y.lower()]})" if y.lower() in ra else ""))
        else:
            print(f"  - fx {i}: '{x}'" + (f"  (now fx {rb[x.lower()]})" if x.lower() in rb else ""))
    moved = [n for n in ra if n in rb and ra[n] != rb[n]]
    print(f"  {changed} id(s) differ, {len(moved)} effect(s) moved, "
          f"{len(set(rb) - set(ra))} added, {len(set(ra) - set(rb))} removed (by name)")
    if args.presets:
        with open(args.presets, encoding="utf-8") as f:
            doc = json.load(f)
        bad = 0
        for e in doc.get("entries", []):
            fx = e.get("params", {}).get("fx") if e.get("action") == "preset" else None
            if fx is not None and a.get(fx) != b.get(fx):
                bad += 1
                new = rb.get((a.get(fx) or "").lower())
                print(f"  preset '{e['menu']}'{' (manual)' if e.get('manual') else ''}: fx {fx} '{a.get(fx)}' -> "
                      f"{'fx %d' % new if new is not None else 'gone'}")
        print(f"  presets: {bad or 'no'} entr{'y' if bad == 1 else 'ies'} affected"
              + (" (re-run 'wledport.py presets'; fix manual entries by hand)" if bad else ""))
    return 0
