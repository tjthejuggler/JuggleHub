"""CLI entry point for the SAM 3 live pipeline.

Examples:
  python -m sam3_live --source webcam --tracker sam3 --balls 3
  python -m sam3_live --source realsense --tracker sam3 \
      --prompt "red juggling ball" --balls 3 --csv runs/session1.csv
  python -m sam3_live --source webcam --tracker legacy --balls 3  # A/B
"""

import argparse
import logging
import sys


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="sam3_live",
        description="Source-pluggable, tracker-pluggable juggling ball "
                    "tracking (SAM 3 default, legacy color fallback).",
    )
    p.add_argument("--source", choices=["webcam", "realsense"],
                   default="webcam", help="input mode (default: webcam)")
    p.add_argument("--tracker", choices=["sam3", "legacy"], default="sam3",
                   help="tracker backend (default: sam3)")
    p.add_argument("--prompt", default="ball",
                   help="SAM 3 text prompt (default 'ball'; note 'juggling ball' "
                        "scores 0.0 with SAM 3's text encoder), e.g. 'tennis ball', "
                        "'LED ball'")
    p.add_argument("--balls", type=int, default=None,
                   help="expected ball count N: keep top-N tracks")
    p.add_argument("--score", type=float, default=0.2,
                   help="SAM 3 score threshold (tuned: 0.2 gives best F1; "
                        "0.5 was the pre-sweep default)")
    p.add_argument("--depth-min", type=float, default=1.0,
                   help="working range minimum, meters (realsense only)")
    p.add_argument("--depth-max", type=float, default=4.0,
                   help="working range maximum, meters (realsense only)")
    p.add_argument("--csv", default=None,
                   help="per-frame track CSV output path")
    p.add_argument("--headless", action="store_true",
                   help="no overlay window (server/benchmark use)")
    p.add_argument("--no-overlay", action="store_true",
                   help="run with overlay disabled")
    p.add_argument("--device", type=int, default=0,
                   help="webcam device index (webcam only)")
    p.add_argument("--width", type=int, default=1280)
    p.add_argument("--height", type=int, default=720)
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--checkpoint", default=None,
                   help="path to a local SAM 3 .pt checkpoint "
                        "(default: models/sam3/sam3.pt or $SAM3_CHECKPOINT)")
    p.add_argument("--sam-version", choices=["sam3", "sam3.1"],
                   default="sam3", help="SAM 3.1 = multiplex, ~2x faster")
    p.add_argument("--window", type=int, default=48,
                   help="live sliding-window size in frames for SAM 3")
    p.add_argument("--sam-device", choices=["cuda", "xpu", "cpu"],
                   default=None,
                   help="force SAM 3 device: cuda (GPU box), xpu (Intel "
                        "iGPU, experimental), cpu (slow, offline validation)")
    p.add_argument("--max-frames", type=int, default=None)
    p.add_argument("-v", "--verbose", action="store_true")
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    source_kwargs = {}
    if args.source == "webcam":
        source_kwargs = dict(device=args.device, width=args.width,
                             height=args.height, fps=args.fps)
    else:
        source_kwargs = dict(width=args.width, height=args.height,
                             fps=args.fps)

    tracker_kwargs = {}
    if args.tracker == "sam3":
        if args.checkpoint:
            tracker_kwargs["checkpoint_path"] = args.checkpoint
        tracker_kwargs["version"] = args.sam_version
        tracker_kwargs["window"] = args.window
        if args.sam_device:
            tracker_kwargs["device"] = args.sam_device

    from .pipeline import LivePipeline

    try:
        pipeline = LivePipeline(
            source_kind=args.source,
            tracker_kind=args.tracker,
            prompt=args.prompt,
            balls=args.balls,
            score_threshold=args.score,
            working_range_m=(args.depth_min, args.depth_max),
            csv_path=args.csv,
            show_overlay=not args.no_overlay,
            headless=args.headless,
            source_kwargs=source_kwargs,
            tracker_kwargs=tracker_kwargs,
        )
    except RuntimeError as exc:
        # Graceful fallback policy: missing CUDA / sam3 -> warn loudly,
        # suggest legacy, exit cleanly rather than crash mid-run.
        logging.getLogger(__name__).error("%s", exc)
        if args.tracker == "sam3":
            print("SAM 3 unavailable. Re-run with --tracker legacy for the "
                  "color-blob baseline.", file=sys.stderr)
        return 2

    stats = pipeline.run(max_frames=args.max_frames)
    print(stats.summary())
    return 0


if __name__ == "__main__":
    sys.exit(main())
