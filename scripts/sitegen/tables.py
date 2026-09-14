#!/usr/bin/env python3
"""The site, from its tables.

    sitegen/tables.py codegen <site|-> <hash>          every command below the site (or `-`,
                                                       the library) needs, then its stamp
    sitegen/tables.py check   <site> <hash> <scratch>  codegen into a copy, diffed against the site

    sitegen/tables.py generate [site]   library.csv + tables/*.csv -> enums, vars.gen.{h,cpp}, rows.gen.h
    sitegen/tables.py config   [site]   site.toml + the tables     -> config.gen.h
    sitegen/tables.py plant    [site]   programs.csv + ST          -> enums, plant.st, plant.cpp, matiec
    sitegen/tables.py library           library.csv + lib headers  -> quality/health/rows_none.gen.h

`codegen` also writes the site's reference page, ../docs/generated/variables.md
beside the site. `site` is a name under examples/, or a path. What it reads out
of the library is anchored to this checkout; what it writes for a site goes
beside the site."""

import filecmp, os, re, shutil, sys

# Runnable as a path (`python3 .../sitegen/tables.py`), so the package's parent
# has to be importable before the package is.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    from sitegen import render
except ImportError as e:
    sys.exit('the generator needs jinja2 (pip install jinja2): %s' % e)
from sitegen.paths import lib
from sitegen.reference import points
from sitegen.model import (H, KIND, as_int, build, check, config_defaults,
                           emit_cals, emit_row, site_dir, site_guard)
from sitegen.st import enums, plant, quality_block

def generate(site, t):
    table = t['table']
    order = sorted(range(len(table)), key=lambda i: table[i]['name'].lower())

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
                       + (', %s' % m['words'] if m['words'] else '')} for m in t['models']]
    guard = site_guard(t['toml'])

    render.write(os.path.join(site, 'generated', 'vars.gen.cpp'), 'vars.gen.cpp.j2',
                 banner=H, cal_lines=emit_cals(t['cal']), rows=rows,
                 param_count=t['param_count'], order_chunks=chunks, models=models,
                 devices=t['dev'], home=t['home'], system=t['system'])

    render.write(os.path.join(site, 'generated', 'vars.gen.h'), 'vars.gen.h.j2',
                 banner=H, guard=guard,
                 cals=[c['cal'] for c in t['cal']], rows_count=len(table),
                 param_count=t['param_count'],
                 models=[m['symbol'] for m in t['models']],
                 home_count=len(t['home']), system_count=len(t['system']))

    abi = re.search(r'^#define BOTANIK_CODEGEN_ABI (\d+)$',
                    open(lib('src', 'libbotanik', 'codegen_abi.h')).read(), re.M).group(1)
    render.write(os.path.join(site, 'generated', 'rows.gen.h'), 'rows.gen.h.j2',
                 banner=H, guard=guard, abi=abi,
                 rows=[{'name': r['name'], 'index': r['_i']} for r in table])

    print('  vars.gen.cpp   %d rows (%d params), %d quality, %d order'
          % (len(table), t['param_count'],
             sum(1 for r in table if r['_fac'] == 'Q'), len(order)))


# ---- config.gen.h ----------------------------------------------------------

def prereqs():
    out = {}
    for m in re.finditer(r'#\s*error "BOTANIK_FEATURE_(\w+) requires BOTANIK_FEATURE_(\w+)"',
                         open(lib('src', 'libbotanik_features.h')).read()):
        out.setdefault(m.group(1), []).append(m.group(2))
    return out


def config(site, t):
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

    on  = [f for f, v in tom['features'].items() if v]
    # A flag the site turns OFF. Only the ones defaulting ON need saying, but
    # the site states its whole profile and the redundant ones cost a #ifndef.
    off = [f for f, v in tom['features'].items() if not v]
    features = [{'name': f, 'needs': dep.get(f, [])} for f in on]

    render.write(os.path.join(site, 'generated', 'config.gen.h'),
                 'config.gen.h.j2', guard=guard, values=val,
                 features=features, features_off=off)
    print('  config.gen.h   %d values, %d on, %d off'
          % (len(val), len(on), len(off)))



# ---- the build's entry points -----------------------------------------------

STAMP = ('# Input hash of this generated tree, written by the build.\n'
         '# The build regenerates when it stops matching. Commit it with the output.\n'
         '%s\n')


def codegen(site, stamp):
    """Every command a tree needs, then the stamp; site None is the library."""
    if site is None:
        quality_block()
        path = lib('src', 'libbotanik', 'codegen.stamp')
    else:
        t = build(site)
        check(t)
        enums(site, t)
        generate(site, t)
        config(site, t)
        plant(site, t)
        points(site, t)
        path = os.path.join(site, 'generated', 'codegen.stamp')
    with open(path, 'w') as fh: fh.write(STAMP % stamp)


def check_generated(site, stamp, scratch):
    """A copy of the site without its output, regenerated from scratch, so a
    committed file that is no longer generated has nothing to compare equal to."""
    name = os.path.basename(os.path.dirname(site))
    copy = os.path.join(scratch, name, 'src')
    shutil.rmtree(os.path.dirname(copy), ignore_errors=True)
    shutil.copytree(site, copy, ignore=shutil.ignore_patterns('generated'))
    codegen(copy, stamp)
    old, new = os.path.join(site, 'generated'), os.path.join(copy, 'generated')
    names = sorted(set(os.listdir(old)) | set(os.listdir(new)))
    _same, differ, missing = filecmp.cmpfiles(old, new, names, shallow=False)
    bad = differ + missing
    page = os.path.join('docs', 'generated', 'variables.md')
    committed = os.path.join(os.path.dirname(site), page)
    if not (os.path.exists(committed) and
            filecmp.cmp(committed, os.path.join(os.path.dirname(copy), page), shallow=False)):
        bad.append('../' + page)
    shutil.rmtree(os.path.dirname(copy))
    if bad: sys.exit('%s differs from a fresh generation: %s' % (old, ' '.join(bad)))
    print('check-generated: %s matches' % name)


def _site(args):
    return site_dir(args[0] if args else None)


def _generate(args):
    site = _site(args)
    t = build(site)
    check(t)
    enums(site, t)
    generate(site, t)


def _plant(args):
    site = _site(args)
    t = build(site)
    enums(site, t)
    plant(site, t)


COMMANDS = {
    'codegen':  lambda a: codegen(None if a[0] == '-' else site_dir(a[0]), a[1]),
    'check':    lambda a: check_generated(site_dir(a[0]), a[1], a[2]),
    'generate': _generate,
    'config':   lambda a: config(_site(a), build(_site(a))),
    'plant':    _plant,
    'library':  lambda a: quality_block(),
}


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd not in COMMANDS: sys.exit(__doc__)
    COMMANDS[cmd](sys.argv[2:])
