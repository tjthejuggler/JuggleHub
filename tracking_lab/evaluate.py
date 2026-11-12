"""Regression suite + structural diagnostics on cached detections.

Why this exists: the old scorecard compared ONE number per video (total
catches) against the user's hand count. A total can be right for the wrong
reasons — missed catches and phantom catches cancel. This runner keeps the
count comparison but adds checks that need no extra ground truth, because
juggling obeys conservation rules the event stream must satisfy:

  * every ball alternates THROW, CATCH, THROW, CATCH ...   (per-track order)
  * catches == throws (±n_balls for start/stop) in a no-drop run
  * one hand holds at most one ball in a 3-ball pattern     (hand exclusivity)
  * a caught ball was airborne first                        (flight before catch)

Usage:
  python3 -m tracking_lab.evaluate                 # tuning suite + held-out
  python3 -m tracking_lab.evaluate --sweep         # constant-sensitivity sweep
  python3 -m tracking_lab.evaluate --json out.json
Detector switch (rebuilds the cache automatically): TRACKING_MODEL_V11=1, or
TRACKING_BALL_MODEL=<path to an OpenVINO .xml> for any other model.
Held-out sessions are recorded with `python3 -m tracking_lab.verify_wizard
--heldout <tag>` and are found here automatically (see `discover_heldout`).
"""
from __future__ import annotations

import argparse
import copy
import glob
import json
import os
import sys
from collections import Counter, defaultdict

from . import detcache
from .recording import RecordingSession
from .tracker import TrackingConfig

HERE = os.path.dirname(os.path.abspath(__file__))
REC_ROOT = os.path.join(HERE, "recordings")

# (session-name suffix, user-counted catches, n_balls)
# TUNING: every tracker constant was chosen to minimise error on these six.
TUNING = [("cascade60", 60, 3), ("box30", 30, 3), ("441_30", 30, 3),
          ("shower40", 40, 3), ("columns30", 30, 3), ("flash_10", 30, 3)]
# HELD-OUT: counted by the user but never used to pick a constant. These are
# the honest generalisation numbers. Add every new counted recording HERE,
# and never tune against them.
# The two user-counted LED sessions (cascade3_glow_mixed_locked = 216,
# cascade3_glow_same = 212) are listed here so the integrity check reports
# them, but both are rejected by it: ~300 unique colour frames out of ~1800
# (each frame repeated 6x, i.e. 5 real fps). No tracker can be scored on
# them. Until new counted sessions are recorded there is NO valid held-out
# data, and nothing is known about how the tracker generalises.
HELDOUT = [("cascade3_glow_mixed_locked", 216, 3),
           ("cascade3_glow_same", 212, 3)]


def find_rec(suffix: str) -> str | None:
    hits = sorted(glob.glob(os.path.join(REC_ROOT, f"*_{suffix}")))
    return hits[-1] if hits else None


def discover_heldout() -> list:
    """Sessions recorded with `verify_wizard --heldout <tag>`: meta.json has
    heldout=true and required_catches. Aborted takes are skipped. All wizard
    patterns are 3-ball. -> [(suffix, expected, n_balls)]"""
    found = {}
    for meta_path in sorted(glob.glob(os.path.join(REC_ROOT, "*", "meta.json"))):
        try:
            with open(meta_path) as f:
                meta = json.load(f)
        except (OSError, ValueError) as e:
            print(f"WARNING: unreadable {meta_path}: {e}", file=sys.stderr)
            continue
        if not meta.get("heldout") or meta.get("aborted") \
                or not meta.get("required_catches"):
            continue
        suffix = os.path.basename(os.path.dirname(meta_path)).split("_", 2)[-1]
        found[suffix] = (suffix, int(meta["required_catches"]), 3)
    return list(found.values())


def heldout_suite() -> list:
    listed = {s for s, _e, _b in HELDOUT}
    return HELDOUT + [h for h in discover_heldout() if h[0] not in listed]


