"""Build the V12 training dataset (rewritten 2026-11-12).

WHY THE FIRST VERSION WAS REPLACED
The first builder copied five dataset generations (V8, V9.1.1, V9.2, V9.3,
V10) plus annotation sessions plus the GLM-labelled frames into one folder.
Measured on its output (.tmp/v12_dataset, 22,969 train / 4,856 val files):
  * the five generations are the SAME frames: 10,656 unique images in total;
  * the yaml said nc: 1 but the labels use classes 0 and 1, so Ultralytics
    would have dropped 66.7% of train and 75.7% of val images;
  * V9.3 has different box sizes from V8..V9.2 for the same frames (2-4 px on
    a ~20 px ball), so most duplicated frames carried conflicting labels;
  * 2,783 val files were byte-identical to a train file, and nearly all val
    frames came from recordings that were also in train;
  * 240 of the 559 GLM frames came from the six counted regression videos.
Do not train on .tmp/v12_dataset or upload .tmp/v12_dataset.zip.

WHAT THIS VERSION DOES
  * One source of hand labels: V9_3_targeted. It is a superset of the frames
    in V8/V9.1.1/V9.2, has the newest labels, is 640x480 like the camera, and
    already contains the hard negatives as empty label files (kept: an empty
    label file is a background image for Ultralytics).
    V10_V8_640x360 is not used: it is a 640x360 crop of the same frames, and
    its 387 extra frames are images that had NO annotation and were given
    empty labels (the deployed detector fires on 62 of them, mostly hands; it
    is unknown whether those are unlabelled balls).
  * Each frame appears once.
  * Train/val split BY RECORDING, per annotation category. The old split was
    by frame: every one of the 88 val recordings was also in train. Colour-
    augmented copies share their recording's name, so they follow it.
    Expect lower val mAP than earlier models reported; the old numbers were
    measured on near-copies of training frames and are not comparable.
  * Two classes (0 ball, 1 ball_held), as in the deployed model. The tracker
    ignores the class; --single-class writes one class instead.
  * --with-glm adds the GLM-labelled frames from the 2026-11 recordings to
    TRAIN only. Excluded always: every counted session (TUNING and HELDOUT in
    evaluate.py; those are for evaluation) and every recording that fails the
    repeated-frame check. GLM labels are NOT hand-verified and never go into
    val. Off by default.
  * The result is checked before the script reports success (see verify()).

WHAT IT DOES NOT SOLVE
Without --with-glm the dataset contains no frame from the 2026-11 recordings,
and there is still no hand-labelled detection test set from them, so nothing
here can show whether a new model detects better on the footage the tracker
is judged on.

Usage:
  python3 -m tracking_lab.build_v12_dataset [--with-glm] [--single-class] [--force]
  python3 -m tracking_lab.build_v12_dataset --check      # re-verify an existing build
Output: .tmp/v12_clean/{train,val}/{images,labels} + data.yaml + manifest.json
"""
from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TMP = os.path.join(REPO, ".tmp")
OUT = os.path.join(TMP, "v12_clean")
BASE = os.path.join(REPO, "engine", "data", "3_training_datasets", "V9_3_targeted")
SESSIONS = os.path.join(REPO, "engine", "data", "annotation_sessions")
GLM_DIR = os.path.join(TMP, "v11_dataset")
REC_ROOT = os.path.join(HERE, "recordings")

VAL_FRACTION = 0.15
IMG_EXT = (".jpg", ".jpeg", ".png")
NAMES = ["ball", "ball_held"]


def _name_hash(name: str) -> str:
    return hashlib.md5(name.encode()).hexdigest()


def _file_md5(path: str) -> str:
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def group_of(stem: str) -> str:
    """The recording a frame came from (augmented copies keep the name)."""
    if stem.startswith("glm_"):
        stem = stem[4:]
    if "_frame_" in stem:
        return stem.split("_frame_")[0]
    return re.sub(r"_f\d+$", "", stem)


