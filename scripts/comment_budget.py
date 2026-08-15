#!/usr/bin/env python3
"""Report comment:code ratios and flag files over the CONTRIBUTING.md budgets.

Budgets: headers 25%, implementations 15%, tests 15%. --strict exits 1 on any
breach, which is how `make check-full` uses it.
"""

import argparse
import re
import sys
from pathlib import Path

BUDGETS = (("header", 0.25), ("impl", 0.15), ("test", 0.15))
ROOTS = ("src/libbotanik", "src/sites", "src/botanik_sim", "src/botanik_fw", "tests")
MIN_CODE = 20  # below this a ratio is noise, not a signal


def kind(path):
    if "tests/" in path.as_posix():
        return "test"
    return "header" if path.suffix in (".h", ".hpp") else "impl"


def count(path):
    """Return (comment, code). Blank lines count as neither."""
    comment = code = 0
    in_block = False
    for raw in path.read_text(errors="replace").splitlines():
        line = raw.strip()
        if not line:
            continue
        if in_block:
            comment += 1
            if "*/" in line:
                in_block = False
                # code trailing the close still counts as code
                if line.split("*/", 1)[1].strip():
                    code += 1
            continue
        if line.startswith("//"):
            comment += 1
        elif line.startswith("/*"):
            comment += 1
            in_block = "*/" not in line
        elif "//" in line or "/*" in line:
            code += 1  # trailing comment: the line is code
            if "/*" in line and "*/" not in line.split("/*", 1)[1]:
                in_block = True
        else:
            code += 1
    return comment, code


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="*", default=None)
    ap.add_argument("--strict", action="store_true", help="exit 1 on any breach")
    ap.add_argument("--top", type=int, default=15, help="worst N to list")
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    roots = [Path(p) for p in args.paths] if args.paths else [root / r for r in ROOTS]

    rows = []
    for base in roots:
        files = [base] if base.is_file() else sorted(
            f for ext in ("*.h", "*.hpp", "*.cpp") for f in base.rglob(ext)
        )
        for f in files:
            if "vendor" in f.parts:
                continue
            comment, code = count(f)
            if code < MIN_CODE:
                continue
            rows.append((kind(f), f.relative_to(root), comment, code))

    limits = dict(BUDGETS)
    over = [r for r in rows if r[2] > r[3] * limits[r[0]]]
    over.sort(key=lambda r: r[2] / r[3], reverse=True)

    if over:
        print(f"over budget ({len(over)} of {len(rows)} files):\n")
        for k, path, comment, code in over[: args.top]:
            print(f"  {comment/code:5.2f}  {comment:5d}c {code:5d}L  {path}")
        if len(over) > args.top:
            print(f"  ... and {len(over) - args.top} more")
        print()

    print(f"{'group':10} {'comment':>9} {'code':>9} {'ratio':>7} {'budget':>7}")
    fail = False
    for k, budget in BUDGETS:
        c = sum(r[2] for r in rows if r[0] == k)
        n = sum(r[3] for r in rows if r[0] == k)
        if not n:
            continue
        bad = c > n * budget
        fail |= bad
        print(f"{k:10} {c:9d} {n:9d} {c/n:7.2f} {budget:7.2f}  {'OVER' if bad else 'ok'}")

    tc = sum(r[2] for r in rows)
    tn = sum(r[3] for r in rows)
    print(f"{'TOTAL':10} {tc:9d} {tn:9d} {tc/tn:7.2f}")

    return 1 if (args.strict and (fail or over)) else 0


if __name__ == "__main__":
    sys.exit(main())
