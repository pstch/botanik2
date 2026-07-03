#!/usr/bin/env python3
"""Generate the code charts shown in the docs (Library + Testing pages).

Four charts, all bucketed by "code area" (the folder under src/):
  - code_loc.svg     : code lines per area (cloc, code only - no comments/blanks).
  - code_churn.svg   : per area, lines changed (git add+del) AND commits touched,
                       over a trailing window (default 12 months).
  - code_asserts.svg : per area, BOTANIK_ASSERT() call sites AND POST structural checks.
  - code_tests.svg   : per area, unit-test cases (doctest TEST_CASE) AND pyte
                       screen-test functions.

Stdlib only; shells out to `cloc` (LoC) and `git` (churn). If either is missing
(or there is no git history), the affected chart is written as a placeholder so the
Doxygen build never hits a missing-image warning. Output is build-only and
gitignored; the CMake `docs` target runs this before doxygen.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

# --------------------------------------------------------------------------
# Area mapping - one definition shared by every chart
# --------------------------------------------------------------------------

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


# Each test file maps to the code area it exercises (the test name rarely matches
# the area verbatim, e.g. test_serial -> utils). Add a row when a test file is
# added; unmapped files fall into "other" so they stay visible.
TEST_AREA = {
    "test_alarms":         "alarms",
    "test_config_handler": "core",
    "test_filters":        "logs",
    "test_line_buffer":    "pages",
    "test_logger":         "logs",
    "test_parser":         "console",
    "test_resolver":       "network",
    "test_serial":         "utils",
    "test_shell":          "shell",
    "test_sizes":          "core",
    "test_sun":            "utils",
    "test_text":           "utils",
    "test_time":           "utils",
    "test_writer":         "logs",
}


# POST structural checks (HealthMonitor::_post in health/health.h), counted by the
# area each one guards. A fixed handful, so it's a small hand-maintained map - update
# it if a POST check is added or removed: alarm-code resolution -> alarms; PLC region
# non-null + 32-byte aligned -> plcio (2); logger-has-writers -> logs.
POST_CHECKS = {"alarms": 1, "plcio": 2, "logs": 1}


# Historical path renames, applied (in order) before area-mapping so churn from a
# file's pre-rename life attributes to its current area. `-M` only rewrites the path
# at the rename commit itself; earlier commits carry the literal old path. Add a row
# here when a directory/file is renamed and its old name still falls in the window.
PATH_ALIASES = (
    ("src/libbotanix/", "src/libbotanik/"),                       # lib dir typo fix
    ("src/libbotanix_config.h", "src/libbotanik_config.h"),
    ("src/libbotanix.h", "src/libbotanik.h"),
    ("src/libbotanik/panes/", "src/libbotanik/pages/"),           # panes -> pages
    ("src/libbotanik/terminal/", "src/libbotanik/console/"),      # terminal -> console
    ("src/libbotanik_asserts.h", "src/libbotanik/utils/assert.h"),  # asserts -> utils
    # emulation moved out of the library into the sim harness (plcio/network -> botanik_sim)
    ("src/libbotanik/plcio/emulator.h",   "src/botanik_sim/emulator.h"),
    ("src/libbotanik/plcio/emulator.cpp", "src/botanik_sim/emulator.cpp"),
    ("src/libbotanik/network/sim_netd.h", "src/botanik_sim/network.h"),
)


def normalize_path(path):
    """Resolve a git-numstat path to its current canonical location: strip any
    rename syntax ('old => new' and the brace form 'pre/{old => new}/post'), then
    fold the known historical renames so old paths land in current areas.
    """
    if "=>" in path:
        if "{" in path and "}" in path:
            pre, rest = path.split("{", 1)
            mid, post = rest.split("}", 1)
            path = pre + mid.split("=>", 1)[1].strip() + post
        else:
            path = path.split("=>", 1)[1].strip()
    for old, new in PATH_ALIASES:
        if old.endswith("/"):
            if path.startswith(old):
                path = new + path[len(old):]
        elif path == old:
            path = new
    return path


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
        area = area_for(normalize_path(path))
        if area is None:
            continue
        lines[area] = lines.get(area, 0) + int(add) + int(dele)
        touched.add(area)
    for a in touched:  # final commit (no trailing marker)
        commits[a] = commits.get(a, 0) + 1
    return lines, commits


def collect_asserts(src):
    """{area: BOTANIK_ASSERT call sites}. Macro-definition lines are skipped, so the
    macro's own header doesn't count as a usage."""
    counts = {}
    for root, _dirs, files in os.walk(src):
        for fn in files:
            if not fn.endswith((".h", ".cpp", ".c")):
                continue
            path = os.path.join(root, fn).replace(os.sep, "/")
            area = area_for(path)
            if area is None:
                continue
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    n = sum(1 for line in f
                            if "BOTANIK_ASSERT(" in line and "define" not in line)
            except OSError:
                continue
            if n:
                counts[area] = counts.get(area, 0) + n
    return counts


def collect_tests(tests_dir):
    """({area: unit cases}, {area: pyte tests}). Unit = doctest TEST_CASE in
    tests/test_*.cpp (mapped via TEST_AREA); pyte = def test_* in tests/pyte/, all
    attributed to `pages` (the pyte suite drives the page screens)."""
    unit = {}
    case_re = re.compile(r"\bTEST_CASE\b")
    try:
        names = os.listdir(tests_dir)
    except OSError:
        names = []
    for fn in names:
        if not (fn.startswith("test_") and fn.endswith(".cpp")):
            continue
        area = TEST_AREA.get(fn[:-4], "other")
        try:
            with open(os.path.join(tests_dir, fn), encoding="utf-8", errors="replace") as f:
                n = sum(1 for line in f if case_re.search(line))
        except OSError:
            continue
        unit[area] = unit.get(area, 0) + n

    pyte = {}
    pyte_dir = os.path.join(tests_dir, "pyte")
    pyte_re = re.compile(r"^\s*def\s+test_")
    try:
        pnames = os.listdir(pyte_dir)
    except OSError:
        pnames = []
    for fn in pnames:
        if not (fn.startswith("test_") and fn.endswith(".py")):
            continue
        try:
            with open(os.path.join(pyte_dir, fn), encoding="utf-8", errors="replace") as f:
                n = sum(1 for line in f if pyte_re.match(line))
        except OSError:
            continue
        pyte["pages"] = pyte.get("pages", 0) + n
    return unit, pyte


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


def render_bars(path, items, missing_msg):
    """Single-series horizontal bar chart, sorted descending."""
    if not items:
        _placeholder(path, missing_msg)
        return
    rows = sorted(items.items(), key=lambda kv: kv[1], reverse=True)
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


def render_grouped(path, a, b, legend_a, legend_b, missing_msg):
    """Two-series grouped horizontal bar chart; each series scaled to its own max so
    both stay visible even at very different magnitudes. Sorted by series `a`."""
    if a is None:
        _placeholder(path, missing_msg)
        return
    areas = set(a) | set(b)
    if not areas:
        _placeholder(path, missing_msg)
        return
    rows = sorted(areas, key=lambda k: a.get(k, 0), reverse=True)
    top = 30           # room for the legend
    rh = 30            # two sub-bars per area
    bh = 10
    h = top + len(rows) * rh + 10
    amax = max((a.get(k, 0) for k in rows), default=1) or 1
    bmax = max((b.get(k, 0) for k in rows), default=1) or 1
    s = _svg_open(WIDTH, h)
    # legend
    s.append('<rect x="%d" y="8" width="11" height="11" rx="2" fill="#4c78a8"/>' % LABEL_W)
    s.append('<text x="%d" y="17" font-size="11" fill="#555">%s</text>' % (LABEL_W + 16, _esc(legend_a)))
    s.append('<rect x="%d" y="8" width="11" height="11" rx="2" fill="#f58518"/>' % (LABEL_W + 120))
    s.append('<text x="%d" y="17" font-size="11" fill="#555">%s</text>' % (LABEL_W + 136, _esc(legend_b)))
    y = top
    for name in rows:
        av = a.get(name, 0)
        bv = b.get(name, 0)
        aw = max(1, int(BAR_W * av / amax))
        bw = max(1, int(BAR_W * bv / bmax))
        s.append('<text x="%d" y="%d" font-size="12" fill="#333" text-anchor="end" '
                 'dominant-baseline="middle">%s</text>' % (LABEL_W - 8, y + rh // 2, _esc(name)))
        s.append('<rect x="%d" y="%d" width="%d" height="%d" rx="2" fill="#4c78a8"/>'
                 % (LABEL_W, y + 3, aw, bh))
        s.append('<text x="%d" y="%d" font-size="10" fill="#555" dominant-baseline="middle">%s</text>'
                 % (LABEL_W + aw + 5, y + 3 + bh // 2, _esc("{:,}".format(av))))
        s.append('<rect x="%d" y="%d" width="%d" height="%d" rx="2" fill="#f58518"/>'
                 % (LABEL_W, y + 3 + bh + 2, bw, bh))
        s.append('<text x="%d" y="%d" font-size="10" fill="#555" dominant-baseline="middle">%s</text>'
                 % (LABEL_W + bw + 5, y + 3 + bh + 2 + bh // 2, _esc("{:,}".format(bv))))
        y += rh
    s.append("</svg>")
    _write(path, s)


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, help="output directory for the SVGs")
    ap.add_argument("--src", default="src", help="source tree to scan (default: src)")
    ap.add_argument("--tests", default="tests", help="tests tree to scan (default: tests)")
    ap.add_argument("--since", default="12 months ago",
                    help="git --since window for churn (default: '12 months ago')")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    loc = collect_loc(args.src)
    render_bars(os.path.join(args.out, "code_loc.svg"), loc,
                "LoC chart unavailable (cloc not found)")

    lines, commits = collect_churn(args.src, args.since)
    render_grouped(os.path.join(args.out, "code_churn.svg"), lines, commits,
                   "lines changed", "commits", "Churn chart unavailable (no git history)")

    asserts = collect_asserts(args.src)
    render_grouped(os.path.join(args.out, "code_asserts.svg"), asserts, POST_CHECKS,
                   "BOTANIK_ASSERT", "POST checks", "no self-checks found")

    unit, pyte = collect_tests(args.tests)
    render_grouped(os.path.join(args.out, "code_tests.svg"), unit, pyte,
                   "unit tests", "pyte tests", "no tests found")

    print("code_charts: wrote code_loc / code_churn / code_asserts / code_tests to %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