def read_label(path: str, nc: int = len(NAMES)) -> list:
    """-> [(cls, x, y, w, h)]; raises ValueError on anything Ultralytics rejects."""
    rows = []
    with open(path) as f:
        for ln in f:
            p = ln.split()
            if not p:
                continue
            if len(p) != 5:
                raise ValueError(f"{len(p)} fields")
            cls, xywh = int(float(p[0])), [float(v) for v in p[1:]]
            if not 0 <= cls < nc:
                raise ValueError(f"class {cls} with nc={nc}")
            if not all(0.0 <= v <= 1.0 for v in xywh):
                raise ValueError("coordinates outside 0..1")
            rows.append((cls, *xywh))
    return rows


def load_base():
    """-> ({stem: (image, label)}, Counter of dropped frames by reason)."""
    frames, dropped = {}, Counter()
    for split in ("train", "valid"):
        img_dir = os.path.join(BASE, split, "images")
        for f in sorted(os.listdir(img_dir)):
            stem, ext = os.path.splitext(f)
            if ext.lower() not in IMG_EXT:
                continue
            lab = os.path.join(BASE, split, "labels", stem + ".txt")
            if not os.path.exists(lab):
                dropped["no label file"] += 1
            elif stem in frames:
                dropped["same name in train and valid (train copy kept)"] += 1
            else:
                try:
                    read_label(lab)
                except ValueError as exc:
                    dropped[f"bad label ({exc})"] += 1
                    continue
                frames[stem] = (os.path.join(img_dir, f), lab)
    return frames, dropped


def categories() -> dict:
    """stem -> annotation category, from the V9.3 annotation-session folders."""
    out = {}
    for d in sorted(glob.glob(os.path.join(SESSIONS, "V9.3*"))):
        name = os.path.basename(d)
        if "col_aug" in name or "augmented" in name:
            continue  # augmented copies take the category of their base frame
        cat = re.sub(r"^V9\.3_?", "", name)
        img_dir = os.path.join(d, "images")
        if os.path.isdir(img_dir):
            for f in os.listdir(img_dir):
                stem, ext = os.path.splitext(f)
                if ext.lower() in IMG_EXT:
                    out[stem] = cat
    return out


def split_by_recording(frames: dict, cat_of: dict):
    """-> (set of val recordings, {recording: category}). Deterministic: within
    each category recordings are taken in name-hash order until VAL_FRACTION of
    the category's frames is reached. A recording larger than twice the target
    is skipped, so a category with one recording stays entirely in train."""
    by_rec = defaultdict(list)
    for stem in frames:
        by_rec[group_of(stem)].append(stem)
    rec_cat = {}
    for rec, stems in by_rec.items():
        votes = Counter(cat_of.get(s.split("_col_aug")[0], "other") for s in stems)
        rec_cat[rec] = sorted(votes.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]
    val = set()
    for cat in sorted(set(rec_cat.values())):
        recs = sorted((r for r in by_rec if rec_cat[r] == cat), key=_name_hash)
        target = VAL_FRACTION * sum(len(by_rec[r]) for r in recs)
        got = 0
        for r in recs:
            if got >= target:
                break
            if got + len(by_rec[r]) > 2 * target:
                continue
            val.add(r)
            got += len(by_rec[r])
    return val, rec_cat


