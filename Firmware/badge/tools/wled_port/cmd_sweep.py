"""wledport sweep - build components/wled_fx for the host with ASan/UBSan and render every effect."""
import subprocess

from wp_common import HOST_DIR

HELP = "host sanitizer sweep of every effect (make -C host sweep), plus the golden-frame check"


def add_args(p):
    p.add_argument("--no-golden", action="store_true", help="skip the golden-frame comparison")
    p.add_argument("-j", type=int, default=8, help="parallel compile jobs")


def run(args):
    targets = ["sweep"] + ([] if args.no_golden else ["check"])
    return subprocess.call(["make", "-C", HOST_DIR, f"-j{args.j}", *targets])
