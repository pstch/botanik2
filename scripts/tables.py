#!/usr/bin/env python3
"""The site, from its tables.

    tables.py generate [site]   library.csv + tables/*.csv -> vars.gen.{h,cpp}, rows.gen.h
    tables.py config   [site]   site.toml + the tables     -> config.gen.h
    tables.py plant    [site]   programs.csv + ST          -> plant.st, plant.cpp, matiec
    tables.py points   [site]   the lot                    -> docs/reference/points.md

`site` is a name under src/sites/, or a path. BOTANIK_SITE_DIR overrides it.

Offline: the output is COMMITTED and never built. `make check-generated` is
what holds the two together - it regenerates into a scratch copy and diffs.

The generator owns the whole table. It merges the library's rows into the
site's, applies the site's overrides, orders the rows, assigns every index and
every generated register, resolves every name to an index, and CHECKS. Nothing
it checks is checked again at runtime.
"""

import csv, os, re, sys

DEFAULT_SITE = 'src/sites/gh_mlb41'


def site_dir(arg=None):
    """Which site to generate. The ARGUMENT first, so `check-generated` can
    regenerate the site the build selected rather than the default; then the
    environment, which is how that check redirects the whole run into a scratch
    copy - a check never writes into the tree it is checking."""
    env = os.environ.get('BOTANIK_SITE_DIR')
    if env: return env
    if not arg: return DEFAULT_SITE
    return arg if os.sep in arg else os.path.join('src', 'sites', arg)

VAR_NONE = 'VAR_NONE'

# factory -> (csv stem, the CSV's columns). The factory's own parameter list is
# FAC_COLS below: `qlt` is generated, not declared.
ROWS = {
    'P':   ('params',      'name type def lo hi lo_row hi_row digits unit reg desc notes writable retained'),
    'PE':  ('params_enum', 'name def label reg desc notes writable retained'),
    'PS':  ('params_text', 'name def cap kind reg desc notes writable retained'),
    'A':   ('alarms',      'name code on_s off_s recent_s hold_s reg desc notes'),
    'SV':  ('status',      'name type def digits unit reg desc notes retained'),
    'SVE': ('status_enum', 'name def label reg desc notes'),
    'MI':  ('modbus_in',   'name type device fc reg regs lo hi lo_row hi_row def timeout_ms digits unit desc notes poll_ms word_swap'),
    'MO':  ('modbus_out',  'name type device fc reg regs safe digits unit desc notes word_swap'),
    'AI':  ('analog_in',   'name cal device fc reg regs wire def timeout_ms digits unit desc notes poll_ms word_swap'),
    'AO':  ('analog_out',  'name cal device fc reg regs wire safe digits unit desc notes word_swap'),
    'DI':  ('digital_in',  'name device fc reg def timeout_ms invert desc notes poll_ms'),
    'DO':  ('digital_out', 'name device fc reg safe invert desc notes'),
    'RO':  ('relay_out',   'name device coil safe invert desc notes'),
    'PI':  ('pulse_in',    'name channel edge desc notes'),
}

# What each factory takes, in order. Equal to the CSV's columns except where the
# generator supplies one.
FAC_COLS = dict((f, c.split()) for f, (_s, c) in ROWS.items())
FAC_COLS['SV']  = ROWS['SV'][1].split()  + ['qlt']
FAC_COLS['SVE'] = ROWS['SVE'][1].split() + ['retained', 'qlt']
for _f in ('MI', 'AI', 'DI', 'PI'):
    FAC_COLS[_f] = ROWS[_f][1].split() + ['qlt']
FAC_COLS['Q']   = ['name', 'reg', 'desc']

# The six Kinds, and which factories build each.
KIND = {'P': 'PARAM', 'PE': 'PARAM', 'PS': 'PARAM',
        'A': 'ALARM',
        'SV': 'STATUS', 'SVE': 'STATUS',
        'MI': 'INPUT', 'AI': 'INPUT', 'DI': 'INPUT', 'PI': 'INPUT',
        'MO': 'OUTPUT', 'AO': 'OUTPUT', 'DO': 'OUTPUT', 'RO': 'OUTPUT',
        'Q': 'QUALITY'}

# Emission order within each half. Parameters first and contiguous; then
# everything else, each companion directly after its row.
PARAM_FACS = ['P', 'PE', 'PS']
OTHER_FACS = ['A', 'SV', 'SVE', 'MI', 'MO', 'AI', 'AO', 'DI', 'PI', 'DO', 'RO']

TEXT = {'name', 'unit', 'desc', 'notes', 'code', 'tag', 'model_name'}
# Columns whose type depends on the FACTORY: a text parameter's default is a
# string where every other factory's is a number.
TEXT_BY_FAC = {'PS': {'def'}}
# Not quoted, but not a number either: a symbol the emitted C++ names.
#
# `reg`, `coil` and `channel` carry a NUMBER on the field bus (a register, a
# coil) and a PIN LABEL locally, where there is no register to address. Both
# spellings are allowed in one column and told apart by shape: a number is a
# number, anything else is a HAL::Pin.
SYMBOL = {'edge': 'HAL::Pulse::', 'reg': 'HAL::Pin::',
          'coil': 'HAL::Pin::', 'channel': 'HAL::Pin::'}
# Columns naming a ROW, resolved to a generated index.
ROW_COLS = {'lo_row', 'hi_row'}
# A column read as a boolean, and what an empty cell means.
BOOLS = {'writable': 'true', 'retained': 'false', 'qlt': 'false',
         'word_swap': 'false', 'invert': 'false', 'safe': 'false'}
