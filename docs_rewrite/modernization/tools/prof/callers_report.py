"""Callers of given functions in a samply profile of the dedicated server: for each target, its inclusive and self
share of the samples under SV_Frame and the three frames above its innermost occurrence.
Usage: python -I callers_report.py <profile.json.gz> <profile.json.syms.json> <function> ..."""
import bisect
import collections
import gzip
import json
import sys

prof = json.load(gzip.open(sys.argv[1]))
syms = json.load(open(sys.argv[2]))
TARGETS = sys.argv[3:]
ROOT = "SV_Frame"
TOP = 7

th = max((t for t in prof["threads"] if t.get("processName", "").startswith("openjkded")),
         key=lambda t: t["samples"]["length"])
libs = prof["libs"]
res_lib = th["resourceTable"]["lib"]
ft, fn, st = th["frameTable"], th["funcTable"], th["stackTable"]
symtab = {d["debug_name"]: sorted((e["rva"], e["size"], syms["string_table"][e["symbol"]]) for e in d["symbol_table"])
          for d in syms["data"]}


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
            return rows[i][2].split("(")[0]
    return "[" + lib.get("name", "?") + "]"


frame_name = {}


def names(stack):
    out = []
    k = stack
    while k is not None:
        f = st["frame"][k]
        if f not in frame_name:
            frame_name[f] = name_of(f)
        out.append(frame_name[f])
        k = st["prefix"][k]
    return out


samples = th["samples"]
weights = samples.get("weight") or [1] * samples["length"]
in_root = 0
incl_by = {t: collections.Counter() for t in TARGETS}
tot_self = collections.Counter()
tot_incl = collections.Counter()
for s, w in zip(samples["stack"], weights):
    if s is None:
        continue
    ns = names(s)
    if ROOT not in ns:
        continue
    in_root += w
    below = ns[:len(ns) - 1 - ns[::-1].index(ROOT)]
    for t in TARGETS:
        if t in below:
            i = below.index(t)
            tot_incl[t] += w
            incl_by[t][" < ".join(below[i + 1:i + 4])] += w
            if i == 0:
                tot_self[t] += w
for t in TARGETS:
    print()
    print(f"{t}: inclusive {100.0 * tot_incl[t] / in_root:.1f} %, self {100.0 * tot_self[t] / in_root:.1f} % of {ROOT}")
    for chain, v in incl_by[t].most_common(TOP):
        print(f"   {100.0 * v / in_root:5.2f} %  < {chain}")
