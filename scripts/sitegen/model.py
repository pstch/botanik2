#!/usr/bin/env python3
"""The site's tables, read and checked.

The model the three generators share: which CSV holds which spelling, what a
Kind is, how the library's rows merge into the site's, and every rule a table
has to satisfy. `tables.py` is the command line over it."""

import csv, functools, os, re, sys

from .paths import ROOT, lib

DEFAULT_SITE = lib('examples', 'ref', 'src')


def site_dir(arg=None):
    """A site path, or a bare name for an example in this tree, whose src/ is
    the site as a site repository lays it out."""
    if not arg: return DEFAULT_SITE
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
    'MI':  ('modbus_in',   'name type device fc reg regs lo hi lo_row hi_row def timeout_ms digits unit desc notes poll_ms word_swap obj'),
    'MO':  ('modbus_out',  'name type device fc reg regs safe digits unit desc notes word_swap obj'),
    'AI':  ('analog_in',   'name cal device fc reg regs wire def timeout_ms digits unit desc notes poll_ms word_swap obj'),
    'AO':  ('analog_out',  'name cal device fc reg regs wire safe digits unit desc notes word_swap obj'),
    'DI':  ('digital_in',  'name device fc reg def timeout_ms invert desc notes poll_ms'),
    'DO':  ('digital_out', 'name device fc reg safe invert desc notes'),
    'RO':  ('relay_out',   'name device coil safe invert desc notes'),
    'PI':  ('pulse_in',    'name channel edge desc notes'),
}

# Columns a table declares for a READER: the variables page prints them and the
# firmware never sees them, so they cost no flash on a row.
DOC_COLS = {'obj'}

# What each factory takes, in order. Equal to the CSV's columns except where the
# generator supplies one, minus the documentation-only ones.
FAC_COLS = dict((f, [c for c in cols.split() if c not in DOC_COLS])
                for f, (_s, cols) in ROWS.items())
for _f in ('SV', 'MI', 'AI', 'DI', 'PI'):
    FAC_COLS[_f].append('qlt')
FAC_COLS['SVE'] += ['retained', 'qlt']
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
TAIL['PE'] = TAIL['PS'] = TAIL['P']

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

# The contract between what the generator emits and what the library compiles.
# Here rather than in codegen_abi.h, which is GENERATED from it: the generator
# must never have to read C++ to learn something it owns. Bump it with any
# change to a template or to the generator that the library's headers depend on.
CODEGEN_ABI = 1

LIB_CSV = lib('src', 'libbotanik', 'vars', 'tables', 'library.csv')
LIB_ENUMS_CSV = lib('src', 'libbotanik', 'vars', 'tables', 'library_enums.csv')
CAPACITIES_CSV = lib('src', 'libbotanik', 'config', 'capacities.csv')
FEATURES_TOML = lib('src', 'libbotanik_features.toml')

H = '// GENERATED by scripts/sitegen/tables.py from tables/*.csv. Do not edit.'
H_ST = H.replace('//', '(*') + ' *)'

CAL_ENDS = ('raw_min', 'raw_max', 'eng_min', 'eng_max', 'min', 'max')


# ---- reading ---------------------------------------------------------------

def _numeric(v):
    try: int(v, 0); return True
    except ValueError: return False


def features():
    """libbotanik_features.toml's flags, in file order."""
    return toml(FEATURES_TOML)['features']


def core_features():
    """The flags a build that sets nothing gets.

    Derived, not listed: a default is a literal or the name of another flag
    (HOME_PAGE and SHELL_PAGE follow CONSOLE), so resolving it is the same walk
    the preprocessor does. The set used to be hand-copied here, which is one
    more place for the header to drift away from."""
    val = {f['name']: f['default'] for f in features()}
    def on(name, seen=()):
        v = val[name]
        if v.isdigit(): return v != '0'
        dep = v.replace('BOTANIK_FEATURE_', '')
        return dep not in seen and on(dep, seen + (name,))
    return {n for n in val if on(n)}


def lib_enums():
    """vars/tables/library_enums.csv, as rows.

    The members of every enumeration the LIBRARY owns, in declaration order.
    One statement feeds the C++ enum, the block matiec is handed, and the check
    against the hand-written ST TYPE - so a code cannot exist on one side
    alone."""
    return read_table(LIB_ENUMS_CSV, 'enum ctype symbol value label desc'.split())


