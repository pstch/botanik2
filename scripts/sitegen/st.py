#!/usr/bin/env python3
"""The ST half: the site's enumerated types, the resource matiec compiles, and
the roster `Plc::Runtime::Plant` runs.

Everything here needs the compiler's own answer, what each body writes, so the
checks that belong to a law live here rather than beside the table's."""

import os, re, sys

from . import render
from .paths import ROOT, lib
from .model import (H, H_ST, KIND, _opted_in, lib_table, row_ref, site_guard,
                    st_files, var_externals)

# ---- the site's enumerations, on both sides ---------------------------------

def enums(site, t):
    """`generated/enums.st` and `generated/enums.gen.h` from enums.csv.

    One declaration, two languages. matiec numbers an enumerated type 0,1,2...
    in declaration order and rejects explicit values, so the ST side can only
    spell a set contiguous from zero. Checked here: the codes are also a Modbus
    contract, and a gap in one would silently renumber the other.

    The C++ side is the same type, not a second one with equal values: matiec's
    own member list is discarded and these constants take its place, exactly as
    QUALITY does. A member added to the CSV and not regenerated fails to
    compile on the name matiec emits for it."""
    en = t['enums']
    names = list(dict.fromkeys(e['enum'] for e in en))
    groups = [(n, [e for e in en if e['enum'] == n]) for n in names]

    bad = []
    for n, mem in groups:
        for i, e in enumerate(mem):
            if int(e['value'], 0) != i:
                bad.append('%s.%s is %s, and an ST enumeration is numbered by '
                           'its declaration order (%d here)'
                           % (n, e['symbol'], e['value'], i))
    if bad:
        for b in bad: print('  ' + b, file=sys.stderr)
        sys.exit('%d enumeration(s) ST cannot spell' % len(bad))

    ctx = [{'name': n, 'upper': n.upper(), 'ctype': mem[0]['ctype'],
            'members': mem} for n, mem in groups]

    render.write(os.path.join(site, 'generated', 'enums.st'), 'enums.st.j2',
                 banner=H_ST, groups=ctx)
    render.write(os.path.join(site, 'generated', 'enums.gen.h'), 'enums.gen.h.j2',
                 banner=H, guard=site_guard(t['toml']), groups=ctx)
    print('  enums.gen.h    %d enumeration(s), %d code(s)' % (len(groups), len(en)))

# ---- plant -----------------------------------------------------------------

def iec2c():
    """IEC2C= when set, else the pinned matiec, built once per checkout.

    Only a site with laws gets here, so a site without any never needs the
    compiler, nor the autotools that build it."""
    if os.environ.get('IEC2C'): return os.environ['IEC2C']
    import subprocess
    r = subprocess.run([os.environ.get('CMAKE', 'cmake'),
                        '-DSRC=' + lib('vendor', 'matiec'),
                        '-P', lib('cmake', 'matiec-build.cmake')])
    if r.returncode:
        sys.exit('the pinned matiec did not build; point IEC2C= at another iec2c')
    return lib('vendor', 'matiec', 'build', 'iec2c')


# What `iec2c -I` is pointed at: the directory holding OUR ieclib.txt.
IECLIB_DIR = lib('src', 'libbotanik', 'plc', 'functions')
# ORDER MATTERS: matiec needs a block declared before it is instanced, and there
# is no forward declaration. filters before analog, because DC_RAIL holds an EWMA.
LIBST = [lib('src', 'libbotanik', 'plc', p) for p in (
    'functions/quality.st', 'functions/health.st', 'functions/math.st',
    'functions/psychro.st', 'functions/time.st',
    'blocks/timers.st', 'blocks/filters.st', 'blocks/analog.st',
    'blocks/pid.st', 'blocks/actuator.st', 'blocks/meter.st')]


def writes_by_pou(site, prog):
    """What each PROGRAM writes, off matiec's own generated C, as a set of
    upper-case names per POU, empty for a program that writes nothing.

    A write is `__SET_EXTERNAL` - our macro, emitted from matiec's parse. So this
    is the COMPILER's answer, not a second reading of the ST: no comments to
    skip, and a call's named arguments cannot look like an assignment."""
    src = open(os.path.join(site, 'generated', 'POUS.c')).read()
    out = dict((e['pou'], set()) for e in prog)
    for m in re.finditer(r'^static void (\w+)_body__\(.*?^\}', src, re.M | re.S):
        out[m.group(1)] = {w.upper() for w in
                           re.findall(r'__SET_EXTERNAL\(\w+->,\s*(\w+)\s*,', m.group(0))}
    return out


def pou_of(path):
    m = re.search(r'^PROGRAM (\w+)', open(path).read(), re.M)
    return m.group(1).upper() if m else sys.exit('%s: no PROGRAM' % path)


