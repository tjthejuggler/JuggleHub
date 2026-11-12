"""Compare two `evaluate --json` outputs field by field.

  python3 -m tracking_lab.evaluate --json .tmp/now.json
  python3 -m tracking_lab.compare_eval tracking_lab/baseline_eval_v8.json .tmp/now.json

Prints every field that differs. Exit code 0 = identical, 1 = something
differs, 2 = wrong usage. Use it after any change that is NOT meant to alter
tracking results (refactors, new diagnostics, camera or dataset code): the
answer must be "identical".

baseline_eval_v8.json = V8 detector, default TrackingConfig, 2026-11-12.
"""
from __future__ import annotations

import json
import sys


def _rows(d: dict, key: str) -> dict:
    return {r["session"]: r for r in d.get(key, [])}


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 2:
        print(__doc__)
        return 2
    with open(args[0]) as f:
        a = json.load(f)
    with open(args[1]) as f:
        b = json.load(f)
    diffs = 0
    for key in ("tuning", "heldout"):
        ra, rb = _rows(a, key), _rows(b, key)
        for s in sorted(set(ra) | set(rb)):
            if s not in ra or s not in rb:
                print(f"{key}/{s}: only in the {'second' if s not in ra else 'first'} file")
                diffs += 1
                continue
            for field in sorted(set(ra[s]) | set(rb[s])):
                if ra[s].get(field) != rb[s].get(field):
                    print(f"{key}/{s}: {field} {ra[s].get(field)} -> {rb[s].get(field)}")
                    diffs += 1
    print("identical" if not diffs else f"{diffs} difference(s)")
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