def capacities():
    """config/capacities.csv, as rows.

    A library text row states its capacity as one of these names rather than a
    number, and config/capacities.gen.h is generated from the same file - so
    the pool the generator sizes and the buffer the library declares are one
    statement. Data, never a config header: the generator does not read C++."""
    return read_table(CAPACITIES_CSV, 'name value desc'.split())


def config_defaults():
    """Every capacity name, as an int."""
    return {c['name']: int(c['value'], 0) for c in capacities()}


def as_int(v, defs):
    """A CSV cell that is a number, or a config macro standing for one."""
    try: return int(v, 0)
    except ValueError: pass
    if v in defs: return defs[v]
    sys.exit('%r is neither a number nor a config default' % v)


def read_table(path, cols):
    """One table, as dicts. A file may stop short of the last columns, which is
    what lets a column be added to a factory without every site's file moving;
    the columns it does carry have to be the first ones, in order."""
    if not os.path.exists(path): return []
    with open(path, newline='') as fh:
        rd = csv.reader(fh)
        head = [h.strip() for h in next(rd)]
        if head != cols[:len(head)]:
            sys.exit('%s: header %s is not the first %d of %s'
                     % (path, head, len(head), cols))
        tail = dict((c, '') for c in cols[len(head):])
        out = []
        for n, r in enumerate(rd, start=2):
            if not r or not any(c.strip() for c in r): continue
            # zip() stops at the shorter list, so a dropped comma would take a
            # trailing column out of the dict and revert it to a default.
            if len(r) != len(head):
                sys.exit('%s:%d has %d fields, the header has %d'
                         % (path, n, len(r), len(head)))
            row = dict(tail)
            row.update(zip(head, [c.strip() for c in r]))
            out.append(row)
        return out


def aux(site, stem):
    """One of the site's tables that is not rows; empty where it has none."""
    return read_table(os.path.join(site, 'tables', stem + '.csv'), AUX[stem].split())


def lib_table():
    """library.csv, whose `fac` column says which factory each row is."""
    with open(LIB_CSV, newline='') as fh:
        return [dict((k, (v or '').strip()) for k, v in r.items())
                for r in csv.DictReader(fh)]


def _scalar(v):
    if v in ('true', 'false'):                 return v == 'true'
    if v[0] == v[-1] == '"':                   return v[1:-1]
    if v[0] == '[':
        return [x.strip().strip('"') for x in v[1:-1].split(',') if x.strip()]
    return int(v)


def toml(path):
    """Enough TOML for our own files: tables, arrays of tables, scalar
    `key = value`, inline `["a", "b"]` lists, and triple-quoted blocks.

    Ours rather than tomllib's, which is python 3.11 and up: a site repository
    regenerates with whatever python3 it has, and the subset we write is small
    enough to read in thirty lines."""
    out, sec, key, block = {}, None, None, None
    for ln in open(path):
        if block is not None:                      # inside a """ ... """ value
            if ln.rstrip('\n').rstrip() == '"""':
                sec[key] = '\n'.join(block).strip('\n')
                block = None
            else:
                block.append(ln.rstrip('\n'))
            continue
        ln = ln.split('#')[0].strip() if not ln.lstrip().startswith('#') else ''
        if not ln: continue
        if ln.startswith('[['):
            sec = {}
            out.setdefault(ln.strip('[]'), []).append(sec)
        elif ln[0] == '[':
            sec = out.setdefault(ln.strip('[]'), {})
        else:
            key, v = (x.strip() for x in ln.split('=', 1))
            if v == '"""': block = []
            else:          sec[key] = _scalar(v)
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
    return core_features() | {k for k, v in tom.get('features', {}).items() if v}


def lib_rows(tom):
    """The library's own rows, filtered to what this site's flags compile.

    ORDER IS THE FILE'S: the rows interleave kinds, and the parameter run has to
    stay contiguous, so one table with a `fac` column rather than one per kind.
    A guard is AND-ed flags; a flag the site does not name takes the header's
    default, which is off for every optional one."""
    on = features_on(tom)
    out = []
    for row in lib_table():
        if not all(g in on for g in row['guard'].split()): continue
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


@functools.lru_cache(maxsize=None)
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
    return tuple(out)


