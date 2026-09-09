#!/usr/bin/env python3
"""docs/<site>/reference/variables.md: every row the firmware exposes, from
the tables and the ST itself."""

import os, re

from . import render
from .model import KIND, build, var_externals

# ---- the reference page ----------------------------------------------------

KINDS = [
    ('ALARM',  'Alarms',     'the boolean an AlarmCondition reads',
     'name code on_s off_s recent_s hold_s reg desc notes'),
    ('PARAM',  'Parameters', 'persisted, editable',
     'name type def accepts unit access reg desc notes'),
    ('STATUS', 'Status values', 'computed by a law; published when it has a register',
     'name type def digits unit reg desc notes'),
    ('INPUT',  'Inputs',     'read from the field; each has a quality companion',
     'name type device fc reg regs unit desc notes'),
    ('OUTPUT', 'Outputs',    'driven to the field',
     'name type device fc reg safe desc notes'),
    ('QUALITY', 'Quality companions', 'the quality of the row before it',
     'name reg desc'),
]

# The CSV's keys are terse where a reference table wants words.
HEADINGS = {'def': 'default', 'desc': 'description', 'cal': 'calibration',
            'on_s': 'on (s)', 'off_s': 'off (s)', 'recent_s': 'recent (s)',
            'hold_s': 'hold (s)', 'regs': 'registers'}
# What a factory pins where the CSV does not carry it.
FAC_TYPE = {'A': 'T_BOOL', 'PE': 'T_ENUM', 'PS': 'T_STR', 'SVE': 'T_ENUM',
            'AI': 'T_F32', 'AO': 'T_F32', 'DI': 'T_BOOL', 'DO': 'T_BOOL',
            'RO': 'T_BOOL', 'PI': 'T_U32', 'Q': 'T_ENUM'}


def _md(v):
    v = (v or '').strip().strip('"')
    return v.replace('|', '\\|').replace('\n', ' ') if v else '-'


def _wire(r, col):
    """A wire field the CSV does not carry, because declare.h fixes it.

    RO() builds { coil, device, 5, 1, ... }: a relay is FC05, write single
    coil, at the coil number, so relay_out.csv states neither. PI() builds
    { channel, 0, edge, 1, ... }: a pulse terminal is local, and the slot a
    function code would sit in carries the EDGE instead, so it has none."""
    if r['_fac'] == 'RO':
        return {'fc': '5', 'reg': r.get('coil'), 'regs': '1'}.get(col)
    if r['_fac'] == 'PI':
        return {'device': '0', 'reg': r.get('channel'), 'regs': '1'}.get(col)
    return None


def _point_row(r, want):
    lo, hi = r.get('lo', ''), r.get('hi', '')
    out = []
    for c in want.split():
        if   c == 'accepts': out.append('%s .. %s' % (_md(lo), _md(hi))
                                        if lo or hi else '-')
        elif c == 'access':  out.append('RW' if r.get('writable', 'true') != 'false' else 'RO')
        elif c == 'type':    out.append(_md(r.get('type') or FAC_TYPE.get(r['_fac'], '')))
        else:                out.append(_md(r.get(c) or _wire(r, c)))
    return out


_IDX = re.compile(r'^(.*?)(\d+)(.*)$')


def _collapse(names, least=2):
    """Fold an indexed family - x1, x2, x3 - into x1..3.

    A site declares a row per tray, per zone, per probe, and a law that reads
    eight of them says nothing more by naming all eight. The index is the FIRST
    run of digits, so grid_L1_V groups on grid_L + _V rather than on the unit."""
    groups, order = {}, []
    for n in names:
        m = _IDX.match(n)
        key = (m.group(1), m.group(3)) if m else (n, None)
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(int(m.group(2)) if m else None)

    out = []
    for pre, suf in order:
        idx = groups[(pre, suf)]
        if suf is None or len(idx) < least:
            out += [pre if i is None else '%s%d%s' % (pre, i, suf) for i in idx]
            continue
        lo, hi = min(idx), max(idx)
        if sorted(idx) == list(range(lo, hi + 1)):
            out.append('%s%d..%d%s' % (pre, lo, hi, suf))
        else:
            out.append('%s{%s}%s'
                       % (pre, ','.join(str(i) for i in sorted(idx)), suf))
    return out


