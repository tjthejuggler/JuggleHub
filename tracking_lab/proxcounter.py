"""Online 3D proximity catch counter (validated in catchmine.py, 2026-11-13).

Physics instead of frame-dwell state machines: a catch is a ball ARRIVING at
a hand. Deproject balls and wrists into camera-frame metres with depth, then
per hand:
  1. latch when nearest-ball distance (any class) drops < PROX (0.25 m),
     tracking the deepest minimum while it stays near,
  2. emit when it rises again: CONFIRMED only if a class-0 (in-flight) ball
     came within ARRIVE (0.45 m) during the approach (snapshotted at latch —
     by emit time the ball has flipped to class 1 'held'), which kills
     held-ball double-dips,
  3. cascade constraint: a same-hand event with NO opposite-hand event
     between it and the previous same-hand event, within ALT_WIN (0.60 s),
     collapses to the deeper minimum.

Offline evidence (4 labelled live takes, truth 50 each): tracker state
machine 22/8/31/39; the offline miner 47/41/48/55 at arrive=0.45 (mean
|err| ~5 vs ~27). Constants sit on a plateau: the prox threshold
(0.20-0.30 m) changes nothing — the minima are deep (~8 cm).
"""
from __future__ import annotations

import time
from collections import deque

PROX = 0.25      # m; latch threshold (all-class nearest ball)
RISE = 0.06      # m; must rise this far above the min before emitting
ARRIVE = 0.45    # m; class-0 ball must have come this close during approach
FLIGHT_WIN = 0.5  # s lookback for the arrival confirmation
STUCK = 1.0      # s; emit anyway if latched this long (held at hand)
MIN_GAP = 0.30   # s; same-hand duplicate suppression
ALT_WIN = 0.60   # s; cascade alternation window

HANDS = ("left_wrist", "right_wrist")


class _Hand:
    def __init__(self):
        self.prev_d = None            # previous frame's d_all (for minima)
        self.prev_t = 0.0
        self.falling = False
        self.flight_win: deque[tuple[float, float]] = deque()  # (t, d_flight)


class ProxCounter:
    """Feed update(t, samples) every processed frame; samples maps hand ->
    (d_all | None, d_flight | None) in metres (distance to nearest ball)."""

    def __init__(self, prox=PROX, arrive=ARRIVE):
        self.prox, self.arrive = prox, arrive
        self.hands = {h: _Hand() for h in HANDS}
        self.events: list[dict] = []      # {t, hand, d}

    # ------------------------------------------------------------- helpers
    def _prune(self, h: _Hand, t: float):
        while h.flight_win and t - h.flight_win[0][0] > FLIGHT_WIN:
            h.flight_win.popleft()

    def _flight_min(self, h: _Hand) -> float:
        vals = [d for _t, d in h.flight_win if d is not None]
        return min(vals) if vals else float("inf")

    def _emit(self, hand: str, t: float, d: float):
        """Record a raw candidate minimum and reconcile the event list with
        the SAME multi-pass logic the offline miner uses post-hoc (cross-hand
        merge, same-hand duplicate collapse, cascade alternation) — one-pass
        greedy suppression under-counted chains on one take by +12."""
        self.events.append(dict(t=t, hand=hand, d=round(d, 3)))
        self.events.sort(key=lambda e: e["t"])
        evs = self.events
        # 1. cross-hand merge: same arrival seen on both wrists
        merged = []
        for ev in evs:
            if merged and ev["t"] - merged[-1]["t"] < MIN_GAP * 0.5:
                if ev["d"] < merged[-1]["d"]:
                    merged[-1] = ev
            else:
                merged.append(ev)
        evs = merged
        # 2. cascade alternation, repeated until stable (offline parity)
        changed = True
        while changed:
            changed = False
            for a in range(len(evs) - 1):
                b = a + 1
                if (evs[b]["t"] - evs[a]["t"] < ALT_WIN
                        and evs[b]["hand"] == evs[a]["hand"]):
                    drop = a if evs[a]["d"] >= evs[b]["d"] else b
                    evs.pop(drop)
                    changed = True
                    break
        self.events = evs

    # ---------------------------------------------------------------- api
    def update(self, t: float, samples: dict[str, tuple]) -> list[dict]:
        """One processed frame. Returns events emitted this frame.

        Local-minimum detection with one-frame delay (identical to the
        offline miner): while d falls we note it; the first frame where d
        RISES again closes the minimum at the previous frame. A hand in a
        cascade has SOME ball near it almost continuously (p50 0.14 m), so
        episode-latching would merge catches — only per-frame minima work.
        """
        n0 = len(self.events)
        for hand, h in self.hands.items():
            d_all, d_flight = samples.get(hand, (None, None))
            h.flight_win.append((t, d_flight))
            self._prune(h, t)
            prev = h.prev_d
            h.prev_d, h.prev_t = d_all, t
            if d_all is None or prev is None:
                h.falling = False
                continue
            if d_all < prev:
                h.falling = True
                continue
            if d_all > prev and h.falling and prev <= self.prox:
                h.falling = False
                # CONFIRM: a class-0 ball approached this hand recently
                # (the ball now at the hand may already read class 1 'held')
                if self._flight_min(h) <= self.arrive:
                    self._emit(hand, h.prev_t, prev)
        return self.events[n0:]

    @property
    def count(self) -> int:
        return len(self.events)

    def side_counts(self) -> tuple[int, int]:
        return (sum(1 for e in self.events if e["hand"] == "left_wrist"),
                sum(1 for e in self.events if e["hand"] == "right_wrist"))


