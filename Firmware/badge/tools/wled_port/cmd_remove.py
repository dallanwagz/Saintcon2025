"""wledport remove - drop an effect brought in with 'add' (by name, slug or fx id) and renumber the rest."""
import os

from wp_common import COMPONENT_DIR, community_scan, community_sync, load_manifest, save_manifest

HELP = "remove a community effect added with 'add' (name, file slug or fx id)"


def add_args(p):
    p.add_argument("effect")


def run(args):
    man = load_manifest()
    key = args.effect.lower()
    ids = {e["file"]: e["id"] for e in man["effects"] if e.get("bucket") == "COMMUNITY"}
    hit = [e for e in community_scan() if key in (e["name"].lower(), e["slug"], str(ids.get(e["file"])))]
    if not hit:
        raise SystemExit(f"remove: no community effect '{args.effect}' (have: "
                         + ", ".join(e["name"] for e in community_scan()) + ")")
    os.remove(os.path.join(COMPONENT_DIR, hit[0]["file"]))
    community_sync(man, man["mode_count"])
    save_manifest(man)
    print(f"remove: {hit[0]['name']} ({hit[0]['file']}) removed; later community effects moved down one id. "
          "Re-run 'wledport.py presets' if a preset used them, then idf.py build.")
    return 0
