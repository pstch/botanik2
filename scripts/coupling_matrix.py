#!/usr/bin/env python3
"""Generate the module coupling matrix shown in the docs (Developer manual).

One chart, `coupling.svg`: every cross-module `#include` in src/, with the modules
on both axes ordered bottom-of-stack to top. A cell left of the diagonal is a
module depending on something BENEATH it - the layering working. A cell right of
the diagonal points back UP the stack, and is a layering violation with nowhere
to hide.

Two kinds of edge are counted separately, because they cost differently:
  - a `.h -> .h` include is CONTAGIOUS: it lands in every file that transitively
    includes the header, so it is what fuses modules into dependency cycles;
  - a `.cpp -> .h` include is private to one translation unit.
The matrix shows header edges; `--json` reports both.

Stdlib only, no build required. Output is gitignored; the CMake `docs` target
runs this before doxygen.
"""

import argparse
import json
import os
import re
import sys

# --------------------------------------------------------------------------
# Layering - the one thing this script asserts rather than derives
# --------------------------------------------------------------------------

# The intended layering, bottom of the stack first: a module may depend on
# anything ABOVE it in this list and nothing below. Adding a module means adding
# a row; a missing one is appended at the top and reported on stderr, so a new
# subsystem cannot quietly acquire a free pass.
LAYERS = [
    "hal",        # the platform boundary; everything may reach it
    "tasks",      # TaskTimer + the task registry - BELOW core, because Component
                  # embeds a TaskTimer for its optional per-component rate
    "core",       # Component + SystemContext: the lifecycle primitives
    "utils",      # serial, text, time, filters, assert
    "logs",       # the logger and its writers
    "vars",       # the variable model
    "scheduler",  # the beat driver
    "plc",        # the POU vocabulary
    "scan",       # the scan cycle over the site's programs
    "alarms",     # the alarm model
    "modbus",     # the field bus
    "network",    # transport-side components
    "sun",        # standalone solar tracker
    "health",     # the health monitor, over alarms + scheduler
    "console",    # the terminal device and the page multiplexer
    "shell",      # the command interpreter - renders on the terminal, and knows
                  # nothing of pages (that is what Shell::ShellWriter is for)
    "pages",      # the individual pages; ShellPage owns a CommandShell
    "firmware",   # the composition root
    "umbrella",   # libbotanik.h, the convenience header a site includes
    "site",       # sites/<name> and the target entry points
]

# Held out of the matrix: every module includes these, so they would render as a
# solid column and say nothing.
CONFIG_FILES = {"libbotanik_config.h", "libbotanik_features.h", "libbotanik_site.h"}
CONFIG_DIR = "libbotanik/config"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')

# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------

FONT = "system-ui, -apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"
DOWN_FILL = "#cde2fb"     # conforming: a light step of the sequential blue
DOWN_INK = "#184f95"
UP_FILL = "#d03b3b"       # violation: the status red, never a series colour
GRID = "#e1e0d9"
DIAG = "#c3c2b7"
INK = "#52514e"
MUTED = "#898781"


def module_of(rel):
    """Map a path under src/ to its module, or None to leave it out."""
    if rel in CONFIG_FILES:
        return None
    if rel == "libbotanik.h":
        return "umbrella"
    parts = rel.split("/")
    if parts[0] in ("sites", "botanik_sim", "botanik_fw"):
        return "site"
    if parts[0] != "libbotanik" or len(parts) < 2:
        return None
    if "/".join(parts[:2]) == CONFIG_DIR:
        return None
    # A loose file directly under libbotanik/ is part of the library's face to a
    # SITE, like the umbrella, not a subsystem of its own.
    if len(parts) == 2:
        return "umbrella"
    return parts[1]


def scan_tree(src_dir):
    """Return (files, edges) with paths relative to src_dir."""
    files = []
    for d, _, names in os.walk(src_dir):
        for n in names:
            if n.endswith((".h", ".cpp")):
                files.append(os.path.relpath(os.path.join(d, n), src_dir))
    files.sort()
    known = set(files)
    edges = []                      # (src_rel, dst_rel)
    for rel in files:
        with open(os.path.join(src_dir, rel), errors="ignore") as f:
            for line in f:
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                target = m.group(1)
                if target not in known:
                    # relative include, or a toolchain header (mbed.h, ...)
                    cand = os.path.normpath(os.path.join(os.path.dirname(rel), target))
                    if cand not in known:
                        continue
                    target = cand
                edges.append((rel, target))
    return files, edges


def build_matrix(files, edges):
    """Aggregate file edges into module counts, split header vs implementation."""
    seen = sorted({m for m in (module_of(f) for f in files) if m})
    order = [m for m in LAYERS if m in seen]
    extra = [m for m in seen if m not in LAYERS]
    if extra:
        sys.stderr.write(
            "coupling_matrix: module(s) missing from LAYERS, placed at the top "
            "of the stack: %s\n" % ", ".join(extra))
        order += extra
    pos = {m: i for i, m in enumerate(order)}

    hdr = {}      # (a, b) -> count of .h -> .h includes
    impl = {}     # (a, b) -> count of .cpp -> .h includes
    witness = {}
    for a, b in edges:
        ma, mb = module_of(a), module_of(b)
        if not ma or not mb or ma == mb:
            continue
        bucket = hdr if a.endswith(".h") else impl
        bucket[(ma, mb)] = bucket.get((ma, mb), 0) + 1
        if a.endswith(".h") and pos[ma] < pos[mb]:
            witness.setdefault((ma, mb), []).append((a, b))
    return order, pos, hdr, impl, witness


