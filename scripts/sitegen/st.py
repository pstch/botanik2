#!/usr/bin/env python3
"""The ST half: the site's enumerated types, the resource matiec compiles, and
the roster `Plc::Runtime::Plant` runs.

Everything here needs the compiler's own answer, what each body writes, so the
checks that belong to a law live here rather than beside the table's."""

import os, re, sys

from . import render
from .model import (AUX, H, KIND, _opted_in, build, read_table, site_guard,
                         st_files, toml, var_externals)

# ---- the site's enumerations, on both sides ---------------------------------

def enums(site):
    """`generated/enums.st` and `generated/enums.gen.h` from enums.csv.

    One declaration, two languages. matiec numbers an enumerated type 0,1,2...
    in declaration order and rejects explicit values, so the ST side can only
    spell a set contiguous from zero. Checked here: the codes are also a Modbus
    contract, and a gap in one would silently renumber the other.

    The C++ side is the same type, not a second one with equal values: matiec's
    own member list is discarded and these constants take its place, exactly as
    QUALITY does. A member added to the CSV and not regenerated fails to
    compile on the name matiec emits for it."""
    en = read_table(os.path.join(site, 'tables', 'enums.csv'), AUX['enums'].split())
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
                 banner=H.replace('//', '(*').rstrip() + ' *)', groups=ctx)

    guard = site_guard(toml(os.path.join(site, 'site.toml')))
    render.write(os.path.join(site, 'generated', 'enums.gen.h'), 'enums.gen.h.j2',
                 banner=H.rstrip('\n'), guard=guard, groups=ctx)
    print('  enums.gen.h    %d enumeration(s), %d code(s)' % (len(groups), len(en)))



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
         'src/libbotanik/plc/functions/health.st',
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
    ext = sorted({n for e in prog for n, _t, _ro in var_externals(e['path'])})
    externals = [{'upper': n.upper(), 'row': _home(index, n)} for n in ext]

    programs = []
    for e in prog:
        inst = e['instance'].upper()
        wr = set(writes.get(e['pou'], ()))
        mine = [n for n, _t, ro in var_externals(e['path']) if not ro and n.upper() in wr]
        park = ('Vars::Row::prog_%s_park' % e['name'],
                'Vars::Row::prog_%s_parked' % e['name']) if _opted_in(e, 'park') \
            else ('Vars::VAR_NONE', 'Vars::VAR_NONE')
        programs.append({
            'name': e['name'], 'pou': e['pou'], 'inst': inst,
            'rate': e['rate'].upper(),
            'writes': ''.join(' Vars::Row::%s,' % _home(index, n) for n in mine) or ' 0',
            'park': park[0], 'parked': park[1],
            'health': ('Vars::Row::prog_%s_health' % e['name'])
                      if _opted_in(e, 'health') else 'Vars::VAR_NONE'})

    render.write(os.path.join(site, 'generated', 'plant.cpp'), 'plant.cpp.j2',
                 externals=externals, programs=programs)