# ...plus `def`, which is a boolean on the two factories whose rows are bits.
BOOL_DEF = ('DI',)
# The tail a factory defaults, popped when the row agrees with it.
TAIL = {'P': {'writable': 'true', 'retained': 'false'},
        'PE': {'writable': 'true', 'retained': 'false'},
        'PS': {'writable': 'true', 'retained': 'false'},
        'SV': {'retained': 'false', 'qlt': 'false'},
        'SVE': {'retained': 'false', 'qlt': 'false'},
        # An INPUT always gets a companion, so `qlt` is the default there and
        # the emitted row says nothing. A hand-written table (a unit case's) is
        # where it has to be said, since nothing generates the companion.
        'MI': {'poll_ms': '0', 'word_swap': 'false', 'qlt': 'true'},
        'AI': {'poll_ms': '0', 'word_swap': 'false', 'qlt': 'true'},
        'MO': {'word_swap': 'false'},
        'AO': {'word_swap': 'false'},
        'DI': {'poll_ms': '0', 'qlt': 'true'},
        'PI': {'qlt': 'true'}}

AUX = {  # stem -> columns, for the tables that are not rows
    'analog_cal':     'cal raw_min_row raw_min raw_max_row raw_max eng_min_row eng_min '
                      'eng_max_row eng_max min_row min max_row max',
    'modbus_models':  'symbol kind model_name regs words',
    'modbus_devices': 'addr model tag',
    'enums':          'enum ctype symbol value label desc',
    'programs':       'name instance source rate desc quality park',
    'home':           'label var',
}

LIB_CSV = 'src/libbotanik/vars/tables/library.csv'
CONFIG_DIR = 'src/libbotanik/config'
# The always-on core, from libbotanik_features.h's defaults.
CORE_FEATURES = {'CONSOLE', 'LOGGING', 'VARS', 'PLC', 'HOME_PAGE', 'SHELL_PAGE'}

H = '// GENERATED by scripts/tables.py from tables/*.csv. Do not edit.\n'


# ---- reading ---------------------------------------------------------------

def _numeric(v):
    try: int(v, 0); return True
    except ValueError: return False


def config_defaults():
    """Every `BOTANIK_<NAME> <int>` default the config headers declare.

    A library text row states its capacity as one of these, so the pool the
    generator sizes is a number the library owns rather than one restated
    here. Read, never copied."""
    out = {}
    for f in sorted(os.listdir(CONFIG_DIR)):
        if not f.endswith('.h'): continue
        for m in re.finditer(r'^#\s*define\s+(BOTANIK_\w+)\s+(\d+)\s*$',
                             open(os.path.join(CONFIG_DIR, f)).read(), re.M):
            out.setdefault(m.group(1), int(m.group(2)))
    return out


def as_int(v, defs):
    """A CSV cell that is a number, or a config macro standing for one."""
    try: return int(v, 0)
    except ValueError: pass
    if v in defs: return defs[v]
    sys.exit('%r is neither a number nor a config default' % v)


def read_table(path, cols):
    if not os.path.exists(path): return []
    with open(path, newline='') as fh:
        rd = csv.reader(fh)
        head = [h.strip() for h in next(rd)]
        if head != cols: sys.exit('%s: header %s != %s' % (path, head, cols))
        return [dict(zip(head, [c.strip() for c in r])) for r in rd
                if r and any(c.strip() for c in r)]


def toml(path):
    """Enough TOML for a config file: tables, and scalar key = value."""
    out, sec = {}, None
    for ln in open(path):
        ln = ln.split('#')[0].strip()
        if not ln: continue
        if ln[0] == '[':
            sec = out.setdefault(ln.strip('[]'), {})
        else:
            k, v = (x.strip() for x in ln.split('=', 1))
            if   v in ('true', 'false'):     v = v == 'true'
            elif v[0] == v[-1] == '"':       v = v[1:-1]
            else:                            v = int(v)
            sec[k] = v
    return out


def site_guard(t):
    """A site's include-guard prefix. One formula: two copies could diverge on
    the day the rule has to change (a name starting with a digit, say)."""
    return re.sub(r'\W', '_', t['site']['name']).upper()


def features_on(tom):
    return CORE_FEATURES | {k for k, v in tom.get('features', {}).items() if v}


def lib_rows(tom):
    """The library's own rows, filtered to what this site's flags compile.

    ORDER IS THE FILE'S: the rows interleave kinds, and the parameter run has to
    stay contiguous, so one table with a `fac` column rather than one per kind.
    A guard is AND-ed flags; a flag the site does not name takes the header's
    default, which is off for every optional one."""
    on = features_on(tom)
    with open(LIB_CSV, newline='') as fh:
        recs = list(csv.DictReader(fh))
    out = []
    for r in recs:
        want = [g for g in (r.get('guard') or '').split() if g]
        if not all(g in on for g in want): continue
        row = dict((k, (v or '').strip()) for k, v in r.items())
        row['_fac'] = row.pop('fac')
        row['_lib'] = True
        out.append(row)
    return out


def st_files(site):
    out = []
    for base in (os.path.join(site, 'blocks'), os.path.join(site, 'programs')):
        for r, _, fs in os.walk(base):
            out += [os.path.join(r, f) for f in fs if f.endswith('.st')]
    return sorted(out)


def var_externals(path):
    """Every row one ST file names, as (name, type, read_only). `VAR_EXTERNAL
    CONSTANT` is read-only and plain `VAR_EXTERNAL` is written - that IS the
    site's in/out declaration, there is no table saying so."""
    out = []
    # Comments FIRST: a law that opens with a block explaining itself would
    # otherwise swallow the CONSTANT block behind it. Anchored to a line start
    # for the same reason.
    src = re.sub(r'\(\*.*?\*\)', ' ', open(path).read(), flags=re.S)
    for m in re.finditer(r'^VAR_EXTERNAL(\s+CONSTANT)?$(.*?)^END_VAR',
                         src, re.S | re.M):
        for line in m.group(2).split('\n'):
            mm = re.match(r'\s*([A-Za-z_]\w*)\s*:\s*([A-Z]+)\s*;', line)
            if mm: out.append((mm.group(1), mm.group(2), bool(m.group(1))))
    return out