def render_svg(path, order, pos, hdr):
    cell = 18
    pad_l, pad_t = 92, 86
    n = len(order)
    # wide enough for the grid OR the legend beneath it, whichever needs more
    width = max(pad_l + cell * n + 12, pad_l + 470)
    height = pad_t + cell * n + 46

    up_total = sum(v for (a, b), v in hdr.items() if pos[a] < pos[b])
    down_total = sum(v for (a, b), v in hdr.items() if pos[a] > pos[b])

    s = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
         'viewBox="0 0 %d %d" font-family="%s">' % (width, height, width, height, FONT)]

    # column headers, rotated so long module names fit a narrow cell
    for j, m in enumerate(order):
        x = pad_l + j * cell + cell * 0.72
        s.append('<text transform="translate(%.1f,%d) rotate(-55)" font-size="9" '
                 'fill="%s">%s</text>' % (x, pad_t - 6, INK, m))

    for i, a in enumerate(order):
        y = pad_t + i * cell
        s.append('<text x="%d" y="%.1f" font-size="9" fill="%s" text-anchor="end">%s</text>'
                 % (pad_l - 6, y + cell * 0.72, INK, a))
        for j, b in enumerate(order):
            x = pad_l + j * cell
            if i == j:
                s.append('<rect x="%d" y="%d" width="%d" height="%d" fill="#efeee9"/>'
                         % (x, y, cell, cell))
                continue
            s.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" '
                     'stroke="%s" stroke-width="0.5"/>' % (x, y, cell, cell, GRID))
            v = hdr.get((a, b), 0)
            if not v:
                continue
            # i > j means the target sits LOWER in the stack: a downward include.
            up = i < j
            s.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="2" fill="%s"/>'
                     % (x + 1.5, y + 1.5, cell - 3, cell - 3, UP_FILL if up else DOWN_FILL))
            s.append('<text x="%.1f" y="%.1f" font-size="8" text-anchor="middle" '
                     'fill="%s"%s>%d</text>'
                     % (x + cell / 2.0, y + cell * 0.7,
                        "#ffffff" if up else DOWN_INK,
                        ' font-weight="bold"' if up else "", v))

    s.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="0.8" '
             'stroke-dasharray="2 2"/>'
             % (pad_l, pad_t, pad_l + n * cell, pad_t + n * cell, DIAG))

    ly = pad_t + n * cell + 20
    s.append('<rect x="%d" y="%d" width="9" height="9" rx="2" fill="%s"/>' % (pad_l, ly, DOWN_FILL))
    s.append('<text x="%d" y="%d" font-size="9.5" fill="%s">downward include '
             '(conforming) - %d</text>' % (pad_l + 13, ly + 8, INK, down_total))
    s.append('<rect x="%d" y="%d" width="9" height="9" rx="2" fill="%s"/>'
             % (pad_l + 250, ly, UP_FILL))
    s.append('<text x="%d" y="%d" font-size="9.5" fill="%s">upward include '
             '(violation) - %d</text>' % (pad_l + 263, ly + 8, INK, up_total))
    s.append('<text x="%d" y="%d" font-size="8.5" fill="%s">header edges only; '
             'config headers held out (every module includes them)</text>'
             % (pad_l, ly + 24, MUTED))
    s.append("</svg>")

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(s) + "\n")


def render_text(order, pos, hdr, impl, witness):
    up = [(k, v) for k, v in hdr.items() if pos[k[0]] < pos[k[1]]]
    print("modules: %d   header edges: %d   implementation edges: %d"
          % (len(order), sum(hdr.values()), sum(impl.values())))
    print("layering: %s" % " -> ".join(order))
    if not up:
        print("\nno upward header includes: the module graph respects the layering")
        return
    print("\nUPWARD header includes (%d pair(s), %d include(s)):"
          % (len(up), sum(v for _, v in up)))
    for (a, b), v in sorted(up, key=lambda kv: -kv[1]):
        print("  %-12s -> %-12s  %d" % (a, b, v))
        for w in witness.get((a, b), [])[:4]:
            print("        %s  ->  %s" % w)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=os.path.join(repo, "src"),
                    help="source tree to scan (default: <repo>/src)")
    ap.add_argument("--out", default=None,
                    help="directory to write coupling.svg into")
    ap.add_argument("--text", action="store_true", help="print the summary instead")
    ap.add_argument("--json", metavar="PATH", help="also dump the raw counts")
    args = ap.parse_args()

    files, edges = scan_tree(args.src)
    order, pos, hdr, impl, witness = build_matrix(files, edges)

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        render_svg(os.path.join(args.out, "coupling.svg"), order, pos, hdr)
    if args.json:
        json.dump({"layers": order,
                   "header_edges": {"%s->%s" % k: v for k, v in hdr.items()},
                   "impl_edges": {"%s->%s" % k: v for k, v in impl.items()},
                   "upward": {"%s->%s" % k: v for k, v in hdr.items()
                              if pos[k[0]] < pos[k[1]]}},
                  open(args.json, "w"), indent=1)
    if args.text or not (args.out or args.json):
        render_text(order, pos, hdr, impl, witness)
    return 0


if __name__ == "__main__":
    sys.exit(main())
