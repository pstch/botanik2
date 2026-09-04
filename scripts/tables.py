#!/usr/bin/env python3
"""The site, from its tables.

    tables.py generate [site]   library.csv + tables/*.csv -> vars.gen.{h,cpp}, rows.gen.h
    tables.py config   [site]   site.toml + the tables     -> config.gen.h
    tables.py plant    [site]   programs.csv + ST          -> plant.st, plant.cpp, matiec
    tables.py points   [site]   the lot                    -> docs/reference/points.md

`site` is a name under src/sites/, or a path. BOTANIK_SITE_DIR overrides it.

Offline: the output is COMMITTED and never built. `make check-generated` is what
holds the two together - it regenerates into a scratch copy and diffs.

The generator owns the whole table. It merges the library's rows into the site's,
applies the site's overrides, orders the rows, assigns every index and every
generated register, resolves every name to an index, and CHECKS. Nothing it
checks is checked again at runtime."""

import os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from points_md import points
from site_tables import (AUX, H, KIND, as_int, build, check, config_defaults,
                         emit_cals, emit_row, read_table, site_dir, site_guard)
from st_plant import enums, plant, quality_block

def generate(site):
    enums(site)
    t = build(site)
    check(t)
    table, tom = t['table'], t['toml']
    guard = site_guard(tom)

    order = sorted(range(len(table)), key=lambda i: table[i]['name'].lower())

    L = [H, '', '#include "libbotanik.h"', '',
         '// Outside the namespace: it opens its own, and the ST binding in it',
         '// is not a site-scoped thing.',
         '#include "enums.gen.h"', '',
         'namespace Site {', '',
         'using namespace Botanik;', 'using namespace Botanik::Vars;', '',
         '// Inside the namespace and after the usings: the header names every',
         '// type unqualified, because it is only ever included here and by site.h.',
         '#include "vars.gen.h"', '']
    L += emit_cals(t['cal'])
    L += ['// ONE table: the library\'s rows merged into the site\'s, parameters',
          '// first and contiguous, each quality companion directly after the row',
          '// it speaks for, the program rows last.',
          'const Descriptor rows[] = {',
          '    // parameters, first and contiguous']
    for i, r in enumerate(table):
        if i == t['param_count']: L.append('    // ...and everything else')
        for ln in (r.get('lead') or '').split('\n'):
            if not ln: continue
            L.append('    ' + ln if ln.startswith('/*') else
                     '     ' + ln if ln.startswith('*') else ln)
        L.append(emit_row(r))
    L += ['};', '',
          '// Row numbers in NAME order, so Bank::find() binary-searches a const',
          '// array and nothing is built at boot. Case-folded, which is the order',
          '// the lookup compares in.',
          'const uint16_t row_order[] = {']
    for i in range(0, len(order), 12):
        L.append('    ' + ' '.join('%d,' % k for k in order[i:i + 12]))
    L += ['};', '']

    mdl = read_table(os.path.join(site, 'tables', 'modbus_models.csv'),
                     AUX['modbus_models'].split())
    dev = read_table(os.path.join(site, 'tables', 'modbus_devices.csv'),
                     AUX['modbus_devices'].split())
    for m in mdl:
        args = '"%s", %s' % (m['model_name'], m['regs']) + (', %s' % m['words'] if m['words'] else '')
        L += ['const Modbus::DeviceModel %s = Modbus::%s(%s);' % (m['symbol'], m['kind'], args)]
    L += ['', 'const Modbus::Device g_devices[] = {']
    L += ['    { %s, &%s, "%s" },' % (d['addr'], d['model'], d['tag']) for d in dev]
    L += ['};', '', 'const Modbus::DeviceTable g_device_table = {',
          '    g_devices, (uint8_t)(sizeof(g_devices) / sizeof(g_devices[0]))', '};', '',
          '// Home-screen readings: label + row. A caption is a person\'s choice',
          '// and nothing in a row\'s name says it belongs on the landing page, so',
          '// it is the one table no rule derives.',
          'const HomeVar g_home_fields[] = {']
    L += ['    { "%s", Row::%s },' % (h['label'], h['var']) for h in t['home']]
    L += ['};', '',
          '/** @brief The site\'s shell commands: none. A site table is matched',
          ' *  BEFORE the library built-ins, so a name reused there shadows one. */',
          'const Botanik::Shell::ShellCommand* const g_commands = nullptr;',
          'const uint8_t g_command_count = 0;', '',
          '// Field order matches Botanik::SiteDescriptor; any table may be null.',
          'const Botanik::SiteDescriptor g_site = {',
          '    Botanik::Config::SITE_NAME,',
          '    g_commands,    g_command_count,',
          '    g_home_fields, g_home_field_count,',
          '    &g_device_table,',
          '    rows,          row_order, ROWS_COUNT, PARAM_FIRST, PARAM_COUNT,',
          '};', '',
          '}  // namespace Site', '']
    open(os.path.join(site, 'generated', 'vars.gen.cpp'), 'w').write('\n'.join(L))

    # ---- vars.gen.h
    G = [H, '// Included from INSIDE namespace Site, so it opens none of its own.',
         '// The guard is named for the SITE: every site generates a file by this',
         '// name, and a shared symbol would silently skip the second one.',
         '//',
         '// The site\'s ENUMERATIONS are enums.gen.h, which the ST side needs',
         '// too and which therefore opens its own namespace.', '',
         '#ifndef %s_VARS_GEN_H' % guard, '#define %s_VARS_GEN_H' % guard, '']
    G += ['// The build this table was cut for. The row indices in rows.gen.h are',
          '// this profile\'s, so a build with one of these OFF would address the',
          '// wrong rows. @see BOTANIK_SITE_ASSERT_FEATURES',
          '#if !(BOTANIK_SITE_ASSERT_FEATURES)',
          '#  error "this site assumes every optional feature it declares is enabled"',
          '#endif', '']
    G += ['extern const AnalogCal %s;' % c['cal'] for c in t['cal']]
    G += ['',
          '// The whole table, library rows merged in, and the name order beside it.',
          'extern const Descriptor rows[];',
          'extern const uint16_t   row_order[];',
          'const uint16_t ROWS_COUNT  = %d;' % len(table),
          'const uint16_t PARAM_FIRST = %d;' % t['param_first'],
          'const uint16_t PARAM_COUNT = %d;' % t['param_count'], '',
          'static_assert(ROWS_COUNT == Config::VAR_MAX,',
          '              "the table and the bank\'s arrays are generated together");', '']
    G += ['extern const Modbus::DeviceModel %s;' % m['symbol'] for m in mdl]
    G += ['extern const Modbus::Device g_devices[];',
          'extern const Modbus::DeviceTable g_device_table;', '',
          '// The one table no rule derives: a caption is a person\'s choice, and',
          '// nothing in a row\'s name says it belongs on the landing page.',
          'extern const HomeVar g_home_fields[];',
          'const uint8_t g_home_field_count = %d;' % len(t['home']),
          '// The home screen silently ignores fields past its columns.',
          'static_assert(g_home_field_count <= Config::HOME_FIELDS_MAX,',
          '              "too many home fields: the HomePage would drop the tail");', '',
          '// The whole handoff to the library: this site, as data.',
          'extern const Botanik::SiteDescriptor g_site;', '',
          '/** @brief The firmware, bound to this site. A FUNCTION-LOCAL static,',
          ' *  so it is constructed on first use and not during static init: on',
          ' *  the OPTA that has to be after the Arduino core\'s init() has brought',
          ' *  up the UARTs, and a file-scope object would run before it.',
          ' *  @note There is no subclass. A site that needs an extra page or a',
          ' *  component of its own derives BotanikFirmware and overrides a hook;',
          ' *  one that is only data, like this one, just names the descriptor. */',
          'inline Botanik::BotanikFirmware& firmware() {',
          '    static Botanik::BotanikFirmware fw(g_site);',
          '    return fw;',
          '}', '']
    G += ['#endif  // %s_VARS_GEN_H' % guard, '']
    open(os.path.join(site, 'generated', 'vars.gen.h'), 'w').write('\n'.join(G))

    # ---- rows.gen.h
    w = max(len(r['name']) for r in table)
    R = [H, '/**', ' * @file rows.gen.h',
         ' * @brief One index per row, library and site, in ONE namespace.',
         ' *',
         ' * What everything but the console addresses a row through. A name no',
         ' * table declares is a COMPILE error here, rather than a reference that',
         ' * resolves to the sink for the life of the run.',
         ' */', '',
         '#ifndef %s_ROWS_GEN_H' % guard, '#define %s_ROWS_GEN_H' % guard, '',
         '#include <cstdint>', '',
         'namespace Botanik { namespace Vars { namespace Row {', '']
    for r in table:
        R.append('    const uint16_t %-*s = %d;' % (w, r['name'], r['_i']))
    R += ['', '}}}  // namespace Botanik::Vars::Row', '',
          '#endif  // %s_ROWS_GEN_H' % guard, '']
    open(os.path.join(site, 'generated', 'rows.gen.h'), 'w').write('\n'.join(R))

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
    # Named for the SITE: "BOTANIK_SITE_*" is the same symbol in every site's
    # generated header, so two of them in one translation unit would silently
    # skip the second.
    guard = site_guard(tom)

    defs = config_defaults()
    pool = 0
    for r in table:
        if r['_fac'] == 'PS': pool += as_int(r['cap'], defs)
    analog = sum(1 for r in table if r['_fac'] in ('AI', 'AO'))
    # Vars::mapped(): a register, and not one that addresses the FIELD bus.
    mapped = sum(1 for r in table
                 if (r.get('reg') or '0').strip() not in ('', '0')
                 and KIND[r['_fac']] not in ('INPUT', 'OUTPUT'))

    val = [('SITE_NAME', '"%s"' % tom['site']['name']),
           ('SITE_CONTROL_MS', tom['rates']['control']),
           ('SITE_SENSE_MS', tom['rates']['sense']),
           # COMPUTED, not stated: the table is generated, so its size is a fact
           # about it rather than a ceiling somebody has to keep ahead of.
           ('VAR_MAX', len(table)),
           ('STR_POOL_BYTES', pool),
           ('ANALOG_ROWS_MAX', analog),
           ('MODBUS_TCP_MAX_ROWS', mapped),
           ('MODBUS_BAUD', tom['fieldbus']['baud'])]
    if tom.get('build', {}).get('pin_sizes'): val.append(('PIN_SIZES', 1))

    L = ['// GENERATED by scripts/tables.py from site.toml. Do not edit.', '',
         '#ifndef %s_CONFIG_GEN_H' % guard, '#define %s_CONFIG_GEN_H' % guard, '']
    for k, v in val:
        L += ['#ifndef BOTANIK_%s' % k, '#  define BOTANIK_%s %s' % (k, v), '#endif']
    L.append('')
    L += ['// Every flag this site turns on, each under the prerequisites the LIBRARY',
          '// declares. The guard is "unless explicitly turned off", so precedence',
          '// stays command line > site > header default.']
    on = [f for f, v in tom['features'].items() if v]
    for f in on:
        need = [d for d in dep.get(f, []) if d not in ('VARS',)]
        ind = '  ' * len(need)
        for i, d in enumerate(need):
            L.append('%s#if !defined(BOTANIK_FEATURE_%s) || BOTANIK_FEATURE_%s' % ('  ' * i, d, d))
        L += ['%s#ifndef BOTANIK_FEATURE_%s' % (ind, f),
              '%s#  define BOTANIK_FEATURE_%s 1' % (ind, f),
              '%s#endif' % ind]
        L += ['%s#endif' % ('  ' * i) for i in range(len(need) - 1, -1, -1)]
    L += ['',
          '// This site turns every optional feature ON, and its table is CUT for',
          '// that: a guarded library row the build leaves out shifts every index',
          '// after it, and the indices are generated constants. So refuse rather',
          '// than drift. Here rather than in a header the site hand-writes,',
          '// because the flags have their defaults by the time this is read again.',
          '#define BOTANIK_SITE_ASSERT_FEATURES \\']
    L += ['    BOTANIK_FEATURE_%s && \\' % f for f in on]
    L += ['    1', '']
    L += ['', '#endif', '']
    open(os.path.join(site, 'generated', 'config.gen.h'), 'w').write('\n'.join(L))
    print('  config.gen.h   %d values, %d features' % (len(val), len(on)))



if __name__ == '__main__':
    cmd  = sys.argv[1] if len(sys.argv) > 1 else ''
    site = site_dir(sys.argv[2] if len(sys.argv) > 2 else None)
    if   cmd == 'generate': generate(site); quality_block()
    elif cmd == 'plant':    sys.exit(plant(site))
    elif cmd == 'config':   config(site)
    elif cmd == 'points':   points(site)
    else:                   sys.exit(__doc__)
