"""Per-event catch scorer (T3 Part B).

Scores tracker CATCH events against human-marked ground truth
(tracking_lab/mark_catches.py output) instead of only comparing totals, so
missed catches and phantom catches can no longer cancel each other.

Method (fixed, do not tune to improve a score):
  * tracker CATCH hand -> picture side via that frame's wrists in
    tracker.per_frame: the wrist with the smaller x is side L. With fewer
    than two wrists visible the side is UNKNOWN; such a catch matches on
    frame alone and is counted separately.
  * one-to-one matching in time order: nearest unmatched true catch of the
    same side within +-4 frames (window auto-centred on the median tracker
    delay when that median is >= 3 — the tracker reports a catch held_dwell
    frames late by design).
  * wrong-side = a true catch lies within the window but on the other side.

Usage:
  python3 -m tracking_lab.event_score box30 [more suffixes...]
  python3 -m tracking_lab.event_score            # every marked session
  python3 -m tracking_lab.event_score --selftest # headless matcher test

Mark a session first: python3 -m tracking_lab.mark_catches box30
"""
from __future__ import annotations

import statistics
import sys

import numpy as np

from . import detcache, evaluate
from .mark_catches import load_marks

WINDOW = 4          # +- frames for a same-side match (never widen)
RECENTER_MIN = 3    # if median tracker delay >= this, centre window on it


def catch_side(tracker, ev: dict, by_frame: dict) -> str | None:
    """Picture side of a tracker CATCH, or None when unknowable."""
    pf = by_frame.get(ev["frame"])
    if pf is None:
        return None
    wrists = pf.get("wrists") or {}
    if ev["hand"] not in wrists or len(wrists) < 2:
        return None
    left_name = min(wrists, key=lambda k: wrists[k][0])
    return "L" if ev["hand"] == left_name else "R"


def match_events(tracker_catches: list, true_catches: list,
                 window: int = WINDOW) -> dict:
    """One-to-one same-side matching in time order.

    tracker_catches: [{"frame", "side"}] (side may be None = unknown)
    true_catches:    [{"frame", "side"}]
    """
    unmatched = [dict(t) for t in true_catches]
    offsets, matched, unknown, phantom = [], [], 0, []

    for tc in sorted(tracker_catches, key=lambda e: e["frame"]):
        cands = [t for t in unmatched
                 if (tc["side"] is None or t["side"] == tc["side"])
                 and abs(t["frame"] - tc["frame"]) <= window]
        if not cands:
            phantom.append(tc)
            continue
        best = min(cands, key=lambda t: (abs(t["frame"] - tc["frame"]),))
        unmatched.remove(best)
        matched.append((tc, best))
        offsets.append(tc["frame"] - best["frame"])
        if tc["side"] is None:
            unknown += 1

    # wrong-side diagnostic on what is left over
    wrong_side = 0
    for tc in phantom:
        if tc["side"] is None:
            continue
        if any(abs(t["frame"] - tc["frame"]) <= window for t in unmatched):
            wrong_side += 1

    return dict(n_true=len(true_catches), n_tracker=len(tracker_catches),
                matched=matched, missed=unmatched, phantom=phantom,
                wrong_side=wrong_side,
                # matched pairs whose tracker side was unknown (frame-only
                # match): how much of the score lacks side evidence
                unknown_side=unknown,
                offsets=offsets,
                median=int(round(statistics.median(offsets)))
                if offsets else None)


def score_session(suffix: str, n_balls: int = 3,
                  window: int = WINDOW,
                  cfg: "TrackingConfig | None" = None) -> dict | None:
    """-> per-event score dict, or None when unscorable (missing/broken/
    unmarked). Mirrors evaluate.run_one's integrity handling (never score a
    recording the integrity check rejects)."""
    true_marks = load_marks(suffix)
    if not true_marks:
        return None
    rec = evaluate.find_rec(suffix)
    if rec is None:
        return None
    integ = evaluate.integrity(suffix)
    if integ is not None and not integ["valid"]:
        return None
    cache = detcache.load(rec)
    tracker = detcache.replay(cache, cfg, n_balls)
    by_frame = {f["frame"]: f for f in tracker.per_frame}

    tr = [dict(frame=e["frame"], side=catch_side(tracker, e, by_frame),
               track=e.get("track"))
          for e in tracker.events if e["event"] == "CATCH"]

    res = match_events(tr, true_marks, window)
    # tracker reports catches a few frames late by design (held_dwell); if
    # the observed delay reaches RECENTER_MIN, centre the window on it once.
    if res["median"] is not None and abs(res["median"]) >= RECENTER_MIN:
        shift = res["median"]
        shifted = [dict(frame=t["frame"] + shift, side=t["side"])
                   for t in true_marks]
        res = match_events(tr, shifted, window)
        res["median_shift"] = shift   # delay measured pre-shift
        res["median"] = shift         # report the true tracker delay
    res["session"] = suffix
    res["tracker_catches"] = tr
    return res


