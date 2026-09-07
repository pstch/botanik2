#!/usr/bin/env python3
"""Per-area line coverage of libbotanik, off the gcda a coverage build left.

Reports, never fails: what it is for is noticing a fall, and the per-area table
says where.

Two sources this deliberately does not use, each of which gives a badly wrong
answer:

  - `gcov -n`'s per-file summary counts a header's inline bodies against
    whichever .cpp pulled them in.
  - Headers themselves: gcov attributes a header's inline code to one
    translation unit and reports every other TU that included it as zero.

So: -p to write the per-source .gcov, then count the annotated lines. Without -p
the paths are not mangled into the name and `shell/shell.cpp` collides with
`pages/shell.cpp`.
"""

import argparse, glob, os, re, shutil, subprocess, sys

# An annotated line starts with its execution count, or with ##### for one never
# reached. A '-' line and the header block (numbered 0) carry no coverage and
# stay out of the denominator.
ALL_LINE = re.compile(r'^ *(#####|=====|[0-9]+\*?) *: *[1-9]')
HIT_LINE = re.compile(r'^ *[0-9]+\*? *: *[1-9]')


def say(msg):
    print('-- ' + msg)


def counts(report):
    all_n = hit_n = 0
    with open(report, errors='replace') as fh:
        for line in fh:
            if ALL_LINE.match(line):
                all_n += 1
                if HIT_LINE.match(line):
                    hit_n += 1
    return hit_n, all_n


def area_of(report):
    """The subsystem a mangled .gcov name belongs to; '#' stands for '/'."""
    rest = report.rsplit('#libbotanik#', 1)[-1]
    return rest.split('#')[0] if '#' in rest else '(root)'


def pct(hit, all_n):
    tenths = (hit * 1000) // all_n
    return tenths // 10, tenths % 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gcov', default='gcov')
    ap.add_argument('--tree', required=True)
    ap.add_argument('--src', required=True)
    ap.add_argument('--out')
    a = ap.parse_args()

    # Absolute: gcov runs in a working directory below the tree, so a relative
    # gcda path would not resolve from there.
    a.tree = os.path.abspath(a.tree)
    a.src = os.path.abspath(a.src)

    # Sorted: where one source is compiled into more than one object, gcov
    # overwrites the report per gcda, so the order decides which run is
    # described. Fixing it keeps the number reproducible.
    gcda = sorted(p for p in glob.glob(os.path.join(a.tree, '**', '*.gcda'),
                                       recursive=True)
                  if '/libbotanik/' in p)
    if not gcda:
        say('coverage: no profile data under %s - was the suite run?' % a.tree)
        return 0

    work = os.path.join(a.tree, 'gcov')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    rc = subprocess.call([a.gcov, '-p', '-r', '-s', a.src] + gcda, cwd=work,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if rc != 0:
        say('coverage: %s failed' % a.gcov)
        return 0

    areas = {}
    for report in glob.glob(os.path.join(work, '*#libbotanik#*.cpp.gcov')):
        hit, all_n = counts(report)
        if all_n == 0:
            continue
        h, n = areas.get(area_of(report), (0, 0))
        areas[area_of(report)] = (h + hit, n + all_n)

    total_hit = sum(h for h, _ in areas.values())
    total_all = sum(n for _, n in areas.values())
    if total_all == 0:
        say('coverage: nothing matched src/libbotanik/**.cpp')
        return 0

    say('coverage: library .cpp lines, from %s' % a.tree)
    rows = []
    for name in sorted(areas):
        hit, all_n = areas[name]
        whole, tenth = pct(hit, all_n)
        say('  %s%s%d.%d%%  (%d / %d)'
            % (name, ' ' * max(1, 13 - len(name)), whole, tenth, hit, all_n))
        rows.append('    "%s": { "covered": %d, "lines": %d }' % (name, hit, all_n))

    whole, tenth = pct(total_hit, total_all)
    say('coverage: TOTAL %d.%d%% (%d / %d)' % (whole, tenth, total_hit, total_all))

    if a.out:
        with open(a.out, 'w') as fh:
            fh.write('{\n  "covered": %d,\n  "lines": %d,\n  "pct": "%d.%d",\n'
                     '  "areas": {\n%s\n  }\n}\n'
                     % (total_hit, total_all, whole, tenth, ',\n'.join(rows)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
