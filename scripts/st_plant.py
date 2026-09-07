#!/usr/bin/env python3
"""The ST half: the site's enumerated types, the resource matiec compiles, and
the roster `Plc::Runtime::Plant` runs.

Everything here needs the compiler's own answer, what each body writes, so the
checks that belong to a law live here rather than beside the table's."""

import os, re, sys

from site_tables import (AUX, H, KIND, _opted_in, build, read_table, site_guard,
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

    L = [H.replace('//', '(*').rstrip() + ' *)', '',
         "(* The site's enumerated codes, so a law compares against the WORD.",
         '   matiec numbers these in declaration order and the C++ side is the',
         '   SAME type, so the order IS the wire contract: a member may be',
         '   appended, never moved. *)', '', 'TYPE', '']
    for n, mem in groups:
        w = max(len(e['symbol']) for e in mem)
        L.append('  %s : (' % n)
        for i, e in enumerate(mem):
            L.append('    %-*s%s%s' % (w, e['symbol'],
                                       ',' if i + 1 < len(mem) else '',
                                       '   (* %s *)' % e['desc'] if e['desc'] else ''))
        L += ['  );', '']
    L += ['END_TYPE', '']
    open(os.path.join(site, 'generated', 'enums.st'), 'w').write('\n'.join(L))

    guard = site_guard(toml(os.path.join(site, 'site.toml')))
    G = [H, '/**', ' * @file enums.gen.h',
         " * @brief The site's enumerated codes: the C++ enum, the word for each",
         ' *        code, and the ST binding.',
         ' *',
         ' * Included OUTSIDE namespace Site (it opens its own), and before the',
         ' * generated POUs: the `BOTANIK_ST_TYPE_*` blocks below are what',
         " * `__DECLARE_ENUMERATED_TYPE` expands to, so matiec's own member list",
         ' * is discarded and ST compares against these constants.',
         ' */', '',
         '#ifndef %s_ENUMS_GEN_H' % guard, '#define %s_ENUMS_GEN_H' % guard, '',
         '#include <cstdint>', '',
         '#include "libbotanik/vars/model.h"', '',
         'namespace Site {', '']
    for n, mem in groups:
        w = max(len(e['symbol']) for e in mem)
        G.append('enum %s : %s {' % (n, mem[0]['ctype']))
        G += ['    %-*s = %s,%s' % (w, e['symbol'], e['value'],
                                    '   ///< ' + e['desc'] if e['desc'] else '')
              for e in mem]
        G += ['};', '',
              '/** @brief A %s as the word an operator reads and types.' % n,
              ' *  Null for an unmapped code, which is what refuses it. */',
              'inline const char* %s_label(uint64_t code) {' % n,
              '    switch (code) {']
        G += ['        case %s: return "%s";' % (e['value'], e['label']) for e in mem]
        G += ['        default: return nullptr;', '    }', '}', '']
    G += ['}  // namespace Site', '',
          '// matiec upper-cases every identifier, so this is the spelling its',
          '// generated C uses for the type and for each member.']
    for n, mem in groups:
        u = n.upper()
        w = max(len(e['symbol']) for e in mem)
        G += ['', 'typedef ::Site::%s %s;' % (n, u),
              '#define BOTANIK_ST_TYPE_%s \\' % u]
        G += ['    static const %s %s__%-*s = ::Site::%-*s;%s'
              % (u, u, w, e['symbol'], w, e['symbol'],
                 ' \\' if i + 1 < len(mem) else '')
              for i, e in enumerate(mem)]
    G += ['', '// ...and how a Ref of one reaches the row, which is a T_ENUM: the',
          '// code is what sits in storage and in a master\'s register.',
          'namespace Botanik { namespace Vars {', '']
    for n, _mem in groups:
        G += ['    template <> struct RefIo< ::Site::%s > {' % n,
              '        static ::Site::%s read(Var h) { return (::Site::%s)h.as<uint8_t>(); }' % (n, n),
              '        static ::Site::%s of(Value v) { return (::Site::%s)v.as<uint8_t>(); }' % (n, n),
              '        static void write(Var h, ::Site::%s v) { h.set_i((int64_t)v); }' % n,
              '        static SetResult edit(Var h, ::Site::%s v) {' % n,
              '            return h.param_set(from_i64(h.type(), (int64_t)v));',
              '        }',
              '    };', '']
    G += ['}}  // namespace Botanik::Vars', '',
          '#endif  // %s_ENUMS_GEN_H' % guard, '']
    open(os.path.join(site, 'generated', 'enums.gen.h'), 'w').write('\n'.join(G))
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
         '/* Before the POUs: the site\'s enumerated types, which matiec\'s',
         ' * generated C names and whose member list it leaves to us. */',
         '#include "enums.gen.h"', '',
         '/* Every row an ST body names, as matiec spells it. `accessor.h` binds',
         ' * each external to Ref<type>{ ROW_<NAME> }, so a name no table declares',
         ' * fails to compile rather than resolving to the sink at bring-up. */',
         'enum {']
    ext = sorted({n for e in prog for n, _t, _ro in var_externals(e['path'])})
    we = max(len(n) for n in ext)
    for n in ext:
        row = _home(index, n)
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
          '// The health row of a program that opted in, or VAR_NONE. The LAW',
          '// writes it; Plant only reads it back for the TASKS page.']
    for e in prog:
        L.append('static const uint16_t _health_%s = %s;'
                 % (e['instance'].upper(),
                    ('Vars::Row::prog_%s_health' % e['name'])
                    if _opted_in(e, 'health') else 'Vars::VAR_NONE'))
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
    L = [H.replace('//', '(*').rstrip() + ' *)', '',
         '(* The enumerated codes a law compares against are enums.st, which is',
         '   generated beside this and compiled ahead of it. *)', '',
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
    guard = 'BOTANIK_PLC_RUNTIME_%s_GEN_H' % st
    w = max(len(c) for c in codes)
    L = ['/* GENERATED by scripts/tables.py from %s. Do not edit. */' % header,
         '#ifndef %s' % guard, '#define %s' % guard, '',
         '/* matiec discards its own member list for this type: the ST enumeration',
         ' * and the C++ one must be the SAME type, not two with equal values. */',
         '#define BOTANIK_ST_TYPE_%s \\' % st]
    for i, c in enumerate(codes):
        L.append('    static const %s %s__%-*s = %s;%s'
                 % (st, st, w, c, spell % c, ' \\' if i + 1 < len(codes) else ''))
    L += ['', '#endif', '']
    out = 'src/libbotanik/plc/runtime/%s.gen.h' % st.lower()
    open(out, 'w').write('\n'.join(L))
    return st.lower(), len(codes)


def quality_block():
    """The accessor blocks for every enumerated type the library owns."""
    for e in LIB_ENUMS:
        name, n = _lib_enum(*e)
        print('  %-14s %d codes' % (name + '.gen.h', n))
