"""Report of a samply profile (Windows, ETW) of the dedicated server: the samples of its main thread whose stack holds
a root function (SV_Frame), by phase (the first of the given phase functions found from the root down), then for each
phase the functions by inclusive and by self share. Symbols from samply's .syms.json sidecar (--unstable-presymbolicate).
Usage: python -I server_report.py <profile.json.gz> <profile.json.syms.json> [top] [phase ...]"""
import bisect
import collections
import gzip
import json
import sys

prof = json.load(gzip.open(sys.argv[1]))
syms = json.load(open(sys.argv[2]))
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
ROOT = "SV_Frame"
PHASES = sys.argv[4:] or ["SV_BotFrame", "SV_RunClientCommands", "G_RunFrame", "SV_SendClientMessages", "SV_CalcPings"]

th = max((t for t in prof["threads"] if t.get("processName", "").startswith("openjkded")),
         key=lambda t: t["samples"]["length"])
libs = prof["libs"]
res_lib = th["resourceTable"]["lib"]
ft, fn, st = th["frameTable"], th["funcTable"], th["stackTable"]

symtab = {}
for d in syms["data"]:
    symtab[d["debug_name"]] = sorted((e["rva"], e["size"], syms["string_table"][e["symbol"]]) for e in d["symbol_table"])


def name_of(frame):
    r = fn["resource"][ft["func"][frame]]
    li = res_lib[r] if r is not None and r >= 0 else None
    if li is None:
        return "?"
    lib = libs[li]
    rva = ft["address"][frame]
    rows = symtab.get(lib.get("debugName"))
    if rows and rva >= 0:
        i = bisect.bisect_right(rows, (rva, 1 << 62, "")) - 1
        if i >= 0 and rows[i][0] <= rva < rows[i][0] + max(rows[i][1], 1):
            return rows[i][2].split('(')[0]
    return "[" + lib.get("name", "?") + "]"


frame_name = {}
stack_names = {}


def names(stack):
    """Function names from the leaf up to the root."""
    out = []
    k = stack
    while k is not None:
        if k in stack_names:
            out.extend(stack_names[k])
            break
        f = st["frame"][k]
        if f not in frame_name:
            frame_name[f] = name_of(f)
        out.append(frame_name[f])
        k = st["prefix"][k]
    stack_names[stack] = out
    return out


samples = th["samples"]
weights = samples.get("weight") or [1] * samples["length"]
total = 0
in_root = 0
phase_cnt = collections.Counter()
incl = collections.defaultdict(collections.Counter)
self_ = collections.defaultdict(collections.Counter)
for s, w in zip(samples["stack"], weights):
    if s is None:
        continue
    total += w
    ns = names(s)
    if ROOT not in ns:
        continue
    in_root += w
    below = ns[:len(ns) - 1 - ns[::-1].index(ROOT)]  # leaf first, the frames under the root
    phase = "other"
    for n in reversed(below):
        if n in PHASES:
            phase = n
            break
    phase_cnt[phase] += w
    leaf = below[0] if below else ROOT
    self_[phase][leaf] += w
    for n in set(below):
        incl[phase][n] += w

print(f"main thread: {total} samples, {in_root} under {ROOT} ({100.0 * in_root / total:.1f} %)")
for phase, c in phase_cnt.most_common():
    print(f"\n== {phase}: {100.0 * c / in_root:.1f} % of {ROOT}")
    print("  inclusive:")
    for n, v in incl[phase].most_common(top):
        print(f"    {100.0 * v / in_root:5.1f} %  {n}")
    print("  self:")
    for n, v in self_[phase].most_common(top):
        print(f"    {100.0 * v / in_root:5.1f} %  {n}")
