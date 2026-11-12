"""SAM 3 live ball-tracking pipeline for JuggleHub.

Source-pluggable (webcam / RealSense) and tracker-pluggable
(SAM 3 streaming / legacy HSV color-blob) live juggling ball tracker.

See README_SAM3.md for setup (gated HF model, CUDA notes) and usage.
"""

__version__ = "0.1.0"

from .trackers import Track  # noqa: F401