def diagnose(tracker, expected: int, n_balls: int) -> dict:
    ev = tracker.events
    catches = [e for e in ev if e["event"] == "CATCH"]
    throws = [e for e in ev if e["event"] == "THROW"]

    # --- per-track alternation -------------------------------------------
    by_track = defaultdict(list)
    for e in ev:
        by_track[e["track"]].append(e)
    catch_catch = throw_throw = 0
    flight_backed = 0          # CATCH whose previous event on that ball is a
    short_flight = 0           # THROW >= 3 frames earlier (a real airborne arc)
    for seq in by_track.values():
        prev = None
        for e in seq:
            if prev is not None and prev["event"] == e["event"]:
                if e["event"] == "CATCH":
                    catch_catch += 1
                else:
                    throw_throw += 1
            if e["event"] == "CATCH" and prev is not None \
                    and prev["event"] == "THROW":
                if e["frame"] - prev["frame"] >= 3:
                    flight_backed += 1
                else:
                    short_flight += 1
            prev = e

    # --- same-frame THROW+CATCH pairs (hand-jump "transfer" heuristic) -----
    key = Counter((e["frame"], e["track"], e["event"]) for e in ev)
    transfers = sum(1 for (f, t, k) in key
                    if k == "CATCH" and (f, t, "THROW") in key)
    carries = sum(1 for e in catches if e.get("carry"))

    # --- hand exclusivity --------------------------------------------------
    n = len(tracker.per_frame)
    double_held = 0
    for f in tracker.per_frame:
        hands = [h for s, h in zip(f["states"], f.get("hands", []))
                 if s == "HELD" and h]
        if len(hands) != len(set(hands)):
            double_held += 1

    s = tracker.summary()
    c, t = len(catches), len(throws)
    return dict(
        expected=expected, catches=c, throws=t,
        catch_err=c - expected, throw_err=t - expected, balance=c - t,
        # catches that are corroborated by an observed airborne arc of the
        # SAME ball; the remainder are inferred by heuristics
        flight_backed=flight_backed,
        unbacked=c - flight_backed,
        short_flight=short_flight,
        transfers=transfers, carries=carries,
        catch_catch=catch_catch, throw_throw=throw_throw,
        silent_releases=len(tracker.silent_releases),
        deduped=len(tracker.deduped_catches),
        double_held_pct=round(100.0 * double_held / max(n, 1), 1),
        coverage_pct=s["full_coverage_pct"], tracks_created=s["tracks_created"],
        frames=n)


_INTEGRITY: dict = {}


def integrity(suffix: str) -> dict | None:
    """Repeated-colour-frame check for a session (cached per process)."""
    if suffix not in _INTEGRITY:
        rec = find_rec(suffix)
        _INTEGRITY[suffix] = (RecordingSession(rec, load_depth=False).integrity()
                              if rec else None)
    return _INTEGRITY[suffix]


def run_one(suffix: str, expected: int, n_balls: int,
            cfg: TrackingConfig | None = None) -> dict | None:
    """-> diagnostics, or None when the session is missing or its recording
    is rejected by the integrity check (never score a broken recording)."""
    rec = find_rec(suffix)
    if rec is None:
        return None
    integ = integrity(suffix)
    if integ is not None and not integ["valid"]:
        return None
    cache = detcache.load(rec)
    tracker = detcache.replay(cache, copy.copy(cfg) if cfg else None, n_balls)
    d = diagnose(tracker, expected, n_balls)
    d["session"] = suffix
    return d


COLS = [("session", "session", 27), ("expected", "exp", 4), ("catches", "C", 4),
        ("throws", "T", 4), ("catch_err", "Cerr", 5), ("throw_err", "Terr", 5),
        ("balance", "C-T", 4), ("flight_backed", "flt", 4),
        ("unbacked", "unbk", 5), ("transfers", "xfer", 5),
        ("catch_catch", "CC", 3), ("throw_throw", "TT", 3),
        ("silent_releases", "sil", 4), ("deduped", "ddp", 4),
        ("double_held_pct", "2held%", 7), ("coverage_pct", "cov%", 6),
        ("tracks_created", "ids", 4)]


def report_rejected(suite: list) -> None:
    for suffix, expected, _b in suite:
        integ = integrity(suffix)
        if integ is None:
            print(f"  MISSING   {suffix}: no recording found")
        elif not integ["valid"]:
            print(f"  REJECTED  {suffix} (user count {expected}): "
                  f"{integ['unique']} unique colour frames of {integ['frames']} "
                  f"({integ['duplicate_pct']}% repeats, each frame x{integ['median_run']}) "
                  f"- not scored")


