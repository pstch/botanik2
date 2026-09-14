#!/usr/bin/env python3
"""The site's tables, read and checked.

The model the three generators share: which CSV holds which spelling, what a
Kind is, how the library's rows merge into the site's, and every rule a table
has to satisfy. `tables.py` is the command line over it."""

import csv, os, re, sys

from .paths import ROOT, lib

DEFAULT_SITE = lib('examples', 'ref', 'src')


def site_dir(arg=None):
    """Which site to generate: the argument first, so `check-generated` can
    regenerate the site the build selected rather than the default, then the
    environment, which is how that check redirects the run into a scratch copy.
    A check never writes into the tree it is checking."""
    env = os.environ.get('BOTANIK_SITE_DIR')
    if env: return env
    if not arg: return DEFAULT_SITE
    # A bare name is an example in this tree; the example is the project and
    # src/ inside it is the site, as a site repository is laid out.
    return arg if os.sep in arg else lib('examples', arg, 'src')

VAR_NONE = 'VAR_NONE'

# factory -> (csv stem, the CSV's columns). The factory's own parameter list is
# FAC_COLS below; `qlt` is generated, not declared.
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

# Emission order within each half: parameters first and contiguous, then
# everything else, each companion directly after its row.
PARAM_FACS = ['P', 'PE', 'PS']
OTHER_FACS = ['A', 'SV', 'SVE', 'MI', 'MO', 'AI', 'AO', 'DI', 'PI', 'DO', 'RO']

TEXT = {'name', 'unit', 'desc', 'notes', 'code', 'tag', 'model_name'}
# Columns whose type depends on the factory: a text parameter's default is a
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
    'programs':       'name instance source rate desc quality park health',
    'home':           'label var',
    'system':         'label var',
}

LIB_CSV = lib('src', 'libbotanik', 'vars', 'tables', 'library.csv')
CONFIG_DIR = lib('src', 'libbotanik', 'config')
# The always-on core, from libbotanik_features.h's defaults.
CORE_FEATURES = {'CONSOLE', 'LOGGING', 'HOME_PAGE', 'SHELL_PAGE'}