def _opted_in(e, col):
    return e[col].strip().lower() in ('1', 'yes', 'true')


def programs(site):
    return read_table(os.path.join(site, 'tables', 'programs.csv'),
                      AUX['programs'].split())


# ---- the table -------------------------------------------------------------

def quality_names(site, prog):
    """Which rows get a `_qlt` companion.

    One per INPUT row - a reading can always stop being one - plus one per row
    an opted-in program writes, because a law that can fail to compute has to be
    able to say so. Nothing else: a row with no companion is good, always.

    ALARMS are excluded even under an opt-in. An alarm is a verdict rather than
    a reading: AlarmController takes the boolean and has nothing to substitute,
    so a quality beside it would be a row nothing consults.

    The opted-in set comes from the ST's own `VAR_EXTERNAL` sections rather than
    from matiec's output, so this does not depend on a POUS.c the same run is
    about to replace."""
    out = set()
    for fac in ('MI', 'AI', 'DI', 'PI'):
        for r in read_table(os.path.join(site, 'tables', ROWS[fac][0] + '.csv'),
                            ROWS[fac][1].split()):
            out.add(r['name'])
    alarms = {r['name'] for r in
              read_table(os.path.join(site, 'tables', 'alarms.csv'),
                         ROWS['A'][1].split())}
    for e in prog:
        if not _opted_in(e, 'quality'): continue
        for n, _t, ro in var_externals(os.path.join(site, e['source'])):
            if ro or n.endswith('_qlt') or n in alarms: continue
            out.add(n)
    return out


def _merge(site, tom):
    """The library's rows and the site's, with the site's overrides applied.

    A site OVERRIDES a library row by redeclaring the name: the site's row
    replaces the library's IN PLACE, keeping the library's register, so the
    whole row is the site's and the map does not move."""
    lib = lib_rows(tom)
    own = []
    for fac in PARAM_FACS + OTHER_FACS:
        stem, cols = ROWS[fac]
        for r in read_table(os.path.join(site, 'tables', stem + '.csv'),
                            cols.split()):
            r['_fac'] = fac
            r['_lib'] = False
            own.append(r)

    at = dict((r['name'], i) for i, r in enumerate(lib))
    rest = []
    for r in own:
        i = at.get(r['name'])
        if i is None: rest.append(r); continue
        r['reg'] = lib[i]['reg']
        lib[i] = r
    return lib, rest


def _gen_row(fac, **kw):
    r = dict(kw)
    r['_fac'] = fac
    r['_lib'] = False
    r['_gen'] = True    # its register comes out of a generated block
    return r


def build(site):
    """The whole table, ordered and indexed, plus what is emitted beside it."""
    tom  = toml(os.path.join(site, 'site.toml'))
    prog = programs(site)
    lib, own = _merge(site, tom)
    wants_qlt = quality_names(site, prog)

    def is_param(r): return KIND[r['_fac']] == 'PARAM'

    table = [r for r in lib if is_param(r)] + [r for r in own if is_param(r)]
    n_param = len(table)

    qreg = int(tom['registers']['quality_base'])
    for r in [x for x in lib if not is_param(x)] + [x for x in own if not is_param(x)]:
        table.append(r)
        if KIND[r['_fac']] == 'INPUT' or r['name'] in wants_qlt:
            r['qlt'] = 'true'
            table.append(_gen_row('Q', name=r['name'] + '_qlt', reg=str(qreg),
                                  desc='quality of ' + r['name']))
            qreg += 1

    # The program rows, last and in roster order: a health row per program - the
    # one status value no law writes, since it is Plant's own verdict - then the
    # parking pair per program that opted in.
    preg = int(tom['registers']['prog_base'])
    for e in prog:
        table.append(_gen_row('SVE', name='prog_%s' % e['name'], **{
            'def': '3', 'label': '&Plc::health_label', 'reg': str(preg),
            'desc': 'health of the %s program' % e['name'],
            'notes': '3 is INHIBITED: nothing has run yet'}))
        preg += 1
    kreg = int(tom['registers']['park_base'])
    for e in prog:
        if not _opted_in(e, 'park'): continue
        for suffix, what in (('park', 'is parking to its safe state'),
                             ('parked', 'has reached its parked state')):
            table.append(_gen_row('SV', name='prog_%s_%s' % (e['name'], suffix),
                                  type='T_BOOL', **{
                'def': '0', 'digits': '0', 'unit': '', 'reg': str(kreg),
                'desc': '%s %s' % (e['name'], what), 'notes': ''}))
            kreg += 1

    for i, r in enumerate(table): r['_i'] = i
    return {'site': site, 'toml': tom, 'prog': prog, 'table': table,
            'index': dict((r['name'], r['_i']) for r in table),
            'param_first': 0, 'param_count': n_param,
            'cal': read_table(os.path.join(site, 'tables', 'analog_cal.csv'),
                              AUX['analog_cal'].split()),
            'home': read_table(os.path.join(site, 'tables', 'home.csv'),
                               AUX['home'].split())}


# ---- checks ----------------------------------------------------------------

def _num(v):
    try: return float(v)
    except ValueError: return None


