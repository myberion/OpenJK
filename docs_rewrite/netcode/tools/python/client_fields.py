"""Ideal cost of the client-to-server packets, field by field, under candidate codings (plan step 8, part 2).

Input: the text dump of netcodec_bench_client --dump (data/traces/clientpackets.txt by default). Each candidate is a
static model per context, trained on the training sessions and evaluated on them and on the held-out session; the
cost of a symbol is -log2 p with an escape for symbols the training never saw (escape probability = distinct
symbols / (count + distinct), then the raw bits). Integers with a wide range (times, angle deltas) are coded as a
bucket (0, or the sign and the bit length) through the model, plus the bits below the leading one raw.

Usage: python client_fields.py [dump.txt] [held-out file name]
"""
import math
import sys
from collections import Counter, defaultdict

DUMP = sys.argv[1] if len(sys.argv) > 1 else "../../data/traces/clientpackets.txt"
HELD_OUT = sys.argv[2] if len(sys.argv) > 2 else "client2_20261109-213533.jkcp"


def read(path):
    files, cur = {}, None
    with open(path, encoding="latin-1") as f:
        for line in f:
            t = line.rstrip("\n").split("\t")
            if t[0] == "F":
                cur = files.setdefault(t[1], [])
            elif t[0] == "P":
                v = t[1:]
                cur.append({
                    "svTime": int(v[0]), "realTime": int(v[1]), "snapTime": int(v[2]), "commandTime": int(v[3]),
                    "viewangles": [float(x) for x in v[4:7]], "deltaAngles": [int(x) for x in v[7:10]],
                    "weapon": int(v[10]), "forceSel": int(v[11]), "holdable": int(v[12]),
                    "serverId": int(v[13]), "messageAck": int(v[14]), "reliableAck": int(v[15]), "move": int(v[16]),
                    "headerBits": int(v[17]), "commandBits": int(v[18]), "moveBits": int(v[19]), "length": int(v[20]),
                    "cmds": [], "commands": []})
            elif t[0] == "U":
                cur[-1]["cmds"].append([int(x) for x in t[1:]])
            elif t[0] == "C":
                cur[-1]["commands"].append((int(t[1]), t[2] if len(t) > 2 else ""))
    return files


class Model:
    """Static model per context: train with add(), then cost() in bits."""

    def __init__(self):
        self.counts = defaultdict(Counter)

    def add(self, ctx, sym):
        self.counts[ctx][sym] += 1

    def cost(self, ctx, sym, raw_bits):
        c = self.counts.get(ctx)
        if not c:
            return raw_bits + 1.0
        n, e = sum(c.values()), len(c)
        k = c.get(sym, 0)
        if k:
            return -math.log2(k / (n + e))
        return -math.log2(e / (n + e)) + raw_bits


def bucket(d):
    if d == 0:
        return 0, 0
    b = abs(d).bit_length()
    return (b if d > 0 else -b), b - 1


class IntModel:
    """A wide integer as its bucket through a Model, plus the bits below the leading one."""

    def __init__(self):
        self.m = Model()

    def add(self, ctx, d):
        self.m.add(ctx, bucket(d)[0])

    def cost(self, ctx, d):
        s, extra = bucket(d)
        return self.m.cost(ctx, s, 6) + extra


def delta16(a, b):
    return ((a - b + 32768) & 65535) - 32768


def angle2short(x):
    return int(x * 65536 / 360) & 65535


# Candidate codings. Each is (name, component, function(packet, prev_packet, train: bool, models) -> bits); the function
# adds to the models when training and returns the cost otherwise.
CANDIDATES = []


def candidate(component, name):
    def deco(fn):
        CANDIDATES.append((component, name, fn))
        return fn
    return deco


def sym(models, key, ctx, s, raw, train):
    m = models.setdefault(key, Model())
    if train:
        m.add(ctx, s)
        return 0.0
    return m.cost(ctx, s, raw)


def wide(models, key, ctx, d, train):
    m = models.setdefault(key, IntModel())
    if train:
        m.add(ctx, d)
        return 0.0
    return m.cost(ctx, d)


# --- acknowledges --------------------------------------------------------------------------------------------------
@candidate("acknowledges", "legacy (huffman of 3 x 32 bits)")
def ack_legacy(p, q, train, m):
    return p["headerBits"]


@candidate("acknowledges", "serverId same flag, messageAck mod 32 raw, reliableAck vs previous packet")
def ack_mod32(p, q, train, m):
    bits = sym(m, "sid", 0, q is not None and p["serverId"] == q["serverId"], 32, train)
    bits += 0 if train else 5
    bits += wide(m, "rel", 0, p["reliableAck"] - (q["reliableAck"] if q else 0), train)
    return bits


@candidate("acknowledges", "reference, not robust: messageAck and reliableAck vs previous packet")
def ack_prev(p, q, train, m):
    bits = sym(m, "sid", 0, q is not None and p["serverId"] == q["serverId"], 32, train)
    bits += wide(m, "ack", 0, p["messageAck"] - (q["messageAck"] if q else 0), train)
    bits += wide(m, "rel", 0, p["reliableAck"] - (q["reliableAck"] if q else 0), train)
    return bits


