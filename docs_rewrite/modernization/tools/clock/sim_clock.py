"""Compares the interpolation delay of plan step 11 (old) and step 14 part 4e (new) on synthetic snapshot arrivals.

Whole milliseconds, as the engine. Prints, per case: mean delay behind the newest snapshot at the frames that bring
snapshots, and the share of frames whose time passed the newest snapshot (extrapolated)."""
import random

TICK = 10
INT_MAX = 2**31 - 1


def arrivals(seconds, jitter, spike_every, spike, loss, seed):
    rng = random.Random(seed)
    out = []
    for k in range(seconds * 1000 // TICK):
        t = k * TICK
        if rng.random() < loss:
            continue
        late = rng.uniform(0, jitter)
        if spike_every and rng.random() < TICK / spike_every:
            late += spike
        out.append((t + 40 + late, t))  # arrival (real ms), serverTime
    out.sort()
    return out


class Old:
    def __init__(self):
        self.win = []  # (offset, read, lost)
        self.margin = False
        self.delta = None
        self.last_msg = None

    def frame(self, now, new, snap_time, msgs):
        if not new:
            return
        nd = snap_time - now
        if self.delta is None:
            self.delta = nd
            self.last_msg = msgs[-1]
            return
        lost = 0
        for m in msgs:
            lost += max(0, m - self.last_msg - 1)
            self.last_msg = m
        self.win.append((nd, len(msgs), lost))
        self.win = self.win[-64:]
        if len(self.win) < 2:
            return
        srt = sorted(o for o, _, _ in self.win)
        read = sum(r for _, r, _ in self.win)
        lst = sum(l for _, _, l in self.win)
        if lst * 20 > read + lst:
            self.margin = True
        elif lst * 50 < read + lst:
            self.margin = False
        target = srt[1] - TICK - (TICK if self.margin else 0)
        if self.delta < target:
            self.delta += 1
        elif self.delta > target:
            self.delta -= min(2, self.delta - target)


class New:
    def __init__(self):
        self.low = {}
        self.delta = None
        self.pending = None
        self.arrival = 0
        self.credit = 0
        self.last = 0

    def frame(self, now, new, snap_time, msgs):
        msec = min(max(now - self.last, 0), 200)
        self.last = now
        nd = snap_time - now
        if self.delta is None:
            if new:
                self.delta = nd - TICK
            return
        if new:
            if self.pending is not None and now - self.arrival <= 100:
                slot = now // 32
                self.low[slot] = min(self.low.get(slot, INT_MAX), self.pending)
                for s in [s for s in self.low if s <= slot - 64]:
                    del self.low[s]
            self.pending = nd
            self.arrival = now
        elif self.pending is not None:
            self.pending = min(self.pending, nd)
        if not self.low:
            return
        srt = sorted(self.low.values())
        target = srt[1 if len(srt) >= 8 else 0] - 2
        self.credit += msec
        while self.delta < target and self.credit >= 16:
            self.delta += 1
            self.credit -= 16
        while self.delta > target and self.credit >= 14:
            self.delta -= 1
            self.credit -= 14
        if self.delta == target:
            self.credit = 0


def run(algo, arr, frame_ms, seconds):
    i = 0
    snap_time = None
    msg = 0
    delay_sum = delay_n = extrap = frames = 0
    now = 0.0
    while now < seconds * 1000:
        now += frame_ms
        inow = int(now)
        msgs = []
        while i < len(arr) and arr[i][0] <= now:
            if snap_time is None or arr[i][1] > snap_time:
                snap_time = arr[i][1]
                msgs.append(arr[i][1] // TICK)
            i += 1
        if snap_time is None:
            continue
        if algo.delta is not None and inow > 3000:  # after the start
            frames += 1
            t = inow + algo.delta
            if t > snap_time:
                extrap += 1
            if msgs:
                delay_sum += snap_time - t
                delay_n += 1
        algo.frame(inow, bool(msgs), snap_time, msgs)
    return delay_sum / max(delay_n, 1), 100.0 * extrap / max(frames, 1)


CASES = [
    ("no jitter", dict(jitter=0, spike_every=0, spike=0, loss=0)),
    ("jitter 0-3 ms", dict(jitter=3, spike_every=0, spike=0, loss=0)),
    ("jitter 0-8 ms", dict(jitter=8, spike_every=0, spike=0, loss=0)),
    ("jitter 0-3 ms, a 25 ms spike every 5 s", dict(jitter=3, spike_every=5000, spike=25, loss=0)),
    ("jitter 0-3 ms, a 25 ms spike every 0.5 s", dict(jitter=3, spike_every=500, spike=25, loss=0)),
    ("jitter 0-3 ms, loss 0.3 %", dict(jitter=3, spike_every=0, spike=0, loss=0.003)),
    ("jitter 0-3 ms, loss 2 %", dict(jitter=3, spike_every=0, spike=0, loss=0.02)),
    ("jitter 0-3 ms, loss 9 %", dict(jitter=3, spike_every=0, spike=0, loss=0.09)),
]
SECONDS = 120
print(f"{'case':42} {'fps':>4} | {'old delay':>9} {'old extr':>8} | {'new delay':>9} {'new extr':>8}")
for name, kw in CASES:
    for fps, frame_ms in ((60, 1000 / 60), (125, 8.0), (333, 3.0)):
        arr = arrivals(SECONDS, seed=1, **kw)
        od, oe = run(Old(), arr, frame_ms, SECONDS)
        nd, ne = run(New(), arr, frame_ms, SECONDS)
        print(f"{name:42} {fps:>4} | {od:8.1f}  {oe:7.2f}% | {nd:8.1f}  {ne:7.2f}%")