def emit_plant_cpp(site, t, prog, writes):
    home = _spelling(t['index'])
    ext = sorted({n for e in prog for n, _t, _ro in var_externals(e['path'])})
    externals = [{'upper': n.upper(), 'row': home(n)} for n in ext]

    programs = []
    for e in prog:
        park = _opted_in(e, 'park')
        mine = [n for n, _t, ro in var_externals(e['path'])
                if not ro and n.upper() in writes[e['pou']]]
        programs.append({
            'name': e['name'], 'pou': e['pou'], 'inst': e['instance'].upper(),
            'rate': e['rate'].upper(),
            'writes': ''.join(' Vars::Row::%s,' % home(n) for n in mine) or ' 0',
            'park': row_ref(park and 'prog_%s_park' % e['name'], 'Vars::'),
            'parked': row_ref(park and 'prog_%s_parked' % e['name'], 'Vars::'),
            'health': row_ref(_opted_in(e, 'health') and 'prog_%s_health' % e['name'],
                              'Vars::'),
            'gate': row_ref('prog_%s_run' % e['name'], 'Vars::')})

    render.write(os.path.join(site, 'generated', 'plant.cpp'), 'plant.cpp.j2',
                 externals=externals, programs=programs)


def _spelling(index):
    """The table's own spelling of a name an ST file wrote in any case."""
    folded = dict((k.lower(), k) for k in index)
    return lambda n: n if n in index else folded[n.lower()]


def check_laws(t, prog, writes):
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
        for n, _ty, ro in var_externals(e['path']):
            if ro: continue
            if n.upper() not in writes[e['pou']]:
                bad.append('%s: declares %s writable and never writes it'
                           % (e['name'], n))
                continue
            if n in author: shared.append(n)
            else: author[n] = e['name']
    if shared:
        print('  %d row(s) with two authors (read-modify-write): %s'
              % (len(shared), ', '.join(shared)))

    # ...and the other direction: a row nothing writes. An output, an alarm or
    # a status row with no author is a law that was meant to be written and was
    # not, and it reads its declared value for the life of the plant with
    # nothing to say so.
    every = set().union(*writes.values())
    for r in table:
        k = KIND[r['_fac']]
        if k not in ('OUTPUT', 'ALARM', 'STATUS'): continue
        # The library's own rows are written by the library, in C++: its alarms
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
    alarms = {n.upper() for n in alarms}
    for e in prog:
        wr = writes[e['pou']]
        if _opted_in(e, 'quality'):
            for n in sorted(wr):
                # An alarm is a verdict, not a reading, and so is anything a
                # program says about ITSELF - its health, its parked answer.
                if n.endswith('_QLT') or n.startswith('PROG_') or n in alarms: continue
                if n + '_QLT' not in wr:
                    bad.append('%s: writes %s and owes %s_qlt'
                               % (e['name'], n.lower(), n.lower()))
        # HEALTH is declared FAULTED, so a health row nothing writes reads as a
        # program that could not answer; a program told to park that never
        # answers holds the shutdown open until plc_parkTimeout_sec runs out.
        for col, row, what in (('health', 'health', 'opts into health'),
                               ('park', 'parked', 'parks')):
            if _opted_in(e, col) and ('PROG_%s_%s' % (e['name'], row)).upper() not in wr:
                bad.append('%s: %s and owes prog_%s_%s' % (e['name'], what, e['name'], row))

    if bad:
        for b in bad: print('  ' + b, file=sys.stderr)
        sys.exit('%d law error(s)' % len(bad))


