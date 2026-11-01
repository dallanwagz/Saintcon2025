#!/usr/bin/env python3
"""wledport - porting engine for the badge's WLED effect runtime (components/wled_fx).

Usage: wledport.py <command> [args]     (python3, stdlib only)

Every command lives in its own module next to this file, named cmd_<command>.py, and exposes
    HELP = "one line"
    def add_args(parser): ...
    def run(args) -> int
so new commands are added by dropping in a file; this dispatcher never needs editing.
"""
import argparse
import importlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="wledport.py", description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for fn in sorted(os.listdir(HERE)):
        if fn.startswith("cmd_") and fn.endswith(".py"):
            mod = importlib.import_module(fn[:-3])
            p = sub.add_parser(fn[4:-3].replace("_", "-"), help=getattr(mod, "HELP", ""))
            mod.add_args(p)
            p.set_defaults(_run=mod.run)
    args = ap.parse_args(argv)
    return args._run(args) or 0


if __name__ == "__main__":
    sys.exit(main())
