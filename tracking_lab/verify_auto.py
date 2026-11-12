"""Verify auto-annotations: render frames with the model's claims drawn on top,
so a vision-capable reviewer can confirm or reject each claim.

  python3 -m tracking_lab.verify_auto --only box30

Writes .tmp/verify_auto/<video>/frame_<n>_<claim>.jpg contact sheets + a summary.
"""
from __future__ import annotations

import glob
import json
import os
import sys

import cv2

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.abspath(os.path.join(HERE, "..", ".tmp"))
OUT = os.path.join(TMP, "verify_auto")


def verify(frag: str) -> int:
    auto_path = os.path.join(TMP, f"count_{frag}_auto.json")
    vpath = os.path.join(TMP, f"count_{frag}.mp4")
    if not (os.path.exists(auto_path) and os.path.exists(vpath)):
        print(f"missing {auto_path} or {vpath}")
        return 1
    d = json.load(open(auto_path))
    out_dir = os.path.join(OUT, frag)
    os.makedirs(out_dir, exist_ok=True)

    cap = cv2.VideoCapture(vpath)
    pf = {r["frame"]: r for r in d["samples"]}

    n_claims = 0
    # 1. render every flagged claim
    for i, b in enumerate(d["bad_tracks"]):
        f = b["frame"]
        cap.set(cv2.CAP_PROP_POS_FRAMES, f)
        ok, frame = cap.read()
        if not ok:
            continue
        r = pf.get(f)
        if r:
            tr = r["tracker"]
            for tid, (x, y), st in zip(tr["ids"], tr["pos"], tr["states"]):
                cv2.circle(frame, (int(x), int(y)), 16, (0, 255, 0), 2)
                cv2.putText(frame, f"#{tid} {st}", (int(x) + 18, int(y) - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
            for k, (x, y) in tr.get("wrists", {}).items():
                cv2.circle(frame, (int(x), int(y)), 6, (255, 0, 0), 2)
                cv2.putText(frame, k[:5], (int(x) + 8, int(y) + 4),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 0, 0), 1)
        # model's flagged track highlighted red
        r = pf.get(f)
        if r and b["track"] in r["tracker"]["ids"]:
            idx = r["tracker"]["ids"].index(b["track"])
            px, py = r["tracker"]["pos"][idx]
            cv2.circle(frame, (int(px), int(py)), 24, (0, 0, 255), 3)
        cv2.putText(frame, f"CLAIM: {b['problem']}", (12, 200),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
        cv2.imwrite(os.path.join(out_dir, f"claim_{i:02d}_f{f}_t{b['track']}.jpg"),
                    frame)
        n_claims += 1

    # 2. render event-claim frames
    for j, f in enumerate(d["event_frames"]):
        cap.set(cv2.CAP_PROP_POS_FRAMES, f)
        ok, frame = cap.read()
        if not ok:
            continue
        cv2.putText(frame, f"MODEL SAYS: event at frame {f}", (12, 200),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        cv2.imwrite(os.path.join(out_dir, f"event_{j:02d}_f{f}.jpg"), frame)
        n_claims += 1
    cap.release()
    print(f"{frag}: wrote {n_claims} verification images -> {out_dir}")
    return 0


def main(argv=None) -> int:
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]
    queue = [only] if only else ["box30", "441_30", "shower40",
                                 "columns30", "flash_10", "cascade60"]
    for frag in queue:
        auto = os.path.join(TMP, f"count_{frag}_auto.json")
        if os.path.exists(auto):
            verify(frag)
        else:
            print(f"{frag}: no auto annotation yet, skipping")
    return 0


if __name__ == "__main__":
    sys.exit(main())