def check(t):
    """Every rule the table has to satisfy. All fatal: a table that breaks one
    produces firmware whose failure is silent."""
    bad, table, index = [], t['table'], t['index']

    seen = {}
    for r in table:
        k = r['name'].lower()
        if k in seen: bad.append('duplicate row name %s' % r['name'])
        seen[k] = r

    # The TCP-slave register: unique, and present on every kind an external
    # master addresses. An INPUT or an OUTPUT has none - its `reg` addresses a
    # device on the FIELD bus - and a status value with register 0 is simply not
    # published, which is what the old `G` scope was.
    regs = {}
    gen_base = min(int(t['toml']['registers'][k])
                   for k in ('quality_base', 'park_base', 'prog_base'))
    for r in table:
        kind = KIND[r['_fac']]
        if kind in ('INPUT', 'OUTPUT'): continue
        reg = r.get('reg', '')
        if not _numeric(reg):
            bad.append('%s: register %r is not a number' % (r['name'], reg))
            continue
        n = int(reg, 0)
        if n == 0:
            if kind != 'STATUS':
                bad.append('%s: a %s row needs a register' % (r['name'], kind))
            continue
        if n in regs:
            bad.append('%s and %s both claim register %d'
                       % (regs[n], r['name'], n))
        regs[n] = r['name']
        # A hand-assigned register must stay out of the generated blocks, or a
        # new reading would take a register something already answers on.
        if n >= gen_base and not r.get('_gen'):
            bad.append('%s: register %d is inside the generated block (>= %d)'
                       % (r['name'], n, gen_base))

    # Local terminals: unique per direction. Device 0 means one of the
    # controller's own, and configuring one pin two ways is a hardware fault
    # waiting for the first wet week.
    for way in ('INPUT', 'OUTPUT'):
        used = {}
        for r in table:
            if KIND[r['_fac']] != way: continue
            if r.get('device', '1') != '0': continue
            pin = r.get('reg') or r.get('coil') or r.get('channel') or ''
            if pin in used:
                bad.append('%s and %s share local terminal %s'
                           % (used[pin], r['name'], pin))
            used[pin] = r['name']

    # A LOCAL row must be a kind the controller's own terminals can actually be.
    # The OPTA has eight inputs, four relays, four LEDs and a button - and NO
    # analog output. Without this an AO row on device 0 declares, generates and
    # passes every other check, and is then driven by nothing.
    for r in table:
        if r['_fac'] in ('AO', 'MI', 'MO') and r.get('device') == '0':
            bad.append('%s: device 0 means LOCAL, and there is no local %s'
                       % (r['name'], r['_fac']))

    # Every row a band end names must exist, share the referring row's type, and
    # leave that row's own default inside the band - which is what keeps a pair
    # reachable: an operator can always move one end and then the other, but only
    # if the pair was valid to begin with.
    by_name = dict((r['name'], r) for r in table)
    for r in table:
        ends = []
        for side in ('lo', 'hi'):
            nm = r.get(side + '_row', '')
            if not nm:
                ends.append(_num(r.get(side, '')))
                continue
            q = by_name.get(nm)
            if q is None:
                bad.append('%s: %s names no row %s' % (r['name'], side, nm))
                ends.append(None); continue
            if q.get('type') != r.get('type'):
                bad.append('%s: %s is %s, %s is %s'
                           % (r['name'], side, r.get('type'), nm, q.get('type')))
            ends.append(_num(q.get('def', '')))
        lo, hi = ends
        d = _num(r.get('def', ''))
        if lo is None or hi is None or d is None or lo == hi: continue
        if not (lo <= d <= hi):
            bad.append('%s defaults to %s, outside [%g, %g]'
                       % (r['name'], r['def'], lo, hi))

    # A calibration's tunables, the same way.
    for c in t['cal']:
        for k in ('raw_min', 'raw_max', 'eng_min', 'eng_max', 'min', 'max'):
            nm = c[k + '_row']
            if nm and nm not in index:
                bad.append('%s: %s names no row %s' % (c['cal'], k, nm))

    for h in t['home']:
        if h['var'] not in index:
            bad.append('home field %r names no row %s' % (h['label'], h['var']))

    # An alarm's code is a column on the ALARMS page, and a longer one is
    # truncated where an operator reads it.
    for r in table:
        if r['_fac'] == 'A' and len(r['code']) > 9:
            bad.append('%s: alarm code %r is longer than 9' % (r['name'], r['code']))

    if bad:
        for b in bad: print('  ' + b, file=sys.stderr)
        sys.exit('%d table error(s)' % len(bad))


# ---- emitting --------------------------------------------------------------

def quote(v, col, fac=None):
    if col in TEXT or col in TEXT_BY_FAC.get(fac, ()):
        return '"%s"' % v.replace('"', '\\"')
    if col in SYMBOL and v and not _numeric(v) and not v.startswith(SYMBOL[col]):
        return SYMBOL[col] + v
    return v


def _cell(r, col, fac, index):
    v = r.get(col, '')
    if col in ROW_COLS:
        return ('Row::' + v) if v else VAR_NONE
    if col in BOOLS or (col == 'def' and fac in BOOL_DEF):
        if not v: return BOOLS.get(col, 'false')
        return 'true' if v.lower() in ('1', 'yes', 'true') else 'false'
    if col == 'poll_ms' and not v: return '0'
    return quote(v, col, fac)


def emit_row(r, index):
    fac = r['_fac']
    a = [_cell(r, c, fac, index) for c in FAC_COLS[fac]]
    tail = TAIL.get(fac, {})
    cols = FAC_COLS[fac]
    while a and (a[-1] == '' or a[-1] == tail.get(cols[len(a) - 1])):
        a.pop()
    return '    %s(%s),' % (fac, ', '.join(a))


def emit_cals(cal, index):
    L = []
    for c in cal:
        keys = ('raw_min', 'raw_max', 'eng_min', 'eng_max', 'min', 'max')
        L.append('const AnalogCal %s = {' % c['cal'])
        L.append('    ' + ' '.join('SF(%s),' % c[k].rstrip('f') for k in keys))
        L.append('    { ' + ', '.join(
            ('Row::' + c[k + '_row']) if c[k + '_row'] else VAR_NONE
            for k in keys) + ' },')
        L += ['};', '']
    return L