def _yes(v):
    return v.strip().lower() in ('1', 'yes', 'true')


def _opted_in(e, col):
    return _yes(e[col])


def row_ref(name, ns=''):
    """A row named by index, or VAR_NONE where no row is named."""
    return '%sRow::%s' % (ns, name) if name else ns + VAR_NONE


def address(r):
    """The field address, whichever column the factory keeps it in."""
    return r.get('reg') or r.get('coil') or r.get('channel') or ''


# ---- the table -------------------------------------------------------------

def quality_names(site, prog, alarms):
    """Which rows a law's opt-in gives a `_qlt` companion, beside the inputs,
    which always get one.

    Every row an opted-in program writes, because a law that can fail to compute
    has to be able to say so. Alarms are excluded even then: an alarm is a
    verdict rather than a reading, and AlarmController has nothing to
    substitute, so a quality beside it would be a row nothing consults.

    Off the ST's own `VAR_EXTERNAL` sections rather than matiec's output, so this
    does not depend on a POUS.c the same run is about to replace."""
    return {n for e in prog if _opted_in(e, 'quality')
            for n, _t, ro in var_externals(os.path.join(site, e['source']))
            if not ro and not n.endswith('_qlt') and n not in alarms}


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


def _numbered(tom, key, rows):
    """@p rows, registered in order from `[registers] <key>`."""
    for i, r in enumerate(rows): r['reg'] = str(int(tom['registers'][key]) + i)
    return rows


GEN_BASES = ('quality_base', 'prog_park_base', 'prog_health_base', 'prog_run_base')