def plant(site, t):
    """plant.st and plant.cpp from programs.csv, then matiec."""
    import subprocess, tempfile
    prog = t['prog']
    # A site may declare no laws at all. There is no plant to
    # compile then, and an ST resource with no program in it is not valid ST -
    # matiec refuses it. Nothing consumes plant.cpp either: the scan is
    # BOTANIK_FEATURE_PLC's, and a site with no programs has it off.
    if not prog:
        print('  plant          no programs, nothing to compile')
        return
    for e in prog:
        e['path'] = os.path.join(site, e['source'])
        e['pou'] = pou_of(e['path'])
    # IEC identifiers are case-insensitive, so a program INSTANCE that folds
    # onto an enumerated type is one name for two things - and matiec says only
    # "invalid program name", about the RESOURCE, three thousand lines away.
    types = {e['enum'].lower() for e in t['enums']}
    clash = [e['name'] for e in prog if e['instance'].lower() in types]
    if clash:
        sys.exit('%s: the instance name folds onto an enumerated type; '
                 'rename it in programs.csv' % ', '.join(clash))
    sources = st_files(site)
    rows, whence = {}, {}
    for p in sources:
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

    pl = os.path.join(site, 'generated', 'plant.st')
    render.write(pl, 'plant.st.j2', banner=H_ST,
                 # The DECLARED name: matiec names its output after the ST
                 # CONFIGURATION, and a site repository keeps its site under
                 # src/, which would make every one of them src.c.
                 config_name=t['toml']['site']['name'],
                 globals_=sorted(rows.items()),
                 programs=[{'instance': e['instance'], 'pou': e['pou'].lower()}
                           for e in prog])

    # matiec FIRST: plant.cpp's write rosters are read out of the POUS.c it
    # emits, so the compiler's own answer is what they are built from.
    with tempfile.NamedTemporaryFile('w', suffix='.st', delete=False) as fh:
        for f in LIBST + [os.path.join(site, 'generated', 'enums.st')] + sources + [pl]:
            fh.write(open(f).read() + '\n')
        tmp = fh.name
    # -I OURS: matiec loads <dir>/ieclib.txt unconditionally, and its own
    # declares 28 function blocks under names we use for our own - and is GPL
    # besides.
    compiler = iec2c()
    try:
        r = subprocess.run([compiler, '-I', IECLIB_DIR,
                            '-T', os.path.join(site, 'generated'), tmp],
                           capture_output=True, text=True)
    except FileNotFoundError:
        sys.exit('iec2c not found at %r' % compiler)
    finally:
        os.unlink(tmp)
    err = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l.lower()]
    if err:
        for l in err[:6]: print('  ' + l, file=sys.stderr)
        sys.exit('matiec refused the ST')

    writes = writes_by_pou(site, prog)
    check_laws(t, prog, writes)
    emit_plant_cpp(site, t, prog, writes)
    print('  plant.st       %d globals, %d POUs' % (len(rows), len(sources)))


# ---- the ST enumerations the LIBRARY owns -----------------------------------

# (ST type, header the C++ enum is declared in, the enum's own declaration, how
#  a member is spelled in C++, the member pattern). Both are types matiec must be
#  handed rather than allowed to number itself.
LIB_ENUMS = [
    ('QUALITY', lib('src', 'libbotanik', 'vars', 'descriptor.h'),
     r'enum Quality : uint8_t \{(.*?)\n    \};',
     '::Botanik::Vars::%s', r'^\s*(Q_[A-Z_]+)'),
    ('HEALTH', lib('src', 'libbotanik', 'plc', 'health.h'),
     r'enum class Health : uint8_t \{(.*?)\n    \};',
     '::Botanik::Plc::Health::%s',
     r'^\s*([A-Z][A-Z_]+)\s*(?:=\s*\d+)?\s*,?\s*(?:///.*)?$'),
]


def _lib_enum(st, header, decl, spell, member):
    """One accessor enumeration block, off the header that declares the enum.

    matiec initialises a QUALITY-returning function with `QUALITY__Q_NONE`, so
    the members have to be named constants of the REAL C++ type. Generated so a
    code added to one side and not the other cannot compile."""
    m = re.search(decl, open(header).read(), re.S)
    if not m: sys.exit('%s: no `%s`' % (header, decl))
    codes = re.findall(member, m.group(1), re.M)
    if not codes: sys.exit('%s: no members for %s' % (header, st))
    out = lib('src', 'libbotanik', 'plc', 'runtime', '%s.gen.h' % st.lower())
    # The banner names the header the way the tree does. An absolute path would
    # put the generating machine into a committed file.
    rel = os.path.relpath(header, ROOT)
    render.write(out, 'lib_enum.gen.h.j2', header=rel, st=st,
                 guard='BOTANIK_PLC_RUNTIME_%s_GEN_H' % st,
                 codes=[{'name': c, 'spelt': spell % c} for c in codes])
    return st.lower(), len(codes)


def _rows_none():
    """Every row library.csv declares, bound to VAR_NONE.

    vars/rows.h falls back to this when the build names no site, so the library
    compiles for a consumer that wants a component and no table. Emitted from
    library.csv alone, which is why it is the LIBRARY's artifact and not a
    site's."""
    rows = [r['name'] for r in lib_table()]
    render.write(lib('src', 'libbotanik', 'vars', 'rows_none.gen.h'),
                 'rows_none.gen.h.j2', rows=rows)
    return len(rows)


def quality_block():
    """The accessor blocks for every enumerated type the library owns."""
    for e in LIB_ENUMS:
        name, n = _lib_enum(*e)
        print('  %-14s %d codes' % (name + '.gen.h', n))
    print('  %-14s %d rows' % ('rows_none.gen.h', _rows_none()))