def generate(site):
    t = build(site)
    check(t)
    table, index, tom = t['table'], t['index'], t['toml']
    guard = site_guard(tom)

    order = sorted(range(len(table)), key=lambda i: table[i]['name'].lower())

    L = [H, '', '#include "libbotanik.h"', '', 'namespace Site {', '',
         'using namespace Botanik;', 'using namespace Botanik::Vars;', '',
         '// Inside the namespace and after the usings: the header names every',
         '// type unqualified, because it is only ever included here and by site.h.',
         '#include "vars.gen.h"', '']
    L += emit_cals(t['cal'], index)
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
        L.append(emit_row(r, index))
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
    en = read_table(os.path.join(site, 'tables', 'enums.csv'), AUX['enums'].split())
    G = [H, '// Included from INSIDE namespace Site, so it opens none of its own.',
         '// The guard is named for the SITE: every site generates a file by this',
         '// name, and a shared symbol would silently skip the second one.', '',
         '#ifndef %s_VARS_GEN_H' % guard, '#define %s_VARS_GEN_H' % guard, '']
    for name in dict.fromkeys(e['enum'] for e in en):
        mem = [e for e in en if e['enum'] == name]
        w = max(len(e['symbol']) for e in mem)
        G.append('enum %s : %s {' % (name, mem[0]['ctype']))
        G += ['    %-*s = %s,%s' % (w, e['symbol'], e['value'],
                                    '   ///< ' + e['desc'] if e['desc'] else '') for e in mem]
        G += ['};', '',
              '/** @brief A %s as the word an operator reads and types.' % name,
              ' *  Null for an unmapped code, which is what refuses it. */',
              'inline const char* %s_label(uint64_t code) {' % name,
              '    switch (code) {']
        G += ['        case %s: return "%s";' % (e['value'], e['label']) for e in mem]
        G += ['        default: return nullptr;', '    }', '}', '']
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

    val = [('SITE_NAME', '"%s"' % tom['site']['name']),
           ('SITE_CONTROL_MS', tom['rates']['control']),
           ('SITE_SENSE_MS', tom['rates']['sense']),
           # COMPUTED, not stated: the table is generated, so its size is a fact
           # about it rather than a ceiling somebody has to keep ahead of.
           ('VAR_MAX', len(table)),
           ('STR_POOL_BYTES', pool),
           ('ANALOG_ROWS_MAX', analog),
           ('MODBUS_TCP_MAX_ROWS', tom['limits']['modbus_tcp_max_rows']),
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


# ---- plant -----------------------------------------------------------------

# What `make -C build matiec` builds, then whatever is on PATH. CMake's own
# targets pass IEC2C explicitly, so this default only serves a hand run.
_PINNED = os.path.join('build', 'matiec', 'iec2c')
IEC2C = os.environ.get('IEC2C') or (_PINNED if os.access(_PINNED, os.X_OK)
                                    else 'iec2c')
# What `iec2c -I` is pointed at: the directory holding OUR ieclib.txt.
IECLIB_DIR = 'src/libbotanik/plc/functions'
# ORDER MATTERS: matiec needs a block declared before it is instanced, and there
# is no forward declaration. filters before analog, because DC_RAIL holds an EWMA.
LIBST = ['src/libbotanik/plc/functions/quality.st',
         'src/libbotanik/plc/functions/math.st',
         'src/libbotanik/plc/functions/psychro.st',
         'src/libbotanik/plc/functions/time.st',
         'src/libbotanik/plc/blocks/timers.st',
         'src/libbotanik/plc/blocks/filters.st',
         'src/libbotanik/plc/blocks/analog.st',
         'src/libbotanik/plc/blocks/pid.st',
         'src/libbotanik/plc/blocks/actuator.st',
         'src/libbotanik/plc/blocks/meter.st']


def writes_by_pou(site):
    """What each PROGRAM writes, off matiec's own generated C.

    A write is `__SET_EXTERNAL` and a read is `__GET_EXTERNAL` - our macros,
    emitted from matiec's parse. So this is the COMPILER's answer, not a second
    reading of the ST: no comments to skip, and a call's named arguments cannot
    look like an assignment. Names come back matiec-folded (upper)."""
    src = open(os.path.join(site, 'generated', 'POUS.c')).read()
    out = {}
    for m in re.finditer(r'^static void (\w+)_body__\(.*?^\}', src, re.M | re.S):
        w = re.findall(r'__SET_EXTERNAL\(\w+->,\s*(\w+)\s*,', m.group(0))
        if w: out[m.group(1)] = w
    return out


def pou_of(path):
    m = re.search(r'^PROGRAM (\w+)', open(path).read(), re.M)
    return m.group(1).upper() if m else sys.exit('%s: no PROGRAM' % path)


def emit_plant_cpp(site, t, prog, writes):
    w = max(len(e['pou']) for e in prog)
    wi = max(len(e['instance'].upper()) for e in prog)
    wn = max(len(e['name']) for e in prog)
    index = t['index']
    L = ['/**', ' * @file plant.cpp',
         ' * @brief GENERATED by scripts/tables.py from tables/programs.csv.',
         ' *',
         ' * The only translation unit that compiles matiec\'s resource, because the',
         ' * generated bodies are static in it. That is why the trampolines are here:',
         ' * a table of function pointers needs one wrapper per POU.',
         ' */', '',
         '#include "libbotanik/plc/runtime/program.h"',
         '#include "libbotanik/plc/runtime/runtime.h"',
         '#include "libbotanik/vars/rows.h"', '',
         '/* Every row an ST body names, as matiec spells it. `accessor.h` binds',
         ' * each external to Ref<type>{ ROW_<NAME> }, so a name no table declares',
         ' * fails to compile rather than resolving to the sink at bring-up. */',
         'enum {']
    ext = sorted({n for e in prog for n, _t, _ro in var_externals(e['path'])})
    we = max(len(n) for n in ext)
    for n in ext:
        if n.lower() not in {k.lower() for k in index}:
            sys.exit('%s: no table declares it' % n)
        row = [k for k in index if k.lower() == n.lower()][0]
        L.append('    ROW_%-*s = ::Botanik::Vars::Row::%s,' % (we, n.upper(), row))
    L += ['};', '',
          '#pragma GCC diagnostic push',
          '#pragma GCC diagnostic ignored "-Wunused-parameter"',
          '#pragma GCC diagnostic ignored "-Wunused-function"',
          '#pragma GCC diagnostic ignored "-Wunused-but-set-variable"',
          '#pragma GCC diagnostic ignored "-Wshadow"',
          '#include "plc.c"',
          '#pragma GCC diagnostic pop', '',
          'namespace Botanik { namespace Plc { namespace Runtime {', '',
          '#define PLANT_PROGRAM_LIST(X) \\']
    for i, e in enumerate(prog):
        L.append('    X(%-*s %-*s %-*s BOTANIK_SITE_%s_MS)%s' % (
            wn + 1, e['name'] + ',', w + 1, e['pou'] + ',', wi + 1,
            e['instance'].upper() + ',', e['rate'].upper(),
            ' \\' if i + 1 < len(prog) else ''))
    L += ['', '/* The roster index, so a handler knows which entry it is. */', 'enum {',
          '#define X(name, POU, INST, PERIOD) IDX_##INST,',
          '    PLANT_PROGRAM_LIST(X)', '#undef X', '};', '',
          '#define X(name, POU, INST, PERIOD)                                             \\',
          '    static void _init_##INST() { POU##_init__(&INST, 0); }                     \\',
          '    static void _run_##INST()  { POU##_body__(&INST); }                        \\',
          '    static void _task_##INST(Component* self, const Tasks::Tick& t) {          \\',
          '        static_cast<Plant*>(self)->scan(IDX_##INST, t.now_ms);                 \\',
          '    }',
          'PLANT_PROGRAM_LIST(X)', '#undef X', '',
          '/* What each program DRIVES: its plain VAR_EXTERNAL rows, the ones it',
          ' * is the author of. `VAR_EXTERNAL CONSTANT` is a read and is not here.',
          ' * Taking a program out of the scan drops exactly these to their',
          ' * declared safe state, so the list has to be the ST\'s own. */']
    for e in prog:
        wr = set(writes.get(e['pou'], ()))
        wr = [n for n, _t, ro in var_externals(e['path']) if not ro and n.upper() in wr]
        L.append('static const uint16_t _writes_%s[] = {%s};'
                 % (e['instance'].upper(),
                    ''.join(' Vars::Row::%s,' % _home(index, n) for n in wr) or ' 0'))
    L.append('')
    L.append('// The parking pair, or VAR_NONE for a program that is simply dropped.')
    for e in prog:
        pk = ('Vars::Row::prog_%s_park' % e['name'],
              'Vars::Row::prog_%s_parked' % e['name']) \
             if _opted_in(e, 'park') else ('Vars::VAR_NONE', 'Vars::VAR_NONE')
        L.append('static const uint16_t _park_%s   = %s;' % (e['instance'].upper(), pk[0]))
        L.append('static const uint16_t _parked_%s = %s;' % (e['instance'].upper(), pk[1]))
    L += ['',
          '// The health row per program: Plant\'s own verdict, generated beside',
          '// the roster so a program cannot exist without one.']
    for e in prog:
        L.append('static const uint16_t _health_%s = Vars::Row::prog_%s;'
                 % (e['instance'].upper(), e['name']))
    L += ['',
          'const Program programs[] = {',
          '#define X(name, POU, INST, PERIOD)                                             \\',
          '    { #name, PERIOD, &_init_##INST, &_run_##INST, &_task_##INST,               \\',
          '      _writes_##INST,                                                          \\',
          '      (uint8_t)(sizeof _writes_##INST / sizeof _writes_##INST[0]),             \\',
          '      _park_##INST, _parked_##INST, _health_##INST },',
          '    PLANT_PROGRAM_LIST(X)', '#undef X', '};', '',
          'const uint8_t PROGRAM_COUNT =',
          '    (uint8_t)(sizeof(programs) / sizeof(programs[0]));', '',
          '}}}  // namespace Botanik::Plc::Runtime', '']
    open(os.path.join(site, 'generated', 'plant.cpp'), 'w').write('\n'.join(L))


def _home(index, n):
    """The table's own spelling of a name an ST file wrote in any case."""
    if n in index: return n
    for k in index:
        if k.lower() == n.lower(): return k
    sys.exit('%s: no table declares it' % n)


def check_laws(site, t, prog, writes):
    """What only the compiler's own answer can tell: which rows each body
    actually writes, against what the tables and the declarations claim."""
    bad = []
    table = t['table']
    kind_of = dict((r['name'], KIND[r['_fac']]) for r in table)
    alarms = {n for n, k in kind_of.items() if k == 'ALARM'}

    # A row DECLARED writable and never written is a mis-declaration: it belongs
    # in VAR_EXTERNAL CONSTANT, and leaving it here puts a row the program only
    # reads on its safe-state roster. Two programs that both WRITE one row is
    # different - read-modify-written shared state is the design - so it is
    # reported, not refused.
    author, shared = {}, []
    for e in prog:
        wr = set(writes.get(e['pou'], ()))
        for n, _ty, ro in var_externals(e['path']):
            if ro: continue
            if n.upper() not in wr:
                bad.append('%s: declares %s writable and never writes it'
                           % (e['name'], n))
                continue
            if n in author: shared.append(n)
            else: author[n] = e['name']
    if shared:
        print('  %d row(s) with two authors (read-modify-write): %s'
              % (len(shared), ', '.join(shared)))

    # ...and the other direction: a row nothing writes. An OUTPUT, an ALARM or a
    # STATUS with no author is a law somebody meant to write and did not, and it
    # reads its declared value for the life of the plant with nothing to say so.
    every = set()
    for pou in writes.values(): every |= {x.upper() for x in pou}
    for r in table:
        k = KIND[r['_fac']]
        if k not in ('OUTPUT', 'ALARM', 'STATUS'): continue
        # The LIBRARY's own rows are written by the library, in C++: its alarms
        # come from HealthMonitor and its status values from the components that
        # produce them. Only the SITE's rows are a law's to write.
        if r.get('_lib'): continue
        # `prog_*` is Plant's own verdict, and the parking pair is half Plant's.
        if r['name'].startswith('prog_'): continue
        if r['name'].upper() not in every:
            bad.append('%s %s: no program writes it' % (k.lower(), r['name']))

    # A program that OPTS IN owes a quality for every row it writes: the
    # companion EXISTS for each of them, and one nothing writes sits at Q_NONE
    # for the life of the run with nothing to say why.
    for e in prog:
        if not _opted_in(e, 'quality'): continue
        wr = {n.upper() for n in writes.get(e['pou'], ())}
        for n in sorted(wr):
            if n.endswith('_QLT') or n.lower() in {a.lower() for a in alarms}: continue
            if n + '_QLT' not in wr:
                bad.append('%s: writes %s and owes %s_qlt'
                           % (e['name'], n.lower(), n.lower()))

    # ...and the same for PARKING: a program told to park that never answers
    # holds the whole shutdown open until plc_parkTimeout_sec runs out.
    for e in prog:
        if not _opted_in(e, 'park'): continue
        if ('PROG_%s_PARKED' % e['name']).upper() not in \
                {n.upper() for n in writes.get(e['pou'], ())}:
            bad.append('%s: parks and owes prog_%s_parked' % (e['name'], e['name']))

    if bad:
        for b in bad: print('  ' + b, file=sys.stderr)
        sys.exit('%d law error(s)' % len(bad))


def plant(site):
    """plant.st and plant.cpp from programs.csv, then matiec."""
    import subprocess, tempfile
    t = build(site)
    prog = t['prog']
    for e in prog:
        e['path'] = os.path.join(site, e['source'])
        e['pou'] = pou_of(e['path'])
    rows, whence = {}, {}
    for p in st_files(site):
        for n, ty, _ in var_externals(p):
            if rows.get(n, ty) != ty: sys.exit('type clash on %s' % n)
            rows[n] = ty
            whence.setdefault(n, os.path.basename(p))
    # A name no table declares is a program that cannot be wired. Checked HERE
    # because nothing downstream can: these go to matiec as VAR_GLOBAL, so it
    # declares whatever it is given.
    folded = {k.lower() for k in t['index']}
    absent = sorted(n for n in rows if n.lower() not in folded)
    if absent:
        for n in absent:
            print('  %s: no table declares it (%s)' % (n, whence[n]), file=sys.stderr)
        sys.exit('%d ST name(s) declare no row' % len(absent))

    w = max(len(n) for n in rows)
    en = read_table(os.path.join(site, 'tables', 'enums.csv'), AUX['enums'].split())
    wi = max(len(e['instance']) for e in prog)
    ref = ['(* Enumerated codes, for the laws that compare against one. ST has no',
           '   readable spelling for them: matiec rejects an enumerated TYPE with',
           '   explicit values, and a zero-argument FUNCTION does not parse.']
    for name in dict.fromkeys(e['enum'] for e in en):
        ref.append('     %s: %s' % (name, '  '.join(
            '%s=%s' % (e['symbol'], e['value']) for e in en if e['enum'] == name)))
    ref.append('   The codes are a Modbus contract, so they do not move. *)')
    L = [H.replace('//', '(*').rstrip() + ' *)', '',
         '\n'.join(ref), '',
         '(* NO PROGRAM IS BOUND TO A TASK. plant.cpp calls each body itself, at',
         '   the period programs.csv states, so a program keeps its own registry',
         '   entry: its health row, its cost and missed counts, and its writer',
         '   roster.',
         '',
         '   The TASK below is a formality - matiec refuses a RESOURCE without a',
         '   periodic one. Nothing binds to it and nothing reads its interval. *)',
         '', 'CONFIGURATION gh_mlb41', '', '  VAR_GLOBAL']
    L += ['    %-*s : %s;' % (w, n, ty) for n, ty in sorted(rows.items())]
    L += ['  END_VAR', '', '  RESOURCE plc ON OPTA',
          '    TASK unbound (INTERVAL := T#100ms, PRIORITY := 1);', '']
    L += ['    PROGRAM %-*s : %s;' % (wi, e['instance'], e['pou'].lower()) for e in prog]
    L += ['  END_RESOURCE', '', 'END_CONFIGURATION', '']
    pl = os.path.join(site, 'generated', 'plant.st')
    open(pl, 'w').write('\n'.join(L))

    # matiec FIRST: plant.cpp's write rosters are read out of the POUS.c it
    # emits, so the compiler's own answer is what they are built from.
    with tempfile.NamedTemporaryFile('w', suffix='.st', delete=False) as fh:
        for f in LIBST + st_files(site) + [pl]: fh.write(open(f).read() + '\n')
        tmp = fh.name
    # -I OURS: matiec loads <dir>/ieclib.txt unconditionally, and its own
    # declares 28 function blocks under names we use for our own - and is GPL
    # besides.
    try:
        r = subprocess.run([IEC2C, '-I', IECLIB_DIR,
                            '-T', os.path.join(site, 'generated'), tmp],
                           capture_output=True, text=True)
    except FileNotFoundError:
        os.unlink(tmp)
        sys.exit("iec2c not found at %r.\n"
                 "  Build the pinned one:  make -C build matiec\n"
                 "  ...or point at another: IEC2C=/path/to/iec2c %s"
                 % (IEC2C, ' '.join(sys.argv)))
    os.unlink(tmp)
    err = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l.lower()]
    for l in err[:6]: print('  ' + l)
    if err: return 1

    writes = writes_by_pou(site)
    check_laws(site, t, prog, writes)
    emit_plant_cpp(site, t, prog, writes)
    print('  plant.st       %d globals, %d POUs' % (len(rows), len(st_files(site))))
    return 0