# --- user commands --------------------------------------------------------------------------------------------------
def snap_angles(p):
    return [(angle2short(p["viewangles"][i]) - p["deltaAngles"][i]) & 65535 for i in range(3)]


@candidate("user commands", "legacy (count, deltas with the key, huffman)")
def cmd_legacy(p, q, train, m):
    return p["moveBits"]


def cmds_cost(p, train, m, first_time, angles, second_order, prev_ctx):
    cmds = p["cmds"]
    bits = sym(m, "move", 0, p["move"], 2, train)
    if not cmds:
        return bits
    bits += sym(m, "count", 0, len(cmds), 5, train)
    sa = snap_angles(p)
    prev_dt = None
    for i, c in enumerate(cmds):
        t = c[0]
        if i == 0:
            if first_time == "snap" and p["snapTime"] >= 0:
                bits += wide(m, "t0snap", 0, t - p["snapTime"], train)
            elif first_time == "command" and p["snapTime"] >= 0:
                bits += wide(m, "t0cmd", 0, t - p["commandTime"], train)
            else:
                bits += 0 if train else 32
        else:
            dt = t - cmds[i - 1][0]
            bits += sym(m, "dt", prev_dt if prev_ctx else 0, dt, 32, train)
            prev_dt = dt
        for a in range(3):
            if i == 0:
                base = sa[a] if angles == "snap" and p["snapTime"] >= 0 else 0
                bits += wide(m, ("a0", angles), a, delta16(c[1 + a], base), train)
            else:
                pred = cmds[i - 1][1 + a]
                ctx = a
                if second_order and i >= 2:
                    pred = (2 * cmds[i - 1][1 + a] - cmds[i - 2][1 + a]) & 65535
                    ctx = (a, "2")
                elif prev_ctx and i >= 2:
                    ctx = (a, bucket(delta16(cmds[i - 1][1 + a], cmds[i - 2][1 + a]))[0])
                bits += wide(m, ("a", second_order, prev_ctx), ctx, delta16(c[1 + a], pred), train)
        # buttons, weapon, forcesel, invensel, generic_cmd, forwardmove, rightmove, upmove
        if i == 0:
            prevf = [0, p["weapon"], p["forceSel"] & 255, p["holdable"] & 255, 0, 0, 0, 0]
        else:
            prevf = cmds[i - 1][4:12]
        for k, (v, pv) in enumerate(zip(c[4:12], prevf)):
            bits += sym(m, ("f", k, i == 0), pv, v, 16 if k == 0 else 8, train)
    return bits


@candidate("user commands", "absolute first command (as legacy), deltas, per-field models")
def cmd_plain(p, q, train, m):
    return cmds_cost(p, train, m, "raw", "zero", False, False)


@candidate("user commands", "first command vs acknowledged snapshot (time and angles), deltas")
def cmd_snap(p, q, train, m):
    return cmds_cost(p, train, m, "snap", "snap", False, False)


@candidate("user commands", "first time vs snapshot commandTime, angles vs snapshot, deltas")
def cmd_snapcmd(p, q, train, m):
    return cmds_cost(p, train, m, "command", "snap", False, False)


@candidate("user commands", "vs snapshot, contexts: previous time step and previous angle delta")
def cmd_ctx(p, q, train, m):
    return cmds_cost(p, train, m, "snap", "snap", False, True)


@candidate("user commands", "vs snapshot, angles to second order")
def cmd_second(p, q, train, m):
    return cmds_cost(p, train, m, "snap", "snap", True, False)


# --- client commands ------------------------------------------------------------------------------------------------
@candidate("client commands", "legacy (huffman)")
def cc_legacy(p, q, train, m):
    return p["commandBits"]


@candidate("client commands", "presence flag, text order-1 bytes (seq implied)")
def cc_order1(p, q, train, m):
    bits = sym(m, "ncc", 0, len(p["commands"]), 8, train)
    for _seq, text in p["commands"]:
        prev = 0
        for ch in text + "\0":
            bits += sym(m, "cc", prev, ch, 8, train)
            prev = ch
    return bits


def run(files):
    train_files = [n for n in files if n != HELD_OUT]
    print(f"training: {len(train_files)} sessions; held out: {HELD_OUT}")
    results = []
    for component, name, fn in CANDIDATES:
        models = {}
        for n in train_files:
            q = None
            for p in files[n]:
                fn(p, q, True, models)
                q = p
        row = [component, name]
        for group in (train_files, [HELD_OUT]):
            total, count = 0.0, 0
            for n in group:
                q = None
                for p in files[n]:
                    total += fn(p, q, False, models)
                    count += 1
                    q = p
            row.append(total / count / 8)
        results.append(row)
    width = max(len(r[1]) for r in results)
    print(f"\n{'':16} {'candidate':{width}}  bytes per packet: training  held out")
    for r in results:
        print(f"{r[0]:16} {r[1]:{width}}  {r[2]:26.2f}  {r[3]:8.2f}")
    legacy = sum(r[3] for r in results if r[1].startswith("legacy"))
    print(f"\nlegacy held out: {legacy:.2f} bytes per packet (without the unused bits of the last byte)")


if __name__ == "__main__":
    run(read(DUMP))
