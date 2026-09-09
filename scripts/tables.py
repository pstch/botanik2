#!/usr/bin/env python3
"""The site, from its tables.

    tables.py generate [site]   library.csv + tables/*.csv -> vars.gen.{h,cpp}, rows.gen.h
    tables.py config   [site]   site.toml + the tables     -> config.gen.h
    tables.py plant    [site]   programs.csv + ST          -> plant.st, plant.cpp, matiec
    tables.py points   [site]   the lot                    -> docs/<site>/reference/variables.md

`site` is a name under src/sites/, or a path. BOTANIK_SITE_DIR overrides it.

Offline: the output is committed and never built. `make check-generated` holds
the two together by regenerating into a scratch copy and diffing.

The generator owns the whole table. It merges the library's rows into the
site's, applies the site's overrides, orders the rows, assigns every index and
every generated register, resolves every name to an index, and checks. Nothing
it checks is checked again at runtime."""

import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sitegen import render
from sitegen.reference import points
from sitegen.model import (AUX, H, KIND, as_int, build, check, config_defaults,
                         emit_cals, emit_row, read_table, site_dir, site_guard)
from sitegen.st import enums, plant, quality_block

def generate(site):
    enums(site)
    t = build(site)
    check(t)
    table, tom = t['table'], t['toml']
    guard = site_guard(tom)

    order = sorted(range(len(table)), key=lambda i: table[i]['name'].lower())

    mdl = read_table(os.path.join(site, 'tables', 'modbus_models.csv'),
                     AUX['modbus_models'].split())
    dev = read_table(os.path.join(site, 'tables', 'modbus_devices.csv'),
                     AUX['modbus_devices'].split())

    # The lead comment a row may carry is already formatted; the template only
    # decides where it goes.
    def lead_of(r):
        out = []
        for ln in (r.get('lead') or '').split('\n'):
            if not ln: continue
            out.append('    ' + ln if ln.startswith('/*') else
                       '     ' + ln if ln.startswith('*') else ln)
        return out

    rows = [{'lead': lead_of(r), 'text': emit_row(r)} for r in table]
    chunks = [' '.join('%d,' % k for k in order[i:i + 12])
              for i in range(0, len(order), 12)]
    models = [{'symbol': m['symbol'], 'kind': m['kind'],
               'args': '"%s", %s' % (m['model_name'], m['regs'])
                       + (', %s' % m['words'] if m['words'] else '')} for m in mdl]

    render.write(os.path.join(site, 'generated', 'vars.gen.cpp'), 'vars.gen.cpp.j2',
                 banner=H.rstrip('\n'), cal_lines=emit_cals(t['cal']), rows=rows,
                 param_count=t['param_count'], order_chunks=chunks, models=models,
                 devices=dev, home=t['home'], system=t['system'])

    render.write(os.path.join(site, 'generated', 'vars.gen.h'), 'vars.gen.h.j2',
                 banner=H.rstrip('\n'), guard=guard,
                 cals=[c['cal'] for c in t['cal']], rows_count=len(table),
                 param_first=t['param_first'], param_count=t['param_count'],
                 models=[m['symbol'] for m in mdl],
                 home_count=len(t['home']), system_count=len(t['system']))

    render.write(os.path.join(site, 'generated', 'rows.gen.h'), 'rows.gen.h.j2',
                 banner=H.rstrip('\n'), guard=guard,
                 rows=[{'name': r['name'], 'index': r['_i']} for r in table])

    print('  vars.gen.cpp   %d rows (%d params), %d quality, %d order'
          % (len(table), t['param_count'],
             sum(1 for r in table if r['_fac'] == 'Q'), len(order)))


# ---- config.gen.h ----------------------------------------------------------

def prereqs():
    out = {}
    for m in re.finditer(r'#\s*error "BOTANIK_FEATURE_(\w+) requires BOTANIK_FEATURE_(\w+)"',
                         open('src/libbotanik_features.h').read()):
        out.setdefault(m.group(1), []).append(m.group(2))
    return out


def config(site):
    t = build(site)
    tom, dep = t['toml'], prereqs()
    table = t['table']
    # Named for the site: "BOTANIK_SITE_*" is the same symbol in every site's
    # generated header, so two of them in one translation unit would silently
    # skip the second.
    guard = site_guard(tom)

    defs = config_defaults()
    pool = 0
    for r in table:
        if r['_fac'] == 'PS': pool += as_int(r['cap'], defs)
    analog = sum(1 for r in table if r['_fac'] in ('AI', 'AO'))
    # Vars::mapped(): a register, and not one that addresses the field bus.
    mapped = sum(1 for r in table
                 if (r.get('reg') or '0').strip() not in ('', '0')
                 and KIND[r['_fac']] not in ('INPUT', 'OUTPUT'))

    val = [('SITE_NAME', '"%s"' % tom['site']['name']),
           ('SITE_CONTROL_MS', tom['rates']['control']),
           ('SITE_SENSE_MS', tom['rates']['sense']),
           # Computed rather than stated: the table is generated, so its size
           # is a fact about it rather than a ceiling to keep ahead of.
           ('VAR_MAX', len(table)),
           ('STR_POOL_BYTES', pool),
           ('ANALOG_ROWS_MAX', analog),
           ('MODBUS_TCP_MAX_ROWS', mapped),
           ('MODBUS_BAUD', tom['fieldbus']['baud'])]
    if tom.get('build', {}).get('pin_sizes'): val.append(('PIN_SIZES', 1))

    on = [f for f, v in tom['features'].items() if v]
    features = []
    for f in on:
        # VARS is dropped: every one of these needs it, and nesting it round
        # each would say so eighteen times.
        need = [d for d in dep.get(f, []) if d not in ('VARS',)]
        features.append({'name': f, 'needs': need, 'indent': '  ' * len(need)})

    render.write(os.path.join(site, 'generated', 'config.gen.h'),
                 'config.gen.h.j2', guard=guard, values=val, features=features)
    print('  config.gen.h   %d values, %d features' % (len(val), len(on)))



if __name__ == '__main__':
    cmd  = sys.argv[1] if len(sys.argv) > 1 else ''
    site = site_dir(sys.argv[2] if len(sys.argv) > 2 else None)
    if   cmd == 'generate': generate(site); quality_block()
    elif cmd == 'plant':    sys.exit(plant(site))
    elif cmd == 'config':   config(site)
    elif cmd == 'points':   points(site)
    else:                   sys.exit(__doc__)
