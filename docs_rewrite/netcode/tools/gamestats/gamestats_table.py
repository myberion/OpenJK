"""Tables the last "game statistics" line of each gamestats.sh run (plan step 14).

Usage: python3 gamestats_table.py <directory of the runs, /tmp/gamestats by default> <tag> [<tag> ...]
Columns: saber swings, wounds, damage, saber against saber contacts (blocks), idle touches and deaths over the
measured time, then the damage per hit, the share of contacts blocked, and wounds and blocks per minute.
"""
import pathlib
import re
import sys

PATTERN = re.compile(
    r"over ([\d.]+) s, \d+ frames(?:, sv_fps \d+)?: (\d+) saber swings.*\(wounds (\d+), damage (\d+), "
    r"saber against saber (\d+), parried (\d+), locks (\d+), touches (\d+)\).* (\d+) deaths")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    root = pathlib.Path(sys.argv[1])
    print("tag          swings wounds damage blocks touch deaths | dmg/hit blockshare wounds/min blocks/min")
    for tag in sys.argv[2:]:
        log = root / tag / "base" / "qconsole.log"
        lines = [l for l in log.read_bytes().decode("latin-1").splitlines() if "game statistics" in l]
        match = PATTERN.search(lines[-1]) if lines else None
        if not match:
            print(f"{tag:12s} no result")
            continue
        seconds = float(match.group(1))
        swings, wounds, damage, blocks, parried, locks, touches, deaths = map(int, match.groups()[1:])
        minutes = seconds / 60.0
        hits = max(wounds + touches, 1)
        print(f"{tag:12s} {swings:6d} {wounds:6d} {damage:6d} {blocks:6d} {touches:5d} {deaths:6d} | "
              f"{damage / hits:7.2f} {blocks / max(blocks + wounds, 1):10.3f} {wounds / minutes:10.1f} "
              f"{blocks / minutes:10.1f}")
    return 0


sys.exit(main())