def load_glm():
    """-> ({stem: (image, label)}, {session: reason it was excluded})."""
    # counted sessions: never train on them. heldout_suite() also includes
    # sessions recorded later with `verify_wizard --heldout <tag>`.
    from .evaluate import TUNING, heldout_suite
    from .recording import RecordingSession
    counted = [s[0] for s in TUNING + heldout_suite()]
    frames, verdict = {}, {}
    for split in ("train", "val"):
        for img in sorted(glob.glob(os.path.join(GLM_DIR, "images", split, "*"))):
            stem, ext = os.path.splitext(os.path.basename(img))
            if ext.lower() not in IMG_EXT:
                continue
            sess = re.sub(r"_f\d+$", "", stem)
            if sess not in verdict:
                rec = os.path.join(REC_ROOT, sess)
                if any(sess.endswith("_" + c) for c in counted):
                    verdict[sess] = "counted session, kept for evaluation"
                elif not os.path.isdir(rec):
                    verdict[sess] = "recording not found, integrity unknown"
                elif not RecordingSession(rec, load_depth=False).integrity()["valid"]:
                    verdict[sess] = "repeated colour frames"
                else:
                    verdict[sess] = None
            lab = os.path.join(GLM_DIR, "labels", split, stem + ".txt")
            if verdict[sess] is None and os.path.exists(lab):
                try:
                    read_label(lab)
                except ValueError:
                    continue
                frames[stem] = (img, lab)
    return frames, {s: r for s, r in verdict.items() if r}


def _place(img: str, lab: str, split: str, stem: str, single_class: bool) -> None:
    dst_img = os.path.join(OUT, split, "images", stem + os.path.splitext(img)[1].lower())
    try:
        os.link(img, dst_img)        # same disk: no extra space
    except OSError:
        shutil.copy2(img, dst_img)
    with open(lab) as f, open(os.path.join(OUT, split, "labels", stem + ".txt"), "w") as g:
        for ln in f:
            p = ln.split()
            if p:
                g.write(" ".join((["0"] if single_class else [p[0]]) + p[1:]) + "\n")


def verify(ds: str, nc: int):
    """-> (problems, stats). Every check here is one the first build failed."""
    problems, stats, md5s, groups = [], {}, {}, {}
    for split in ("train", "val"):
        img_dir, lab_dir = os.path.join(ds, split, "images"), os.path.join(ds, split, "labels")
        imgs = {os.path.splitext(f)[0]: f for f in os.listdir(img_dir)}
        labs = {f[:-4] for f in os.listdir(lab_dir) if f.endswith(".txt")}
        if set(imgs) != labs:
            problems.append(f"{split}: {len(set(imgs) ^ labs)} images/labels without a partner")
        boxes, empty = Counter(), 0
        groups[split] = {group_of(s) for s in imgs}
        md5s[split] = {}
        for stem, f in imgs.items():
            try:
                rows = read_label(os.path.join(lab_dir, stem + ".txt"), nc)
            except (ValueError, OSError) as exc:
                problems.append(f"{split}/{stem}: {exc}")
                continue
            empty += not rows
            boxes.update(r[0] for r in rows)
            md5s[split].setdefault(_file_md5(os.path.join(img_dir, f)), []).append(stem)
        dup = sum(len(v) - 1 for v in md5s[split].values())
        if dup:
            problems.append(f"{split}: {dup} images are byte-identical to another image")
        stats[split] = dict(images=len(imgs), recordings=len(groups[split]),
                            background_images=empty,
                            boxes={str(k): boxes[k] for k in sorted(boxes)})
    both = groups["train"] & groups["val"]
    if both:
        problems.append(f"{len(both)} recordings are in both train and val, e.g. {sorted(both)[:3]}")
    shared = set(md5s["train"]) & set(md5s["val"])
    if shared:
        problems.append(f"{len(shared)} val images are byte-identical to a train image")
    if not stats["val"]["images"]:
        problems.append("val is empty")
    return problems, stats


