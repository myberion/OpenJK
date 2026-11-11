import pathlib
import re
import sys

# Sums the last "game statistics" line of several runs per group and prints rates per minute.
# Usage: python -I gamestats_agg.py <directory of the runs> name=tag1,tag2 name2=tag3,tag4 ...
# (runs of gamestats_windows.sh in <directory>/gs_<tag>; the first group is the reference of the percentages)
S = pathlib.Path(sys.argv[1])
rx = re.compile(r"over ([\d.]+) s, \d+ frames, sv_fps (\d+): (\d+) saber swings.*\(wounds (\d+), damage (\d+), "
                r"saber against saber (\d+), parried (\d+), locks (\d+), touches (\d+)\).* (\d+) deaths")
ref = None
print("group        min  damage/min deaths/min blocks/min wounds/min effects/min | vs first group: damage deaths blocks")
fx = re.compile(r"(\d+) saber hit effects")
for arg in sys.argv[2:]:
    name, tags = arg.split("=")
    tot = [0.0] * 10
    effects = 0
    have_fx = True
    for t in tags.split(","):
        lines = [l for l in (S / f"gs_{t}" / "base" / "qconsole.log").read_bytes().decode("latin-1").splitlines()
                 if "game statistics" in l]
        m = rx.search(lines[-1])
        vals = [float(m.group(1))] + [int(x) for x in m.groups()[1:]]
        tot = [a + b for a, b in zip(tot, vals)]
        f = fx.search(lines[-1])
        if f:
            effects += int(f.group(1))
        else:
            have_fx = False
    minutes = tot[0] / 60.0
    rates = (tot[4] / minutes, tot[9] / minutes, tot[5] / minutes, tot[3] / minutes)
    if ref is None:
        ref = rates
    rel = [f"{(r / b - 1) * 100:+5.1f}%" for r, b in zip(rates[:3], ref[:3])]
    print(f"{name:12s} {minutes:4.0f} {rates[0]:10.0f} {rates[1]:10.1f} {rates[2]:10.1f} {rates[3]:10.1f} " + (f"{effects / minutes:11.1f}" if have_fx else "          -") + " | "
          + " ".join(rel))
