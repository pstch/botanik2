#!/usr/bin/env python3
"""Generate the LoC and churn SVG charts shown at the top of the docs Library page.

Two charts, both bucketed by "code area" (the folder under src/):
  - code_loc.svg   : code lines per area (cloc, code only - no comments/blanks).
  - code_churn.svg : per area, lines changed (git add+del) AND commits touched,
                     over a trailing window (default 12 months).

Stdlib only; shells out to `cloc` (LoC) and `git` (churn). If either is missing
(or there is no git history), the affected chart is written as a placeholder so the
Doxygen build never hits a missing-image warning. Output is build-only and
gitignored; the CMake `docs` target runs this before doxygen.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

# --------------------------------------------------------------------------
# Area mapping - one definition shared by both charts
# --------------------------------------------------------------------------

# Historical path renames, applied (in order) before area-mapping so churn from a
# file's pre-rename life attributes to its current area. `-M` only rewrites the path
# at the rename commit itself; earlier commits carry the literal old path. Add a row
# here when a directory/file is renamed and its old name still falls in the window.
PATH_ALIASES = (
    ("src/libbotanix/", "src/libbotanik/"),                       # lib dir typo fix
    ("src/libbotanix_config.h", "src/libbotanik_config.h"),
    ("src/libbotanix.h", "src/libbotanik.h"),
    ("src/libbotanik/panes/", "src/libbotanik/pages/"),           # panes -> pages
    ("src/libbotanik_asserts.h", "src/libbotanik/utils/assert.h"),  # asserts -> utils
)


def canonical(path):
    """Apply the known historical renames so old paths fold into current areas."""
    for old, new in PATH_ALIASES:
        if old.endswith("/"):
            if path.startswith(old):
                path = new + path[len(old):]
        elif path == old:
            path = new
    return path


def area_for(path):
    """Map a repo path to its code area, or None to ignore (non-src)."""
    parts = path.split("/")
    if len(parts) < 2 or parts[0] != "src":
        return None
    if parts[1] == "libbotanik":
        # src/libbotanik/<dir>/<file...> -> the subsystem dir; loose files -> core
        return parts[2] if len(parts) >= 4 else "core"
    # top-level umbrella headers (src/libbotanik.h, _config.h, _sizes.h) -> core
    if parts[1].endswith((".h", ".cpp")):
        return "core"
    # src/<dir>/... -> sites, botanik_sim, botanik_fw
    return parts[1]


def resolve_rename(path):
    """Resolve a git numstat rename path to its current location.

    Handles both 'old => new' and the brace form 'pre/{old => new}/post'.
    """
    if "=>" not in path:
        return path
    if "{" in path and "}" in path:
        pre, rest = path.split("{", 1)
        mid, post = rest.split("}", 1)
        new = mid.split("=>", 1)[1].strip()
        return pre + new + post
    return path.split("=>", 1)[1].strip()


# --------------------------------------------------------------------------
# Data collection
# --------------------------------------------------------------------------

def collect_loc(src):
    """{area: code_lines} via cloc, or None if cloc is unavailable."""
    if not shutil.which("cloc"):
        return None
    try:
        out = subprocess.check_output(
            ["cloc", "--by-file", "--json", "--quiet", src],
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        return None
    data = json.loads(out)
    loc = {}
    for path, rec in data.items():
        if path in ("header", "SUM"):
            continue
        area = area_for(path)
        if area is None:
            continue
        loc[area] = loc.get(area, 0) + rec.get("code", 0)
    return loc


def collect_churn(src, since):
    """({area: lines_changed}, {area: commit_count}) or (None, None) on failure."""
    if not shutil.which("git"):
        return None, None
    try:
        out = subprocess.check_output(
            ["git", "log", "-M", "--numstat", "--since", since,
             "--format=tformat:__C__", "--", src],
            stderr=subprocess.DEVNULL,
        ).decode("utf-8", "replace")
    except subprocess.CalledProcessError:
        return None, None

    lines = {}
    commits = {}
    touched = set()  # areas touched by the current commit
    for ln in out.splitlines():
        if ln == "__C__":
            for a in touched:
                commits[a] = commits.get(a, 0) + 1
            touched = set()
            continue
        cols = ln.split("\t")
        if len(cols) != 3:
            continue
        add, dele, path = cols
        if add == "-":  # binary file
            continue
        area = area_for(canonical(resolve_rename(path)))
        if area is None:
            continue
        lines[area] = lines.get(area, 0) + int(add) + int(dele)
        touched.add(area)
    for a in touched:  # final commit (no trailing marker)
        commits[a] = commits.get(a, 0) + 1
    return lines, commits


# --------------------------------------------------------------------------
# SVG rendering (hand-rolled - no charting dep)
# --------------------------------------------------------------------------

FONT = "Helvetica,Arial,sans-serif"
LABEL_W = 118     # left gutter for area names
BAR_W = 360       # max bar pixel length
VAL_W = 70        # right gutter for value text
WIDTH = LABEL_W + BAR_W + VAL_W


def _esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _svg_open(width, height):
    return [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d" font-family="%s">' % (width, height, width, height, FONT),
    ]


def _placeholder(path, msg):
    s = _svg_open(WIDTH, 60)
    s.append('<text x="%d" y="34" font-size="13" fill="#999" text-anchor="middle">%s</text>'
             % (WIDTH // 2, _esc(msg)))
    s.append("</svg>")
    _write(path, s)


def _write(path, parts):
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(parts) + "\n")


def render_loc(path, loc):
    if not loc:
        _placeholder(path, "LoC chart unavailable (cloc not found)")
        return
    rows = sorted(loc.items(), key=lambda kv: kv[1], reverse=True)
    top = 14
    rh = 24
    h = top + len(rows) * rh + 10
    mx = max(v for _, v in rows) or 1
    s = _svg_open(WIDTH, h)
    y = top
    for name, val in rows:
        bw = max(1, int(BAR_W * val / mx))
        cy = y + rh // 2
        s.append('<text x="%d" y="%d" font-size="12" fill="#333" text-anchor="end" '
                 'dominant-baseline="middle">%s</text>' % (LABEL_W - 8, cy, _esc(name)))
        s.append('<rect x="%d" y="%d" width="%d" height="14" rx="2" fill="#4c78a8"/>'
                 % (LABEL_W, cy - 7, bw))
        s.append('<text x="%d" y="%d" font-size="11" fill="#555" '
                 'dominant-baseline="middle">%s</text>'
                 % (LABEL_W + bw + 6, cy, _esc("{:,}".format(val))))
        y += rh
    s.append("</svg>")
    _write(path, s)


def render_churn(path, lines, commits):
    if lines is None:
        _placeholder(path, "Churn chart unavailable (no git history)")
        return
    areas = set(lines) | set(commits)
    if not areas:
        _placeholder(path, "Churn chart: no changes in window")
        return
    rows = sorted(areas, key=lambda a: lines.get(a, 0), reverse=True)
    top = 30           # room for the legend
    rh = 30            # two sub-bars per area
    bh = 10
    h = top + len(rows) * rh + 10
    lmax = max((lines.get(a, 0) for a in rows), default=1) or 1
    cmax = max((commits.get(a, 0) for a in rows), default=1) or 1
    s = _svg_open(WIDTH, h)
    # legend
    s.append('<rect x="%d" y="8" width="11" height="11" rx="2" fill="#4c78a8"/>' % LABEL_W)
    s.append('<text x="%d" y="17" font-size="11" fill="#555">lines changed</text>' % (LABEL_W + 16))
    s.append('<rect x="%d" y="8" width="11" height="11" rx="2" fill="#f58518"/>' % (LABEL_W + 120))
    s.append('<text x="%d" y="17" font-size="11" fill="#555">commits</text>' % (LABEL_W + 136))
    y = top
    for name in rows:
        lv = lines.get(name, 0)
        cv = commits.get(name, 0)
        lw = max(1, int(BAR_W * lv / lmax))
        cw = max(1, int(BAR_W * cv / cmax))
        s.append('<text x="%d" y="%d" font-size="12" fill="#333" text-anchor="end" '
                 'dominant-baseline="middle">%s</text>' % (LABEL_W - 8, y + rh // 2, _esc(name)))
        # lines-changed bar (top), commits bar (bottom), each scaled to its own max
        s.append('<rect x="%d" y="%d" width="%d" height="%d" rx="2" fill="#4c78a8"/>'
                 % (LABEL_W, y + 3, lw, bh))
        s.append('<text x="%d" y="%d" font-size="10" fill="#555" dominant-baseline="middle">%s</text>'
                 % (LABEL_W + lw + 5, y + 3 + bh // 2, _esc("{:,}".format(lv))))
        s.append('<rect x="%d" y="%d" width="%d" height="%d" rx="2" fill="#f58518"/>'
                 % (LABEL_W, y + 3 + bh + 2, cw, bh))
        s.append('<text x="%d" y="%d" font-size="10" fill="#555" dominant-baseline="middle">%s</text>'
                 % (LABEL_W + cw + 5, y + 3 + bh + 2 + bh // 2, _esc(cv)))
        y += rh
    s.append("</svg>")
    _write(path, s)


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, help="output directory for the SVGs")
    ap.add_argument("--src", default="src", help="source tree to scan (default: src)")
    ap.add_argument("--since", default="12 months ago",
                    help="git --since window for churn (default: '12 months ago')")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    loc = collect_loc(args.src)
    render_loc(os.path.join(args.out, "code_loc.svg"), loc)

    lines, commits = collect_churn(args.src, args.since)
    render_churn(os.path.join(args.out, "code_churn.svg"), lines, commits)

    print("code_charts: wrote code_loc.svg + code_churn.svg to %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