# ---- the ST quality enumeration --------------------------------------------

QUALITY_GEN = 'src/libbotanik/plc/runtime/quality.gen.h'


def quality_block():
    """The accessor's QUALITY enumeration block, off descriptor.h.

    matiec initialises a QUALITY-returning function with `QUALITY__Q_NONE`, so
    the members have to be named constants of the REAL C++ type. Generated so a
    code added to one side and not the other cannot compile."""
    src = open('src/libbotanik/vars/descriptor.h').read()
    m = re.search(r'enum Quality : uint8_t \{(.*?)\n    \};', src, re.S)
    if not m: sys.exit('descriptor.h: no `enum Quality`')
    codes = re.findall(r'^\s*(Q_[A-Z_]+)', m.group(1), re.M)
    w = max(len(c) for c in codes)
    L = ['/* GENERATED by scripts/tables.py from vars/descriptor.h. Do not edit. */',
         '#ifndef BOTANIK_PLC_RUNTIME_QUALITY_GEN_H',
         '#define BOTANIK_PLC_RUNTIME_QUALITY_GEN_H', '',
         '/* matiec discards its own member list for this type: the ST enumeration',
         ' * and the C++ one must be the SAME type, not two with equal values. */',
         '#define BOTANIK_ST_TYPE_QUALITY \\']
    for i, c in enumerate(codes):
        L.append('    static const QUALITY QUALITY__%-*s = ::Botanik::Vars::%-*s;%s'
                 % (w, c, w, c, ' \\' if i + 1 < len(codes) else ''))
    L += ['', '#endif', '']
    open(QUALITY_GEN, 'w').write('\n'.join(L))
    print('  quality.gen.h  %d codes' % len(codes))