def _home(index, n):
    """The table's own spelling of a name an ST file wrote in any case."""
    if n in index: return n
    for k in index:
        if k.lower() == n.lower(): return k
    sys.exit('%s: no table declares it' % n)


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

    # ...and the other direction: a row nothing writes. An output, an alarm or
    # a status row with no author is a law that was meant to be written and was
    # not, and it reads its declared value for the life of the plant with
    # nothing to say so.
    every = set()
    for pou in writes.values(): every |= {x.upper() for x in pou}
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
    for e in prog:
        if not _opted_in(e, 'quality'): continue
        wr = {n.upper() for n in writes.get(e['pou'], ())}
        for n in sorted(wr):
            # An alarm is a verdict, not a reading, and so is anything a program
            # says about ITSELF - its health, its parked answer. A quality on
            # one of those would be a verdict on a verdict.
            if n.endswith('_QLT') or n.startswith('PROG_'): continue
            if n.lower() in {a.lower() for a in alarms}: continue
            if n + '_QLT' not in wr:
                bad.append('%s: writes %s and owes %s_qlt'
                           % (e['name'], n.lower(), n.lower()))

    # A program that opts into HEALTH owes a write to its own row, on every
    # path: the row's declared value is FAULTED, so one nothing writes reads as
    # a program that could not answer - which is worse than not opting in.
    for e in prog:
        if not _opted_in(e, 'health'): continue
        if ('PROG_%s_HEALTH' % e['name']).upper() not in \
                {n.upper() for n in writes.get(e['pou'], ())}:
            bad.append('%s: opts into health and owes prog_%s_health'
                       % (e['name'], e['name']))

    # ...and the same for parking: a program told to park that never answers
    # holds the shutdown open until plc_parkTimeout_sec runs out.
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
    enums(site)
    t = build(site)
    prog = t['prog']
    for e in prog:
        e['path'] = os.path.join(site, e['source'])
        e['pou'] = pou_of(e['path'])
    # IEC identifiers are case-insensitive, so a program INSTANCE that folds
    # onto an enumerated type is one name for two things - and matiec says only
    # "invalid program name", about the RESOURCE, three thousand lines away.
    types = {e['enum'].lower() for e in
             read_table(os.path.join(site, 'tables', 'enums.csv'),
                        AUX['enums'].split())}
    clash = [e['name'] for e in prog if e['instance'].lower() in types]
    if clash:
        sys.exit('%s: the instance name folds onto an enumerated type; '
                 'rename it in programs.csv' % ', '.join(clash))
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
    wi = max(len(e['instance']) for e in prog)
    render.write(os.path.join(site, 'generated', 'plant.st'), 'plant.st.j2',
                 banner=H.replace('//', '(*').rstrip() + ' *)',
                 config_name=os.path.basename(site.rstrip(os.sep)),
                 globals_=sorted(rows.items()),
                 programs=[{'instance': e['instance'], 'pou': e['pou'].lower()}
                           for e in prog])
    pl = os.path.join(site, 'generated', 'plant.st')

    # matiec FIRST: plant.cpp's write rosters are read out of the POUS.c it
    # emits, so the compiler's own answer is what they are built from.
    with tempfile.NamedTemporaryFile('w', suffix='.st', delete=False) as fh:
        corpus = (LIBST + [os.path.join(site, 'generated', 'enums.st')]
                  + st_files(site) + [pl])
        for f in corpus: fh.write(open(f).read() + '\n')
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
    check_laws(t, prog, writes)
    emit_plant_cpp(site, t, prog, writes)
    print('  plant.st       %d globals, %d POUs' % (len(rows), len(st_files(site))))
    return 0


# ---- the ST enumerations the LIBRARY owns -----------------------------------

# (ST type, header the C++ enum is declared in, the enum's own declaration, how
#  a member is spelled in C++, the member pattern). Both are types matiec must be
#  handed rather than allowed to number itself.
LIB_ENUMS = [
    ('QUALITY', 'src/libbotanik/vars/descriptor.h',
     r'enum Quality : uint8_t \{(.*?)\n    \};',
     '::Botanik::Vars::%s', r'^\s*(Q_[A-Z_]+)'),
    ('HEALTH', 'src/libbotanik/plc/health.h',
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
    out = 'src/libbotanik/plc/runtime/%s.gen.h' % st.lower()
    render.write(out, 'lib_enum.gen.h.j2', header=header, st=st,
                 guard='BOTANIK_PLC_RUNTIME_%s_GEN_H' % st,
                 codes=[{'name': c, 'spelt': spell % c} for c in codes])
    return st.lower(), len(codes)


def quality_block():
    """The accessor blocks for every enumerated type the library owns."""
    for e in LIB_ENUMS:
        name, n = _lib_enum(*e)
        print('  %-14s %d codes' % (name + '.gen.h', n))
