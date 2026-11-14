"""Simulates the command clock of plan step 14 part 4c: the client's clock steered by the input margin the server
reports (client/cl_cgame.cpp CL_AdjustCmdDelta, server/sv_client.cpp SV_RunClientCommands), in whole milliseconds.

Prints, per case: when the first command ran on time after the connection, the ticks run with their command, without
one and the commands after their tick once settled (after 3 s), the mean and lowest margin, and how far ahead of the
server's clock the command clock ends."""
import random

TICK = 10
INT_MAX = 2**31 - 1
SLOTS, SLOT_MS, MIN_SLOTS, MAX_GAP = 64, 32, 8, 100
SPEEDUP, SLOWDOWN, MARGIN = 16, 14, 3


def to_wire(ms):
    a = abs(ms)
    w = a if a <= 64 else 64 + (a - 64 + 4) // 8
    w = min(w, 127)
    return -w if ms < 0 else w


def from_wire(w):
    a = abs(w)
    ms = a if a <= 64 else 64 + (a - 64) * 8
    return -ms if w < 0 else ms


class Window:
    def __init__(self):
        self.low = {}

    def push(self, now, sample):
        s = now // SLOT_MS
        self.low[s] = min(self.low.get(s, INT_MAX), sample)

    def sorted(self, now):
        s = now // SLOT_MS
        self.low = {k: v for k, v in self.low.items() if k > s - SLOTS}
        return sorted(self.low.values())


def run(up, down, jitter, loss, frame_ms, seconds, seed):
    rng = random.Random(seed)
    start = 5000  # the client enters the world at this real time (ms); the server's time is the real time
    # client
    delta = None
    old_cmd_time = 0
    cmds = []  # (date, delta when made)
    window = Window()
    credit = 0
    settle = 0
    last_frame = start
    sent = []  # packets: list of command dates
    # server
    newest = start
    buf = []  # (date, arrival)
    started = False
    margin_new = None
    # network
    to_server = []  # (arrival, [dates])
    to_client = []  # (arrival, serverTime, margin or None)
    stats = dict(first=None, own=0, missing=0, late=0, msum=0, mn=0, mmin=INT_MAX)

    t = start
    next_frame = start
    end = start + seconds * 1000
    while t < end:
        # server tick
        if t % TICK == 0:
            for a, dates in [p for p in to_server if p[0] <= t]:
                for c in dates:
                    if c <= newest:
                        continue
                    if c > t + 1000:
                        margin_new = c - t
                        continue
                    buf.append((c, int(a)))
                    newest = c
            to_server = [p for p in to_server if p[0] > t]
            own = False
            margin = None
            while buf and buf[0][0] <= t:
                c, a = buf.pop(0)
                if c > t - TICK:
                    own = True
                    m = t - a
                else:
                    m = min(t - (t - c) // TICK * TICK - a, -1)
                    if t > start + 3000:
                        stats["late"] += 1
                margin = m if margin is None else min(margin, m)
            if margin is not None:
                margin_new = margin
                if t > start + 3000:
                    stats["msum"] += margin
                    stats["mn"] += 1
                    stats["mmin"] = min(stats["mmin"], margin)
            if own:
                started = True
                if stats["first"] is None:
                    stats["first"] = t - start
            if t > start + 3000 and started:
                stats["own" if own else "missing"] += 1
            if rng.random() >= loss:
                to_client.append((t + down + rng.uniform(0, jitter), t, None if margin_new is None else to_wire(margin_new)))
            margin_new = None

        # client frame
        if t >= next_frame:
            next_frame += frame_ms
            msec = min(max(t - last_frame, 0), 200)
            last_frame = t
            arrived = [p for p in to_client if p[0] <= t]
            to_client = [p for p in to_client if p[0] > t]
            if arrived:
                _, snap_time, wire = max(arrived, key=lambda p: p[1])
                if delta is None:
                    delta = snap_time - TICK - t  # the drawn time
                elif wire is not None and snap_time >= settle:
                    for date, made in reversed(cmds):
                        if date <= snap_time:
                            sample = from_wire(wire) - made
                            srt = window.sorted(t)
                            if len(srt) < MIN_SLOTS or sample >= srt[len(srt) // 2] - MAX_GAP:
                                window.push(t, sample)
                            break
            if delta is not None:
                srt = window.sorted(t)
                if srt:
                    target = MARGIN - srt[1 if len(srt) >= MIN_SLOTS else 0]
                    off = abs(target - delta)
                    if off > 100 or (len(srt) < MIN_SLOTS and off > TICK):
                        delta = target
                        credit = 0
                        settle = max(t + delta, old_cmd_time) + TICK
                    else:
                        credit += msec
                        while delta < target and credit >= SPEEDUP:
                            delta += 1
                            credit -= SPEEDUP
                        while delta > target and credit >= SLOWDOWN:
                            delta -= 1
                            credit -= SLOWDOWN
                        if delta == target:
                            credit = 0
                cmd_time = max(t + delta, old_cmd_time)
                old_cmd_time = cmd_time
                tick = cmd_time - cmd_time % TICK + TICK
                last = cmds[-1][0] if cmds else tick - TICK
                if abs(tick - last) > 200:
                    last = tick - TICK
                if last < tick:
                    new = list(range(last + TICK, tick + TICK, TICK))
                    cmds.extend((d, delta) for d in new)
                    cmds = cmds[-256:]
                    sent.append(new)
                    packet = [d for p in sent[-3:] for d in p]
                    if rng.random() >= loss:
                        to_server.append((t + up + rng.uniform(0, jitter), packet))
        t += 1
    lead = delta + 0  # cmd clock - real time = cmd clock - server clock
    return stats, lead


CASES = [
    ("LAN, round trip 4 ms", dict(up=2, down=2, jitter=1, loss=0)),
    ("round trip 20 ms", dict(up=10, down=10, jitter=2, loss=0)),
    ("round trip 20 ms, jitter 0-8 ms", dict(up=10, down=10, jitter=8, loss=0)),
    ("round trip 20 ms, loss 5 %", dict(up=10, down=10, jitter=2, loss=0.05)),
    ("round trip 100 ms", dict(up=50, down=50, jitter=3, loss=0)),
    ("round trip 300 ms", dict(up=150, down=150, jitter=5, loss=0)),
    ("round trip 300 ms, loss 5 %", dict(up=150, down=150, jitter=5, loss=0.05)),
]
print(f"{'case':34} {'fps':>4} | {'first on time':>13} | {'own':>6} {'none':>5} {'late':>5} | {'margin mean':>11} {'low':>5} | {'ahead':>5}")
for name, kw in CASES:
    for fps, frame_ms in ((125, 8), (333, 3)):
        st, lead = run(frame_ms=frame_ms, seconds=60, seed=1, **kw)
        mean = st["msum"] / st["mn"] if st["mn"] else float("nan")
        first = "never" if st["first"] is None else f"{st['first']} ms"
        print(f"{name:34} {fps:>4} | {first:>13} | {st['own']:>6} {st['missing']:>5} {st['late']:>5} | {mean:>11.1f} {st['mmin']:>5} | {lead:>5}")