def _st_header(path):
    """The comment block at the top of an ST file, minus its banner.

    Every program opens with `(* <name> program` and a rule, which says nothing
    the heading above it does not. What is left is the author's own."""
    try:
        s = open(path).read()
    except IOError:
        return []
    m = re.match(r'\s*\(\*(.*?)\*\)', s, re.S)
    if not m:
        return []
    out = [ln.strip() for ln in m.group(1).strip().split('\n')
           if not re.match(r'^-{4,}$', ln.strip())]
    if out and re.match(r'^\w+ program$', out[0]):
        out.pop(0)
    while out and not out[0]:
        out.pop(0)
    while out and not out[-1]:
        out.pop()
    return out


def _table(rows, want):
    head = [HEADINGS.get(c, c) for c in want.split()]
    return (['| ' + ' | '.join(head) + ' |', '|' + '---|' * len(head)]
            + ['| ' + ' | '.join(r) + ' |' for r in rows])


def points(site):
    t = build(site)
    table, prog = t['table'], t['prog']
    name = t['toml']['site']['name']
    n_lib = sum(1 for r in table if r.get('_lib'))
    # The site's DECLARED name, not its directory: a site repository keeps its
    # site under src/, and `docs/src/` would be nobody's idea of a page. The
    # hand-written pages beside it are anchored the same way, so
    # `@subpage <site>_variables` resolves from the site's index.
    slug = name
    group = dict((k, [r for r in table if KIND[r['_fac']] == k])
                 for k, _t, _b, _w in KINDS)
    summary = [{'title': title, 'blurb': blurb, 'count': len(group[k])}
               for k, title, blurb, _w in KINDS]
    sections = [{'title': title, 'kind': k, 'blurb': blurb,
                 'table': _table([_point_row(r, want) for r in group[k]], want)}
                for k, title, blurb, want in KINDS if group[k]]

    cal = None
    if t['cal']:
        keys = ('cal', 'raw_min_row', 'raw_min', 'raw_max_row', 'raw_max',
                'eng_min_row', 'eng_min', 'eng_max_row', 'eng_max')
        cal = _table([[_md(c[k]) for k in keys] for c in t['cal']],
                     'block raw_min_row raw_min raw_max_row raw_max '
                     'eng_min_row eng_min eng_max_row eng_max')

    programs = []
    for e in prog:
        ext = var_externals(os.path.join(site, e['source']))
        # Quality companions are left out. A law reads and writes them beside
        # the rows they speak for, so listing both says nothing the pairing does
        # not, and doubles the table.
        ext = [x for x in ext if not x[0].endswith('_qlt')]
        reads = [n for n, _t, ro in ext if ro]
        writes = [n for n, _t, ro in ext if not ro]
        programs.append({
            'name': e['name'], 'source': e['source'], 'rate': e['rate'],
            'rate_upper': e['rate'].upper(), 'desc': _md(e['desc']),
            'n_reads': len(reads), 'n_writes': len(writes),
            'header': _st_header(os.path.join(site, e['source'])),
            'reads': ', '.join('`%s`' % r for r in _collapse(reads)) or '-',
            'writes': ', '.join('`%s`' % w for w in _collapse(writes)) or '-'})

    out = os.path.join('docs', slug, 'reference', 'variables.md')
    render.write(out, 'variables.md.j2', slug=slug, n_rows=len(table),
                 n_site=len(table) - n_lib, n_lib=n_lib, summary=summary,
                 sections=sections, cal=cal, programs=programs)
    print('  variables.md   %d rows, %d programs' % (len(table), len(prog)))