def table(rows: list[dict], title: str) -> None:
    print(f"\n{title}")
    if not rows:
        print("  (no valid sessions)")
        return
    print(" ".join(f"{h:>{w}s}" if k != "session" else f"{h:<{w}s}"
                   for k, h, w in COLS))
    for r in rows:
        print(" ".join(f"{str(r[k]):>{w}s}" if k != "session"
                       else f"{str(r[k]):<{w}s}" for k, _h, w in COLS))
    if rows:
        mae_c = sum(abs(r["catch_err"]) for r in rows) / len(rows)
        mae_t = sum(abs(r["throw_err"]) for r in rows) / len(rows)
        pct = 100.0 * sum(abs(r["catch_err"]) for r in rows) / \
            sum(r["expected"] for r in rows)
        bal = sum(abs(r["balance"]) for r in rows) / len(rows)
        fb = 100.0 * sum(r["flight_backed"] for r in rows) / \
            max(sum(r["catches"] for r in rows), 1)
        print(f"  mean |catch err| {mae_c:.2f}   mean |throw err| {mae_t:.2f}   "
              f"catch err {pct:.1f}% of expected   mean |C-T| {bal:.2f}   "
              f"flight-backed catches {fb:.0f}%")


def score(rows: list[dict]) -> float:
    return sum(abs(r["catch_err"]) for r in rows) / max(len(rows), 1)


def sweep(base: TrackingConfig) -> None:
    """One-at-a-time sensitivity: how far does the suite score move when a
    single constant is nudged? A flat response means the constant is safe; a
    cliff means the current value is fitted to these six videos."""
    grid = dict(
        held_radius_px=[45, 55, 65, 75, 85, 95, 110],
        held_dwell=[1, 2, 3, 4],
        throw_dwell=[2, 3, 4, 5],
        min_flight_frames=[1, 2, 3, 4],
        max_catch_speed=[300, 375, 450, 525, 600, 800],
        min_catch_gap=[0, 2, 4, 6, 8],
        held_max_occluded=[6, 8, 10, 12, 15],
        assoc_gate_px=[60, 70, 80, 90, 110],
        gravity_px_s2=[1500, 2000, 2500, 3000, 3500],
        min_hits=[2, 3, 4],
    )
    print("\nSENSITIVITY SWEEP  (mean |catch err| on the tuning six; "
          "/held-out when valid held-out sessions exist)")
    held = heldout_suite()
    for name, values in grid.items():
        cells = []
        for v in values:
            cfg = copy.copy(base)
            setattr(cfg, name, v)
            tu = [run_one(s, e, b, cfg) for s, e, b in TUNING]
            ho = [run_one(s, e, b, cfg) for s, e, b in held]
            tu = [r for r in tu if r]
            ho = [r for r in ho if r]
            mark = "*" if getattr(base, name) == v else " "
            cell = f"{v}{mark}:{score(tu):.2f}"
            if ho:
                cell += f"/{score(ho):.1f}"
            cells.append(cell)
        print(f"  {name:18s} " + "  ".join(cells))
    print("  (* = current value)")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--json", default=None)
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help="override one TrackingConfig field for this run, "
                         "e.g. --set held_radius_px=85 (repeatable)")
    args = ap.parse_args(argv)

    base = TrackingConfig()
    for item in args.set:
        name, sep, raw = item.partition("=")
        if not sep or not hasattr(base, name):
            ap.error(f"--set {item}: expected NAME=VALUE with NAME a "
                     f"TrackingConfig field")
        cur = getattr(base, name)
        try:
            val = (raw.lower() in ("1", "true", "yes") if isinstance(cur, bool)
                   else type(cur)(raw))
        except ValueError:
            ap.error(f"--set {item}: cannot read {raw!r} as {type(cur).__name__}")
        setattr(base, name, val)
        print(f"OVERRIDE {name} = {val!r} (default {cur!r}) - "
              f"results are NOT the baseline")
    held = heldout_suite()
    tuning = [r for r in (run_one(s, e, b, base) for s, e, b in TUNING) if r]
    heldout = [r for r in (run_one(s, e, b, base) for s, e, b in held) if r]
    table(tuning, "TUNING SET (constants were fitted to these — optimistic)")
    report_rejected(TUNING)
    table(heldout, "HELD-OUT SET (never tuned against — the honest number)")
    report_rejected(held)
    print("\nlegend: flt = catches preceded by an observed >=3-frame flight of "
          "the same ball; unbk = the rest (inferred);\n"
          "        xfer = same-frame THROW+CATCH hand-jump pairs; CC/TT = a "
          "ball caught/thrown twice in a row (impossible);\n"
          "        sil = HELD released with no THROW; ddp = catches swallowed "
          "by same-hand dedup; 2held% = frames with 2 balls HELD in one hand")
    if args.sweep:
        sweep(base)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(dict(tuning=tuning, heldout=heldout), f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