# ---- points.md -------------------------------------------------------------

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


def _point_row(r, want):
    lo, hi = r.get('lo', ''), r.get('hi', '')
    out = []
    for c in want.split():
        if   c == 'accepts': out.append('%s .. %s' % (_md(lo), _md(hi))
                                        if lo or hi else '-')
        elif c == 'access':  out.append('RW' if r.get('writable', 'true') != 'false' else 'RO')
        elif c == 'type':    out.append(_md(r.get('type') or FAC_TYPE.get(r['_fac'], '')))
        else:                out.append(_md(r.get(c)))
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
    L = ['# %s: variables and programs' % name, '',
         'GENERATED by `scripts/tables.py points`. Do not edit.', '',
         'Every row the firmware exposes, what it is for, and where an integrator',
         'finds it. ONE table: the library\'s rows are merged into the site\'s by',
         'the generator, and a site OVERRIDES one by redeclaring the name.', '',
         'One section per KIND, in the order `Vars::Kind` declares them.', '',
         '%d rows: %d declared by the site, %d by the library.'
         % (len(table), len(table) - n_lib, n_lib), '',
         '| kind | what it is | rows |', '|---|---|---|']
    group = dict((k, [r for r in table if KIND[r['_fac']] == k])
                 for k, _t, _b, _w in KINDS)
    for k, title, blurb, _w in KINDS:
        L.append('| %s | %s | %d |' % (title, blurb, len(group[k])))
    L.append('')

    for k, title, blurb, want in KINDS:
        rows = group[k]
        if not rows: continue
        L += ['## %s' % title, '', '`K_%s`: %s.' % (k, blurb), '']
        L += _table([_point_row(r, want) for r in rows], want) + ['']

    if t['cal']:
        L += ['## Analog scaling', '',
              'A raw reading maps to engineering units through four numbers, each',
              'of them fixed or a row an operator edits. `scale` and `offset` are',
              'DERIVED rather than stored, so a recalibration lands the moment the',
              'parameter commits.', '']
        L += _table([[_md(c[k]) for k in ('cal', 'raw_min_row', 'raw_min',
                                          'raw_max_row', 'raw_max',
                                          'eng_min_row', 'eng_min',
                                          'eng_max_row', 'eng_max')] for c in t['cal']],
                    'block raw_min_row raw_min raw_max_row raw_max '
                    'eng_min_row eng_min eng_max_row eng_max') + ['']

    L += ['## Programs', '',
          '%d laws, all of them IEC 61131-3 Structured Text compiled by matiec.'
          % len(prog),
          'The roster is `tables/programs.csv` and it decides the order they run',
          'in; `BotanikFirmware` owns the resource that dispatches them, one task',
          'each. A law reads and writes rows BY NAME, and nothing is passed',
          'between them.', '',
          '| # | program | rate | reads | writes | what it does |',
          '|---|---|---|---|---|---|']
    iface = {}
    for i, e in enumerate(prog, 1):
        ext = var_externals(os.path.join(site, e['source']))
        iface[e['name']] = ([n for n, _t, ro in ext if ro],
                            [n for n, _t, ro in ext if not ro])
        L.append('| %d | %s | %s | %d | %d | %s |'
                 % (i, e['name'], e['rate'], len(iface[e['name']][0]),
                    len(iface[e['name']][1]), _md(e['desc'])))
    L.append('')
    for e in prog:
        reads, writes = iface[e['name']]
        L += ['### %s' % e['name'], '',
              '`%s`, every `BOTANIK_SITE_%s_MS`.'
              % (e['source'], e['rate'].upper()), '',
              '| direction | rows |', '|---|---|',
              '| reads | %s |' % (', '.join('`%s`' % r for r in reads) or '-'),
              '| writes | %s |' % (', '.join('`%s`' % w for w in writes) or '-'), '']

    open('docs/reference/points.md', 'w').write('\n'.join(L))
    print('  points.md      %d rows, %d programs' % (len(table), len(prog)))


if __name__ == '__main__':
    cmd  = sys.argv[1] if len(sys.argv) > 1 else ''
    site = site_dir(sys.argv[2] if len(sys.argv) > 2 else None)
    if   cmd == 'generate': generate(site); quality_block()
    elif cmd == 'plant':    sys.exit(plant(site))
    elif cmd == 'config':   config(site)
    elif cmd == 'points':   points(site)
    else:                   sys.exit(__doc__)