def deproject(px, py, z, w, h, fx, fy):
    import numpy as np
    return np.array([(px - w / 2.0) * z / fx, (py - h / 2.0) * z / fy, z])


def make_intrinsics(w: int, h: int):
    import numpy as np
    fx = (w / 2.0) / np.tan(np.radians(69.4) / 2.0)   # D435 color HFOV
    return fx, fx * h / w


def _robust_depth_fn(depth, rgb_shape):
    """(x, y) -> metres | None; single pixel first, then the min valid value
    in a 5x5 neighbourhood (wrists sit at depth edges / holes ~40% of the
    time on fast takes, which starved the live counter)."""
    if depth is None:
        return None
    import numpy as np
    dh, dw = depth.shape[:2]
    sy, sx = rgb_shape[0] / dh, rgb_shape[1] / dw

    def depth_at(x, y):
        xi, yi = int(x / sx), int(y / sy)
        if not (0 <= yi < dh and 0 <= xi < dw):
            return None
        v = float(depth[yi, xi])
        if v > 0:
            return v / 1000.0
        y0, y1 = max(0, yi - 2), min(dh, yi + 3)
        x0, x1 = max(0, xi - 2), min(dw, xi + 3)
        patch = depth[y0:y1, x0:x1]
        patch = patch[patch > 0]
        return float(patch.min()) / 1000.0 if patch.size else None
    return depth_at


def prox_samples(dets_raw, depth, rgb_shape, wrists, w, h, fx, fy):
    """Distance samples for one frame: hand -> (d_all, d_flight) in metres."""
    import numpy as np
    out: dict[str, tuple] = {}
    depth_fn = _robust_depth_fn(depth, rgb_shape)
    if depth_fn is None:
        return out
    balls = []
    for box, score, cls in dets_raw:
        cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
        z = depth_fn(cx, cy)
        if z is not None:
            balls.append((deproject(cx, cy, z, w, h, fx, fy), cls))
    if not balls:
        return out
    for hand, (wx, wy) in wrists.items():
        wz = depth_fn(wx, wy)
        if wz is None:
            continue
        hp = deproject(wx, wy, wz, w, h, fx, fy)
        ds = [float(np.linalg.norm(b - hp)) for b, _c in balls]
        fl = [d for d, (_b, c) in zip(ds, balls) if c == 0]
        out[hand] = (min(ds), min(fl) if fl else None)
    return out


if __name__ == "__main__":
    # sanity: a synthetic far->near->far arrival on the left hand
    pc = ProxCounter()
    t0 = time.perf_counter()
    for i in range(20):
        d = 1.0 if i < 5 else (0.10 if i < 9 else (0.5 if i < 12 else 1.0))
        pc.update(t0 + i * 0.06,
                  {"left_wrist": (d, 1.0 if i < 6 else d),
                   "right_wrist": (1.0, None)})
    assert pc.count == 1, pc.events
    print("selftest OK:", pc.events)