def _report(problems: list, stats: dict) -> int:
    for split in ("train", "val"):
        print(f"  {split}: {stats[split]}")
    for p in problems[:20]:
        print("  PROBLEM:", p)
    print("  checks:", "FAILED" if problems else
          "passed (class ids, pairing, duplicates, no recording or image shared by train and val)")
    return 1 if problems else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build the V12 dataset (see module docstring).")
    ap.add_argument("--with-glm", action="store_true",
                    help="add unverified GLM-labelled 2026-11 frames to train")
    ap.add_argument("--single-class", action="store_true", help="write one class 'ball'")
    ap.add_argument("--force", action="store_true", help="delete an existing build first")
    ap.add_argument("--check", action="store_true", help="only re-verify the existing build")
    args = ap.parse_args(argv)

    manifest_path = os.path.join(OUT, "manifest.json")
    if args.check:
        if not os.path.exists(manifest_path):
            print(f"no build at {OUT}")
            return 1
        with open(manifest_path) as f:
            return _report(*verify(OUT, json.load(f)["nc"]))
    if os.path.exists(OUT):
        if not args.force:
            print(f"{OUT} already exists; pass --force to rebuild")
            return 1
        shutil.rmtree(OUT)
    for split in ("train", "val"):
        for kind in ("images", "labels"):
            os.makedirs(os.path.join(OUT, split, kind))

    frames, dropped = load_base()
    cat_of = categories()
    val_recs, rec_cat = split_by_recording(frames, cat_of)
    per_cat = defaultdict(lambda: dict(train=0, val=0, train_recordings=set(), val_recordings=set()))
    for stem, (img, lab) in frames.items():
        rec = group_of(stem)
        split = "val" if rec in val_recs else "train"
        _place(img, lab, split, stem, args.single_class)
        per_cat[rec_cat[rec]][split] += 1
        per_cat[rec_cat[rec]][split + "_recordings"].add(rec)

    glm_used, glm_excluded = {}, {}
    if args.with_glm:
        glm_used, glm_excluded = load_glm()
        for stem, (img, lab) in glm_used.items():
            _place(img, lab, "train", "glm_" + stem, args.single_class)

    names = ["ball"] if args.single_class else NAMES
    with open(os.path.join(OUT, "data.yaml"), "w") as f:
        # No 'path:' key: Ultralytics then resolves train/val relative to this
        # file, so the folder works unchanged on Colab.
        f.write("train: train/images\nval: val/images\n"
                f"nc: {len(names)}\nnames:\n" + "".join(f"- {n}\n" for n in names))

    problems, stats = verify(OUT, len(names))
    cats = {c: dict(train=v["train"], val=v["val"],
                    train_recordings=len(v["train_recordings"]),
                    val_recordings=len(v["val_recordings"]))
            for c, v in sorted(per_cat.items())}
    with open(manifest_path, "w") as f:
        json.dump(dict(source=os.path.relpath(BASE, REPO), nc=len(names), names=names,
                       val_fraction_target=VAL_FRACTION, dropped_from_source=dict(dropped),
                       categories=cats, val_recordings=sorted(val_recs),
                       glm=dict(included=args.with_glm, frames=len(glm_used),
                                sessions=sorted({group_of(s) for s in glm_used}),
                                excluded_sessions=glm_excluded,
                                note="GLM labels are not hand-verified; train only"),
                       stats=stats, problems=problems), f, indent=1)

    print(f"V12 dataset -> {OUT}")
    print(f"  source frames used: {len(frames)}; dropped: {dict(dropped) or 'none'}")
    print(f"  {'category':62s} {'train':>6s} {'val':>5s}  recordings train/val")
    for c, v in cats.items():
        print(f"  {c:62s} {v['train']:6d} {v['val']:5d}  {v['train_recordings']}/{v['val_recordings']}")
    if args.with_glm:
        print(f"  GLM frames added to train: {len(glm_used)} from "
              f"{len({group_of(s) for s in glm_used})} sessions (labels unverified)")
        for s, why in sorted(glm_excluded.items()):
            print(f"    excluded {s}: {why}")
    rc = _report(problems, stats)
    if rc == 0:
        print(f"zip with: cd {TMP} && zip -q -r v12_clean.zip v12_clean")
    return rc


if __name__ == "__main__":
    sys.exit(main())
