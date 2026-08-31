#!/usr/bin/env python3
"""The site's tables, as data.

Two directions:

    tables.py export    vars.cpp        -> src/sites/<site>/tables/*.csv
    tables.py generate  tables/*.csv    -> the C++ glue and plant.st's globals

`export` runs ONCE, to bootstrap. It exists so the migration can be proved
rather than trusted: export, generate, and require the argument lists to match
the ones parsed out of the hand-written file. Nothing is deleted before that
diff is empty.

The generator is NOT a build step. It runs when a table changes and its output
is committed, the way iec2c's already is.
"""

import csv, os, re, sys

SITE = 'src/sites/gh_mlb41'
PAD_MAX = 34   # widest a column is padded to; see write_table()

# One CSV per Descriptor factory: ten factories, ten signatures, ten files.
# The column names ARE the factory's parameter names, so a row reads the same
# either side of the migration.
FACTORIES = {
    'P':   ('params',      'name type def lo hi digits unit reg desc notes writable retained'),
    'SV':  ('status',      'name type def digits unit reg desc notes retained'),
    'SVE': ('status_enum', 'name def label reg desc notes'),
    'A':   ('alarms',      'name code on_s off_s recent_s hold_s reg desc notes'),
    'MI':  ('modbus_in',   'name type device fc reg regs lo hi def timeout_ms digits unit desc notes poll_ms word_swap'),
    'MO':  ('modbus_out',  'name type device fc reg regs safe digits unit desc notes word_swap'),
    'AI':  ('analog_in',   'name cal device fc reg regs wire def timeout_ms digits unit desc notes poll_ms word_swap'),
    'AO':  ('analog_out',  'name cal device fc reg regs wire safe digits unit desc notes word_swap'),
    'DI':  ('digital_in',  'name device fc reg def timeout_ms invert desc notes poll_ms pulse'),
    'DO':  ('digital_out', 'name device fc reg safe invert desc notes'),
}


def split_args(s):
    """Split a factory's argument list on top-level commas.

    Nested parens - SF(0.0f), a cast - and string literals both contain
    commas that are not separators."""
    out, depth, cur, in_str, esc = [], 0, '', False, False
    for ch in s:
        if esc:                      cur += ch; esc = False; continue
        if ch == '\\' and in_str:    cur += ch; esc = True;  continue
        if ch == '"':                in_str = not in_str; cur += ch; continue
        if in_str:                   cur += ch; continue
        if ch in '([':               depth += 1
        elif ch in ')]':             depth -= 1
        if ch == ',' and depth == 0: out.append(cur.strip()); cur = ''; continue
        cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


def parse(path):
    """Every factory call in @p path, as (factory, [args]) in file order."""
    src = open(path).read()
    rows = []
    for m in re.finditer(r'^\s*(' + '|'.join(FACTORIES) + r')\((.*)$', src, re.M):
        fac, rest = m.group(1), m.group(2)
        # A call may wrap; take text up to the closing paren at depth 0.
        depth, end, in_str, esc = 1, None, False, False
        for i, ch in enumerate(rest):
            if esc:                   esc = False; continue
            if ch == '\\' and in_str: esc = True;  continue
            if ch == '"':             in_str = not in_str; continue
            if in_str:                continue
            if ch == '(':             depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0: end = i; break
        if end is None: sys.exit("unterminated %s( at %s" % (fac, m.group(0)[:60]))
        rows.append((fac, split_args(rest[:end])))
    return rows


def unquote(a):
    """A C++ string literal as its text; anything else unchanged."""
    if len(a) >= 2 and a[0] == '"' and a[-1] == '"':
        return a[1:-1].replace('\\"', '"').replace('\\\\', '\\')
    return a


def quote(v, col):
    """Back to a C++ literal. The text columns are strings; the rest are
    already C++ - an enum, a cast, a pointer, a number."""
    if col in ('name', 'unit', 'desc', 'notes', 'code'):
        return '"%s"' % v.replace('\\', '\\\\').replace('"', '\\"')
    return v