def build(site):
    """The whole table, ordered and indexed, plus what is emitted beside it."""
    tom  = toml(os.path.join(site, 'site.toml'))
    prog = aux(site, 'programs')
    lib, own = _merge(site, tom)
    merged = lib + own
    wants_qlt = quality_names(site, prog,
                              {r['name'] for r in merged if KIND[r['_fac']] == 'ALARM'})

    def is_param(r): return KIND[r['_fac']] == 'PARAM'

    # The operator's per-program gate, one row each, INSIDE the K_PARAM run:
    # `programs disable` is a parameter edit and a save, which is the whole of
    # what makes the choice survive a reboot.
    table = [r for r in merged if is_param(r)] + _numbered(tom, 'prog_run_base', [
        _gen_row('P', name='prog_%s_run' % e['name'], type='T_BOOL', lo='0', hi='1',
                 digits='0', unit='', desc='let the %s law drive' % e['name'],
                 notes='off takes it out of the scan and drops what it drives '
                       'to its declared safe state', **{'def': '1'})
        for e in prog])
    n_param = len(table)

    qlt = []
    for r in merged:
        if is_param(r): continue
        table.append(r)
        if KIND[r['_fac']] == 'INPUT' or r['name'] in wants_qlt:
            r['qlt'] = 'true'
            qlt.append(_gen_row('Q', name=r['name'] + '_qlt', desc='quality of ' + r['name']))
            table.append(qlt[-1])
    _numbered(tom, 'quality_base', qlt)

    # The program rows, last and in roster order: a health row per program that
    # opted in, then the parking pair per program that opted into that.
    #
    # FAULTED is the declared value, which is also where the row goes when the
    # program leaves the scan: a law that has never run has answered nothing,
    # and saying HEALTHY there would be the firmware speaking for it.
    table += _numbered(tom, 'prog_health_base', [
        _gen_row('SVE', name='prog_%s_health' % e['name'], label='&Plc::health_label',
                 desc='what the %s law says about its own answer' % e['name'],
                 notes='FAULTED until it has run: the law writes it, not Plant',
                 **{'def': '2'})
        for e in prog if _opted_in(e, 'health')])
    table += _numbered(tom, 'prog_park_base', [
        _gen_row('SV', name='prog_%s_%s' % (e['name'], suffix), type='T_BOOL',
                 digits='0', unit='', desc='%s %s' % (e['name'], what), notes='',
                 **{'def': '0'})
        for e in prog if _opted_in(e, 'park')
        for suffix, what in (('park', 'is parking to its safe state'),
                             ('parked', 'has reached its parked state'))])

    for i, r in enumerate(table): r['_i'] = i
    return {'toml': tom, 'prog': prog, 'table': table,
            'index': dict((r['name'], r['_i']) for r in table),
            'param_count': n_param, 'enums': aux(site, 'enums'),
            'cal': aux(site, 'analog_cal'), 'dev': aux(site, 'modbus_devices'),
            'models': aux(site, 'modbus_models'), 'home': aux(site, 'home'),
            # A site with nothing to add to the SYSTEM band declares none.
            'system': aux(site, 'system')}


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
    gen_base = min(int(t['toml']['registers'][k]) for k in GEN_BASES)
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

    # The rows on a terminal: the controller's own (device 0) or a field device.
    terminals, field, addrs = {}, {}, set(d['addr'] for d in t['dev'])
    for r in table:
        kind = KIND[r['_fac']]
        if kind not in ('INPUT', 'OUTPUT'): continue
        d, addr = r.get('device', ''), address(r)
        if d == '0':
            # Configuring one pin two ways is a hardware fault waiting for the
            # first wet week. The OPTA has eight inputs, four relays, four LEDs
            # and a button, and no analog output: a local row of another factory
            # declares, generates and is then driven by nothing.
            if (kind, addr) in terminals:
                bad.append('%s and %s share local terminal %s'
                           % (terminals[kind, addr], r['name'], addr))
            terminals[kind, addr] = r['name']
            if r['_fac'] in ('AO', 'MI', 'MO'):
                bad.append('%s: device 0 means LOCAL, and there is no local %s'
                           % (r['name'], r['_fac']))
            continue
        if not d: continue
        # Nothing expires a field input whose timeout is 0, so once its device
        # goes quiet it holds its last reading, believed, for the life of the
        # firmware.
        if kind == 'INPUT' and (r.get('timeout_ms') or '0') == '0':
            bad.append('%s: a field input needs a timeout_ms, or nothing ever '
                       'stops believing it' % r['name'])
        # Two rows on one device's same register is a typo either way: as an
        # output the last writer wins.
        k = (d, r.get('fc', ''), addr)
        if addr and k in field:
            bad.append('%s and %s both claim device %s fc %r address %s'
                       % (field[k], r['name'], d, k[1], addr))
        field[k] = r['name']
        # ...and a device no table declares is a unit that will never answer.
        if addrs and d not in addrs:
            bad.append('%s: device %s is in no modbus_devices.csv row' % (r['name'], d))

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

    # A calibration's tunables the same way, and its ends the right way round:
    # the other way scales every reading backwards.
    for c in t['cal']:
        for k in CAL_ENDS:
            if c[k + '_row'] and c[k + '_row'] not in index:
                bad.append('%s: %s names no row %s' % (c['cal'], k, c[k + '_row']))
        for lo, hi in zip(CAL_ENDS[::2], CAL_ENDS[1::2]):
            a, b2 = _num(c[lo].rstrip('f')), _num(c[hi].rstrip('f'))
            if a is not None and b2 is not None and a >= b2:
                bad.append('%s: %s (%g) is not below %s (%g)'
                           % (c['cal'], lo, a, hi, b2))

    for band in ('home', 'system'):
        for h in t[band]:
            if h['var'] not in index:
                bad.append('%s field %r names no row %s' % (band, h['label'], h['var']))

    # An alarm's CODE is what the bar and the page show: a longer one is
    # truncated there, and two alarms sharing one are indistinguishable.
    codes = {}
    for r in table:
        if r['_fac'] != 'A': continue
        c = r['code']
        if not c:
            bad.append('%s: an alarm needs a code' % r['name'])
            continue
        if len(c) > 9:
            bad.append('%s: alarm code %r is longer than 9' % (r['name'], c))
        if c in codes:
            bad.append('%s and %s share alarm code %r' % (codes[c], r['name'], c))
        codes[c] = r['name']

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
        return row_ref(v)
    if col in BOOLS or (col == 'def' and fac in BOOL_DEF):
        if not v: return BOOLS.get(col, 'false')
        return 'true' if _yes(v) else 'false'
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
        L.append('const AnalogCal %s = {' % c['cal'])
        L.append('    ' + ' '.join('SF(%s),' % c[k].rstrip('f') for k in CAL_ENDS))
        L.append('    { ' + ', '.join(row_ref(c[k + '_row']) for k in CAL_ENDS) + ' },')
        L += ['};', '']
    return L
