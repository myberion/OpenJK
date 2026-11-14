import pathlib
import re
import sys

# Sums the last "game statistics" line of several runs per group and prints rates per minute.
# Usage: python -I gamestats_agg.py <directory of the runs> name=tag1,tag2 name2=tag3,tag4 ...
# (runs of gamestats_windows.sh in <directory>/gs_<tag>; the first group is the reference of the percentages)
# When the runs print "force statistics" (since the force powers of plan step 14), a second table gives, for each
# power whose timing depended on the frame rate, its starts per minute and its periodic events (drains, rage health
# losses, shots) per start and per second held.
S = pathlib.Path(sys.argv[1])
rx = re.compile(r"over ([\d.]+) s, \d+ frames(?:, sv_fps \d+)?: (\d+) saber swings.*\(wounds (\d+), damage (\d+), "
                r"saber against saber (\d+), parried (\d+), locks (\d+), touches (\d+)\).* (\d+) force damage, (\d+) deaths")
fx = re.compile(r"(\d+) saber hit effects")
fp = re.compile(r"(grip|rage|protect|absorb|lightning|drain) (\d+) (\d+) (\d+)")
POWERS = ("grip", "rage", "protect", "absorb", "lightning", "drain")
ref = None
force_rows = []
print("group        min  damage/min deaths/min blocks/min wounds/min effects/min force/min | vs first group: damage deaths blocks force")
for arg in sys.argv[2:]:
    name, tags = arg.split("=")
    tot = [0.0] * 10
    effects = 0
    have_fx = True
    force = {p: [0, 0, 0] for p in POWERS}  # starts, events, milliseconds held
    have_force = True
    for t in tags.split(","):
        lines = (S / f"gs_{t}" / "base" / "qconsole.log").read_bytes().decode("latin-1").splitlines()
        game = [l for l in lines if "game statistics" in l]
        m = rx.search(game[-1])
        vals = [float(m.group(1))] + [int(x) for x in m.groups()[1:]]
        tot = [a + b for a, b in zip(tot, vals)]
        f = fx.search(game[-1])
        if f:
            effects += int(f.group(1))
        else:
            have_fx = False
        flines = [l for l in lines if "force statistics: starts" in l]
        if flines:
            for p, starts, events, msec in fp.findall(flines[-1]):
                force[p] = [a + int(b) for a, b in zip(force[p], (starts, events, msec))]
        else:
            have_force = False
    minutes = tot[0] / 60.0
    rates = (tot[3] / minutes, tot[9] / minutes, tot[4] / minutes, tot[8] / minutes, tot[2] / minutes)
    if ref is None:
        ref = rates
    rel = [f"{(r / b - 1) * 100:+5.1f}%" if b else "    -" for r, b in zip(rates[:4], ref[:4])]
    print(f"{name:12s} {minutes:4.0f} {rates[0]:10.0f} {rates[1]:10.1f} {rates[2]:10.1f} {rates[4]:10.1f} "
          + (f"{effects / minutes:11.1f}" if have_fx else "          -") + f" {rates[3]:9.1f} | " + " ".join(rel))
    if have_force:
        force_rows.append((name, minutes, force))
if force_rows:
    print("\nper power: starts per minute, events per start, events per second held")
    print("group       " + "".join(f"{p:>24s}" for p in POWERS))
    for name, minutes, force in force_rows:
        cells = []
        for p in POWERS:
            starts, events, msec = force[p]
            if starts:
                cells.append(f"{starts / minutes:7.1f} {events / starts:7.2f} {events * 1000.0 / max(msec, 1):7.2f}")
            else:
                cells.append(f"{'-':>23s}")
        print(f"{name:12s}" + " ".join(cells))