def field(v):
    """One value as CSV text: quoted only where it has to be."""
    if any(c in v for c in ',"\n') or (v and v != v.strip()):
        return '"%s"' % v.replace('"', '""')
    return v


def write_table(path, cols, records):
    """A CSV a person can read: every column padded to its widest cell.

    The padding is OUTSIDE any quotes, so it is not part of the value, and
    the reader strips it. A value that genuinely needed surrounding spaces
    would not survive that - export() refuses one rather than corrupting it.
    """
    cells = [[field(v) for v in rec] for rec in records]
    w = [len(c) for c in cols]
    for row in cells:
        for i, c in enumerate(row): w[i] = max(w[i], len(c))
    # A column is padded to line up, not to the width of its longest cell: one
    # 300-character note would otherwise push every row off the screen. Past
    # the cap the column simply is not aligned, which is the lesser harm.
    w = [min(x, PAD_MAX) for x in w]
    def line(row):
        # No trailing spaces on the last column: they are invisible churn in
        # a diff and mean nothing to a reader.
        out = [c.ljust(w[i]) for i, c in enumerate(row[:-1])] + [row[-1]]
        return ','.join(out).rstrip() + '\n'
    with open(path, 'w') as fh:
        fh.write(line(cols))
        for row in cells: fh.write(line(row))


def export(site):
    out = os.path.join(site, 'tables')
    os.makedirs(out, exist_ok=True)
    rows = parse(os.path.join(site, 'vars.cpp'))
    per = {f: [] for f in FACTORIES}
    for fac, args in rows: per[fac].append(args)
    for fac, (stem, cols) in FACTORIES.items():
        cols = cols.split()
        recs = []
        for args in per[fac]:
            # Trailing defaults are omitted in the source; pad so every row has
            # the full width and the table is rectangular.
            vals = [unquote(a) for a in args] + [''] * (len(cols) - len(args))
            for v in vals:
                if v != v.strip():
                    sys.exit("%s: %r has edge whitespace, which the aligned "
                             "format cannot carry" % (stem, v))
            recs.append(vals[:len(cols)])
        write_table(os.path.join(out, stem + '.csv'), cols, recs)
        print("  %-16s %3d rows" % (stem + '.csv', len(recs)))


def load(site):
    """Every CSV back as (factory, [c++ args]), trailing blanks dropped."""
    out = []
    for fac, (stem, cols) in FACTORIES.items():
        cols = cols.split()
        path = os.path.join(site, 'tables', stem + '.csv')
        if not os.path.exists(path): continue
        with open(path, newline='') as fh:
            rd = csv.reader(fh)
            head = [h.strip() for h in next(rd)]
            if head != cols:
                sys.exit("%s: header is %s, expected %s" % (path, head, cols))
            for raw in rd:
                if not raw or all(c.strip() == '' for c in raw): continue
                rec = dict(zip(head, [c.strip() for c in raw]))
                args = [quote(rec.get(c, ''), c) for c in cols]
                while args and args[-1] == '': args.pop()
                out.append((fac, args))
    return out


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'export':
        export(SITE)
    elif cmd == 'check':
        # The round trip, which is the whole safety argument: what the CSVs
        # say must be exactly what the hand-written file says.
        was = {}
        for fac, args in parse(os.path.join(SITE, 'vars.cpp')):
            was.setdefault(fac, []).append(args)
        now = {}
        for fac, args in load(SITE):
            now.setdefault(fac, []).append(args)
        bad = 0
        for fac in FACTORIES:
            a, b = was.get(fac, []), now.get(fac, [])
            if len(a) != len(b):
                print("  %s: %d rows in vars.cpp, %d in CSV" % (fac, len(a), len(b))); bad += 1; continue
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print("  %s[%d] differs:\n    cpp: %s\n    csv: %s" % (fac, i, x, y))
                    bad += 1
                    if bad > 5: sys.exit(1)
        print("ROUND TRIP CLEAN" if not bad else "MISMATCH")
        sys.exit(1 if bad else 0)
    else:
        sys.exit(__doc__)