H = '// GENERATED by scripts/sitegen/tables.py from tables/*.csv. Do not edit.\n'


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
        out = []
        for n, r in enumerate(rd, start=2):
            if not r or not any(c.strip() for c in r): continue
            # zip() stops at the shorter list, so a dropped comma would take a
            # trailing column out of the dict and revert it to a default.
            if len(r) != len(head):
                sys.exit('%s:%d has %d fields, the header has %d'
                         % (path, n, len(r), len(head)))
            out.append(dict(zip(head, [c.strip() for c in r])))
        return out


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
    """Which library rows this site's table carries.

    The core set counts as on even where the site turns it off: `check-bare`
    builds sites/bare with CONSOLE back on from the command line, and a row cut
    here would leave that build naming a Vars::Row that does not exist."""
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
            # The TYPE may be a site enumeration, which is not all-caps: the
            # ST spells it as enums.csv does and matiec folds it.
            mm = re.match(r'\s*([A-Za-z_]\w*)\s*:\s*([A-Za-z_]\w*)\s*;', line)
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

    One per input row, since a reading can always stop being one, plus one per
    row an opted-in program writes, because a law that can fail to compute has
    to be able to say so. Nothing else: a row with no companion is good, always.

    Alarms are excluded even under an opt-in. An alarm is a verdict rather than
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
    # The operator's per-program gate, one row each, INSIDE the K_PARAM run:
    # `programs disable` is a parameter edit and a save, which is the whole of
    # what makes the choice survive a reboot.
    rreg = int(tom['registers']['prog_run_base'])
    for e in prog:
        table.append(_gen_row('P', name='prog_%s_run' % e['name'],
                              type='T_BOOL', lo='0', hi='1', **{
            'def': '1', 'digits': '0', 'unit': '', 'reg': str(rreg),
            'desc': 'let the %s law drive' % e['name'],
            'notes': 'off takes it out of the scan and drops what it drives '
                     'to its declared safe state'}))
        rreg += 1
    n_param = len(table)

    qreg = int(tom['registers']['quality_base'])
    for r in [x for x in lib if not is_param(x)] + [x for x in own if not is_param(x)]:
        table.append(r)
        if KIND[r['_fac']] == 'INPUT' or r['name'] in wants_qlt:
            r['qlt'] = 'true'
            table.append(_gen_row('Q', name=r['name'] + '_qlt', reg=str(qreg),
                                  desc='quality of ' + r['name']))
            qreg += 1

    # The program rows, last and in roster order: a health row per program that
    # opted in, then the parking pair per program that opted into that.
    #
    # FAULTED is the declared value, which is also where the row goes when the
    # program leaves the scan: a law that has never run has answered nothing,
    # and saying HEALTHY there would be the firmware speaking for it.
    preg = int(tom['registers']['prog_base'])
    for e in prog:
        if not _opted_in(e, 'health'): continue
        table.append(_gen_row('SVE', name='prog_%s_health' % e['name'], **{
            'def': '2', 'label': '&Plc::health_label', 'reg': str(preg),
            'desc': 'what the %s law says about its own answer' % e['name'],
            'notes': 'FAULTED until it has run: the law writes it, not Plant'}))
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
            'dev': read_table(os.path.join(site, 'tables', 'modbus_devices.csv'),
                              AUX['modbus_devices'].split()),
            'home': read_table(os.path.join(site, 'tables', 'home.csv'),
                               AUX['home'].split()),
            # Optional: a site with nothing to add to the SYSTEM band declares
            # none, and gets the library's own rows alone.
            'system': (read_table(os.path.join(site, 'tables', 'system.csv'),
                                  AUX['system'].split())
                       if os.path.exists(os.path.join(site, 'tables', 'system.csv'))
                       else [])}


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
    # master addresses. An input or an output has none, since its `reg`
    # addresses a device on the field bus, and a status value with register 0 is
    # not published.
    regs = {}
    gen_base = min(int(t['toml']['registers'][k])
                   for k in ('quality_base', 'park_base', 'prog_base',
                             'prog_run_base'))
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

    # A field input that can never go stale is a trap: nothing expires a row
    # whose timeout is 0, so once its device goes quiet it holds its last
    # reading, believed, for the life of the firmware. Refused here rather than
    # at bring-up, which would be a table defect discovered on hardware.
    for r in table:
        if KIND[r['_fac']] != 'INPUT': continue
        if r.get('device', '0') == '0': continue
        if (r.get('timeout_ms') or '0').strip() in ('', '0'):
            bad.append('%s: a field input needs a timeout_ms, or nothing ever '
                       'stops believing it' % r['name'])

    # A local row must be a kind the controller's own terminals can be. The
    # OPTA has eight inputs, four relays, four LEDs and a button, and no analog
    # output. Without this an AO row on device 0 declares, generates and passes
    # every other check, and is then driven by nothing.
    for r in table:
        if r['_fac'] in ('AO', 'MI', 'MO') and r.get('device') == '0':
            bad.append('%s: device 0 means LOCAL, and there is no local %s'
                       % (r['name'], r['_fac']))

    # Every row a band end names must exist, share the referring row's type, and
    # leave that row's own default inside the band. That is what keeps a pair
    # reachable: an operator can move one end and then the other, but only if
    # the pair started valid.
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

    for h in t['system']:
        if h['var'] not in index:
            bad.append('system field %r names no row %s' % (h['label'], h['var']))

    # An alarm's code is a column on the ALARMS page, and a longer one is
    # truncated where an operator reads it.
    for r in table:
        if r['_fac'] == 'A' and len(r['code']) > 9:
            bad.append('%s: alarm code %r is longer than 9' % (r['name'], r['code']))

    # An alarm's CODE is what the bar and the page show, so two alarms sharing
    # one are indistinguishable exactly where an operator is reading them.
    codes = {}
    for r in table:
        if r['_fac'] != 'A': continue
        c = r['code']
        if not c:
            bad.append('%s: an alarm needs a code' % r['name'])
            continue
        if c in codes:
            bad.append('%s and %s share alarm code %r' % (codes[c], r['name'], c))
        codes[c] = r['name']

    # The field bus, which the register check above skips because `reg` there
    # addresses a device rather than the TCP map. Two rows on one device's same
    # register is a typo either way: as an output the last writer wins.
    used = {}
    for r in table:
        if KIND[r['_fac']] not in ('INPUT', 'OUTPUT'): continue
        d = r.get('device', '')
        if not d or d == '0': continue          # local, checked by terminal above
        # The address column is per factory, as it is for a local terminal:
        # a coil row has no `reg`.
        addr = r.get('reg') or r.get('coil') or r.get('channel') or ''
        if addr == '': continue
        k = (d, r.get('fc', ''), addr)
        if k in used:
            bad.append('%s and %s both claim device %s fc %r address %s'
                       % (used[k], r['name'], k[0], k[1], k[2]))
        used[k] = r['name']

    # ...and the device has to be one the site declares, or the master plans a
    # unit that will never answer.
    addrs = set(d['addr'] for d in t['dev'])
    if addrs:
        for r in table:
            if KIND[r['_fac']] not in ('INPUT', 'OUTPUT'): continue
            d = r.get('device', '')
            if not d or d == '0' or d in addrs: continue
            bad.append('%s: device %s is in no modbus_devices.csv row' % (r['name'], d))

    # A calibration with its ends the wrong way round scales every reading
    # backwards, and the band check above only sees rows, not these.
    for c in t['cal']:
        for lo, hi in (('raw_min', 'raw_max'), ('eng_min', 'eng_max'),
                       ('min', 'max')):
            a, b2 = _num(c[lo].rstrip('f')), _num(c[hi].rstrip('f'))
            if a is None or b2 is None: continue
            if a >= b2:
                bad.append('%s: %s (%g) is not below %s (%g)'
                           % (c['cal'], lo, a, hi, b2))

    if bad:
        for b in bad: print('  ' + b, file=sys.stderr)
        sys.exit('%d table error(s)' % len(bad))


# ---- emitting --------------------------------------------------------------

def quote(v, col, fac=None):
    if col in TEXT or col in TEXT_BY_FAC.get(fac, ()):
        # Backslash first: escaping the quotes of a value that already ends in
        # one would produce an escaped quote and never close the literal.
        return '"%s"' % v.replace('\\', '\\\\').replace('"', '\\"')
    if col in SYMBOL and v and not _numeric(v) and not v.startswith(SYMBOL[col]):
        return SYMBOL[col] + v
    return v


def _cell(r, col, fac):
    v = r.get(col, '')
    if col in ROW_COLS:
        return ('Row::' + v) if v else VAR_NONE
    if col in BOOLS or (col == 'def' and fac in BOOL_DEF):
        if not v: return BOOLS.get(col, 'false')
        return 'true' if v.lower() in ('1', 'yes', 'true') else 'false'
    if col == 'poll_ms' and not v: return '0'
    return quote(v, col, fac)


def emit_row(r):
    fac = r['_fac']
    a = [_cell(r, c, fac) for c in FAC_COLS[fac]]
    tail = TAIL.get(fac, {})
    cols = FAC_COLS[fac]
    while a and (a[-1] == '' or a[-1] == tail.get(cols[len(a) - 1])):
        a.pop()
    return '    %s(%s),' % (fac, ', '.join(a))


def emit_cals(cal):
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
