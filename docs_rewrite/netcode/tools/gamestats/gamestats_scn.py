"""Compares the statistics of scripted scenarios (plan step 14) between two runs of gamestats_windows.sh or gamestats.sh,
for example the same module at sv_fps 20 and 100: prints SAME, or the lines that differ.
Usage: python -I gamestats_scn.py <directory of the runs> <tagA> <tagB> [<tagA> <tagB> ...]
(runs in <directory>/gs_<tag> for the Windows script, <directory>/<tag> for the Linux one). The statistics are the
lines printed after "scenario done"; the events left by the free play before the scenario (bodies fading, items
respawning, doors) and the death animation numbers (cycled by a static counter in player_die) are ignored."""
import re
import sys
from pathlib import Path

D = Path(sys.argv[1])
IGNORE = re.compile(r" (EV_BODYFADE|EV_ITEM_RESPAWN|EV_PLAYDOORSOUND|EV_DEATH[123]) \d+")
KEEP = ("saber swings", "force statistics", "event statistics", "damage statistics", "shot statistics",
        "style statistics")


def stats(tag):
    log = D / f"gs_{tag}" / "base" / "qconsole.log"
    if not log.exists():
        log = D / tag / "base" / "qconsole.log"
    lines = log.read_bytes().decode("latin-1").splitlines()
    done = [k for k, l in enumerate(lines) if "scenario done" in l]
    if not done:
        sys.exit(f"{log}: no 'scenario done' (the run ended before the repetitions)")
    out = []
    for l in lines[done[-1] + 1:done[-1] + 8]:
        l = re.sub(r"^\S+ \S+ ", "", l)  # time stamp of logfile 2
        l = re.sub(r"game statistics over [^:]*:", "", l)  # duration and frame count differ with the rate
        if any(k in l for k in KEEP):
            out.append(IGNORE.sub("", l))
    return out


args = sys.argv[2:]
for a, b in zip(args[0::2], args[1::2]):
    sa, sb = stats(a), stats(b)
    print(f"{a} / {b}: {'SAME' if sa == sb else 'DIFF'}")
    for x, y in zip(sa, sb):
        if x != y:
            print(f"  {a}: {x}")
            print(f"  {b}: {y}")