def report(rows: list) -> None:
    print("\nPER-EVENT CATCH SCORE (ground truth vs tracker)")
    hdr = (f"{'session':<28}{'true':>5}{'trk':>5}{'match':>6}{'miss':>5}"
           f"{'phant':>6}{'wside':>6}{'ukside':>7}{'medoff':>7}")
    print(hdr)
    tot = dict(true=0, trk=0, match=0, miss=0, ph=0, ws=0, uk=0)
    for r in rows:
        print(f"{r['session']:<28}{r['n_true']:>5}{r['n_tracker']:>5}"
              f"{len(r['matched']):>6}{len(r['missed']):>5}"
              f"{len(r['phantom']) - r['wrong_side']:>6}{r['wrong_side']:>6}"
              f"{r['unknown_side']:>7}"
              f"{r['median'] if r['median'] is not None else '-':>7}")
        tot["true"] += r["n_true"]; tot["trk"] += r["n_tracker"]
        tot["match"] += len(r["matched"]); tot["miss"] += len(r["missed"])
        tot["ph"] += len(r["phantom"]) - r["wrong_side"]
        tot["ws"] += r["wrong_side"]; tot["uk"] += r["unknown_side"]
    if rows:
        print(f"{'TOTAL':<28}{tot['true']:>5}{tot['trk']:>5}{tot['match']:>6}"
              f"{tot['miss']:>5}{tot['ph']:>6}{tot['ws']:>6}{tot['uk']:>7}")
        print(f"\nmatched {tot['match']}/{tot['true']} true catches "
              f"({100 * tot['match'] / max(tot['true'], 1):.0f}%), "
              f"missed {tot['miss']}, phantom {tot['ph'] + tot['ws']} "
              f"(of which wrong-side {tot['ws']})")
    print("\nphantom excludes wrong-side hits (double-counted in no column);"
          "\nmedoff = median(tracker frame - true frame); negative = early.")


def _selftest() -> int:
    true = [{"frame": 100, "side": "L"}, {"frame": 110, "side": "R"},
            {"frame": 120, "side": "L"}]
    trk = [{"frame": 102, "side": "L"},    # match, +2
           {"frame": 112, "side": "L"},    # no L within 4 -> wrong-side vs 110R
           {"frame": 123, "side": "L"},    # match, +3
           {"frame": 200, "side": "R"}]    # phantom
    r = match_events(trk, true)
    assert len(r["matched"]) == 2, r
    assert len(r["missed"]) == 1 and r["missed"][0]["frame"] == 110, r
    assert len(r["phantom"]) == 2 and r["wrong_side"] == 1, r
    # offsets [2, 3] -> median 2.5 -> 2 (banker's rounding)
    assert r["median"] == 2, r["median"]
    # unknown side matches on frame alone
    r2 = match_events([{"frame": 111, "side": None}], true)
    assert len(r2["matched"]) == 1 and r2["unknown_side"] == 1, r2
    assert all(t["frame"] != 110 for t in r2["missed"]), r2  # 110R consumed
    # re-centring: tracker consistently 4 late -> window shifts to 4+-4
    t_true = [{"frame": 50 + 10 * i, "side": "L"} for i in range(4)]
    t_trk = [{"frame": 54 + 10 * i, "side": "L"} for i in range(4)]
    r3 = match_events(t_trk, t_true)
    assert r3["median"] == 4 and len(r3["matched"]) == 4, r3
    shifted = [dict(frame=t["frame"] + 4, side=t["side"]) for t in t_true]
    r4 = match_events(t_trk, shifted)
    assert len(r4["matched"]) == 4 and r4["median"] == 0, r4
    print("event_score selftest OK: matching, wrong-side, unknown-side, "
          "recentring all behave")
    return 0


def main(argv=None) -> int:
    import argparse
    import os
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("suffixes", nargs="*",
                    help="session suffixes (default: every marked session)")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                    help="override one TrackingConfig field for this run "
                         "(same syntax as evaluate --set)")
    a = ap.parse_args(argv)
    if a.selftest:
        return _selftest()

    from .tracker import TrackingConfig
    cfg = TrackingConfig()
    for item in a.set:
        name, sep, raw = item.partition("=")
        if not sep or not hasattr(cfg, name):
            ap.error(f"--set {item}: expected NAME=VALUE with NAME a "
                     f"TrackingConfig field")
        cur = getattr(cfg, name)
        try:
            val = (raw.lower() in ("1", "true", "yes") if isinstance(cur, bool)
                   else type(cur)(raw))
        except ValueError:
            ap.error(f"--set {item}: cannot read {raw!r} as "
                     f"{type(cur).__name__}")
        setattr(cfg, name, val)
        print(f"OVERRIDE {name} = {val!r} (default {cur!r}) - results "
              f"are NOT the baseline")

    suffixes = a.suffixes
    if not suffixes:
        gt_dir = os.path.join(os.path.dirname(
            os.path.abspath(__file__)), "ground_truth")
        suffixes = [f[:-len("_catches.json")] for f in sorted(os.listdir(gt_dir))
                    if f.endswith("_catches.json")] if os.path.isdir(gt_dir) else []
    if not suffixes:
        print("no marked sessions. Mark one first: "
              "python3 -m tracking_lab.mark_catches box30", file=sys.stderr)
        return 2

    rows = []
    for s in suffixes:
        exp, balls = None, 3
        for su, e, b in evaluate.TUNING + evaluate.HELDOUT + \
                evaluate.discover_heldout():
            if su == s:
                exp, balls = e, b
        r = score_session(s, n_balls=balls, cfg=cfg)
        if r is None:
            integ = evaluate.integrity(s)
            why = ("no ground-truth marks" if not load_marks(s)
                   else "recording missing or rejected by integrity check")
            print(f"SKIP {s}: {why}", file=sys.stderr)
            continue
        rows.append(r)
    if not rows:
        return 1
    report(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
