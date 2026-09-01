#!/usr/bin/env python3
"""Site tables: CSV <-> C++.

    tables.py export     vars.cpp -> tables/*.csv   (bootstrap, run once)
    tables.py check      points()'s factory maps vs declare.h
    tables.py generate   tables/*.csv -> vars.gen.{h,cpp}
    tables.py points     tables/*.csv + the ST -> docs/reference/points.md

Offline. Output is committed, never built.
"""

import csv, os, re, sys

SITE = 'src/sites/gh_mlb41'
PAD = 34

ROWS = {  # factory -> (csv stem, columns == the factory's parameters)
    'P':   ('params',      'name type def lo hi digits unit reg desc notes writable retained'),
    'SV':  ('status',      'name type def digits unit reg desc notes retained'),
    'SVE': ('status_enum', 'name def label reg desc notes'),
    'A':   ('alarms',      'name code on_s off_s recent_s hold_s reg desc notes'),
    'MI':  ('modbus_in',   'name type device fc reg regs lo hi def timeout_ms digits unit desc notes poll_ms word_swap'),
    'MO':  ('modbus_out',  'name type device fc reg regs safe digits unit desc notes word_swap'),
    'AI':  ('analog_in',   'name cal device fc reg regs wire def timeout_ms digits unit desc notes poll_ms word_swap'),
    'AO':  ('analog_out',  'name cal device fc reg regs wire safe digits unit desc notes word_swap'),
    'DI':  ('digital_in',  'name device fc reg def timeout_ms invert desc notes poll_ms'),
    'DO':  ('digital_out', 'name device fc reg safe invert desc notes'),
    'RO':  ('relay_out',   'name device coil safe invert desc notes'),
    'PE':  ('params_enum', 'name def label reg desc notes writable retained'),
    'PS':  ('params_text', 'name def cap kind reg desc notes writable retained'),
    'G':   ('globals',     'name type def digits unit desc notes retained'),
    'PI':  ('pulse_in',    'name channel edge desc notes'),
}
TEXT = {'name', 'unit', 'desc', 'notes', 'code', 'tag', 'model_name'}
# Not quoted, but not a number either: a symbol the emitted C++ names.
SYMBOL = {'edge': 'HAL::Pulse::'}

AUX = {  # stem -> columns, for the tables that are not rows
    'analog_cal':     'cal raw_min_row raw_min raw_max_row raw_max eng_min_row eng_min '
                      'eng_max_row eng_max min_row min max_row max',
    'modbus_models':  'symbol kind model_name regs words',
    'modbus_devices': 'addr model tag',
    'enums':          'enum ctype symbol value label desc',
    'programs':       'name instance source rate desc',
    'home':           'label var',
}


def split_args(s):
    out, depth, cur, q, esc = [], 0, '', False, False
    for ch in s:
        if esc:                   cur += ch; esc = False; continue
        if ch == '\\' and q:      cur += ch; esc = True;  continue
        if ch == '"':             q = not q; cur += ch;   continue
        if q:                     cur += ch; continue
        if ch in '([':            depth += 1
        elif ch in ')]':          depth -= 1
        if ch == ',' and not depth: out.append(cur.strip()); cur = ''; continue
        cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


def parse(path):
    src, out = open(path).read(), []
    for m in re.finditer(r'^\s*(' + '|'.join(ROWS) + r')\((.*)$', src, re.M):
        rest, depth, q, esc = m.group(2), 1, False, False
        for i, ch in enumerate(rest):
            if esc:                   esc = False; continue
            if ch == '\\' and q:      esc = True;  continue
            if ch == '"':             q = not q;   continue
            if q:                     continue
            depth += (ch == '(') - (ch == ')')
            if not depth: break
        else:
            sys.exit('unterminated %s(' % m.group(1))
        out.append((m.group(1), split_args(rest[:i])))
    return out


def unquote(a):
    return a[1:-1].replace('\\"', '"') if len(a) > 1 and a[0] == a[-1] == '"' else a


def quote(v, col):
    if col in TEXT:  return '"%s"' % v.replace('"', '\\"')
    if col in SYMBOL and v and not v.startswith(SYMBOL[col]):
        return SYMBOL[col] + v
    return v


def field(v):
    return '"%s"' % v.replace('"', '""') if any(c in v for c in ',"\n') else v


def write_table(path, cols, recs):
    cells = [[field(v) for v in r] for r in recs]
    w = [min(PAD, max([len(c)] + [len(r[i]) for r in cells])) for i, c in enumerate(cols)]
    with open(path, 'w') as fh:
        for r in [cols] + cells:
            fh.write(','.join(c.ljust(w[i]) for i, c in enumerate(r[:-1]))
                     .rstrip() + (',' if len(r) > 1 else '') + r[-1] + '\n')


def read_table(path, cols):
    if not os.path.exists(path): return []
    with open(path, newline='') as fh:
        rd = csv.reader(fh)
        head = [h.strip() for h in next(rd)]
        if head != cols: sys.exit('%s: header %s != %s' % (path, head, cols))
        return [dict(zip(head, [c.strip() for c in r])) for r in rd
                if r and any(c.strip() for c in r)]


def export(site):
    per = {f: [] for f in ROWS}
    for fac, args in parse(os.path.join(site, 'vars.cpp')): per[fac].append(args)
    os.makedirs(os.path.join(site, 'tables'), exist_ok=True)
    for fac, (stem, cols) in ROWS.items():
        cols = cols.split()
        recs = []
        for a in per[fac]:
            v = [unquote(x) for x in a] + [''] * (len(cols) - len(a))
            for x in v:
                if x != x.strip(): sys.exit('%s: %r has edge whitespace' % (stem, x))
            recs.append(v[:len(cols)])
        write_table(os.path.join(site, 'tables', stem + '.csv'), cols, recs)
        print('  %-16s %3d' % (stem + '.csv', len(recs)))
    export_aux(site)


def export_enums(site):
    s = open(os.path.join(site, 'vars.h')).read()
    out = []
    for m in re.finditer(r'enum (\w+) : (\w+) \{(.*?)\n\};', s, re.S):
        name, ctype = m.group(1), m.group(2)
        labels = dict(re.findall(r'case (\d+): return "([^"]+)";',
                                 re.search(r'%s_label.*?\n\}' % name, s, re.S).group(0)))
        for sym, val, desc in re.findall(r'(\w+)\s*=\s*(\d+),\s*///< ([^\n]*)', m.group(3)):
            out.append([name, ctype, sym, val, labels.get(val, ''), desc.strip()])
    return out


def export_aux(site):
    s = open(os.path.join(site, 'vars.cpp')).read()
    got = {
        'analog_cal': [[m.group(1)] + [x for p in re.findall(r'R\("([^"]+)",\s*([^)]+)\)', m.group(2)) for x in p]
                       for m in re.finditer(r'const AnalogCal (CAL_\w+) = \{(.*?)\n\};', s, re.S)],
        'modbus_models': [list(t) for t in re.findall(
            r'const Modbus::DeviceModel (MDL_\w+) =\s*Modbus::(\w+)\("([^"]+)",\s*(\d+),?\s*(\d*)\);', s)],
        'modbus_devices': [list(t) for t in re.findall(r'\{\s*(\d+),\s*&(MDL_\w+),\s*"([^"]+)"\s*\}', s)],
        'enums': export_enums(site),
    }
    for stem, cols in AUX.items():
        write_table(os.path.join(site, 'tables', stem + '.csv'), cols.split(), got[stem])
        print('  %-16s %3d' % (stem + '.csv', len(got[stem])))


def load(site):
    out = []
    for fac, (stem, cols) in ROWS.items():
        cols = cols.split()
        for r in read_table(os.path.join(site, 'tables', stem + '.csv'), cols):
            a = [quote(r.get(c, ''), c) for c in cols]
            while a and a[-1] == '': a.pop()
            out.append((fac, a))
    return out


def check(site):
    """The two hand-kept maps in points() against what declare.h actually says.

    `FAC_SCOPE` and `FAC_TYPE` are a second copy of a fact the C++ factories
    already own - each one hard-codes its own SCOPE_* and T_* - and nothing
    makes them agree. This tree has been bitten by exactly that before, which
    is why BOTANIK_FEATURE_LIST exists: a second hand-kept roster silently
    dropped a flag. Adding a factory, or moving one to another scope, fails
    here rather than quietly mis-filing rows in the point list.
    """
    src = open(DECLARE_H).read()
    bad = 0
    for fac in ROWS:
        # One factory's body only: the next `constexpr Descriptor` ends it, or
        # a greedy match reads the SCOPE of whichever factory comes after.
        body = re.search(r'constexpr Descriptor %s\((.*?)(?=constexpr Descriptor|\Z)'
                         % fac, src, re.S)
        if body is None:
            print('  %-3s: no factory in %s' % (fac, DECLARE_H)); bad += 1; continue
        # A factory that takes a VarType builds rows of whatever the table says,
        # so it pins a ROLE and a SCOPE but not a type.
        m = re.search(r'\b(T_[A-Z0-9]+|type), ROLE_[A-Z]+, SCOPE_([A-Z]+)',
                      body.group(1))
        if m is None:
            print('  %-3s: no SCOPE in its Descriptor{}' % fac); bad += 1; continue
        typ, scope = m.group(1), m.group(2)
        if FAC_SCOPE.get(fac) != scope:
            print('  %-3s: declare.h says SCOPE_%s, points() says %s'
                  % (fac, scope, FAC_SCOPE.get(fac))); bad += 1
        # Only a factory that PINS its type is checked: P/SV/MI/MO/G take one.
        want = FAC_TYPE.get(fac)
        if want is not None and typ != 'type' and want != typ:
            print('  %-3s: declare.h builds %s, points() says %s'
                  % (fac, typ, want)); bad += 1
    print('FACTORIES AGREE' if not bad else 'MISMATCH')
    return bad



H = '// GENERATED by scripts/tables.py from tables/*.csv. Do not edit.\n'
IEC2C = os.environ.get('IEC2C', 'iec2c')
# ORDER MATTERS: matiec needs a block declared before it is instanced, and
# there is no forward declaration. filters before analog, because DC_RAIL holds
# an EWMA.
LIBST = ['src/libbotanik/plc/functions/math.st',
         'src/libbotanik/plc/functions/psychro.st',
         'src/libbotanik/plc/functions/time.st',
         'src/libbotanik/plc/blocks/timers.st',
         'src/libbotanik/plc/blocks/filters.st',
         'src/libbotanik/plc/blocks/analog.st',
         'src/libbotanik/plc/blocks/pid.st',
         'src/libbotanik/plc/blocks/actuator.st',
         'src/libbotanik/plc/blocks/meter.st']


def st_files(site):
    out = []
    for base in (os.path.join(site, 'blocks'), os.path.join(site, 'programs')):
        for r, _, fs in os.walk(base):
            out += [os.path.join(r, f) for f in fs if f.endswith('.st')]
    return sorted(out)


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


def prereqs():
    out = {}
    for m in re.finditer(r'#\s*error "BOTANIK_FEATURE_(\w+) requires BOTANIK_FEATURE_(\w+)"',
                         open('src/libbotanik_features.h').read()):
        out.setdefault(m.group(1), []).append(m.group(2))
    return out


def config(site):
    t, dep = toml(os.path.join(site, 'site.toml')), prereqs()
    # Named for the SITE: "BOTANIK_SITE_*" is the same symbol in every site's
    # generated header, so two of them in one translation unit would silently
    # skip the second.
    guard = site_guard(t)
    val = [('SITE_NAME', '"%s"' % t['site']['name']),
           ('SITE_CONTROL_MS', t['rates']['control']),
           ('SITE_SENSE_MS', t['rates']['sense']),
           ('VAR_MAX', t['limits']['var_max']),
           ('QUALITY_MIRROR_MAX', t['limits']['quality_mirror_max']),
           ('MODBUS_TCP_MAX_ROWS', t['limits']['modbus_tcp_max_rows']),
           ('STR_POOL_BYTES', t['limits']['str_pool_bytes']),
           ('MODBUS_BAUD', t['fieldbus']['baud'])]
    if t.get('build', {}).get('pin_sizes'): val.append(('PIN_SIZES', 1))

    L = ['// GENERATED by scripts/tables.py from site.toml. Do not edit.', '',
         '#ifndef %s_CONFIG_GEN_H' % guard, '#define %s_CONFIG_GEN_H' % guard, '']
    for k, v in val:
        L += ['#ifndef BOTANIK_%s' % k, '#  define BOTANIK_%s %s' % (k, v), '#endif']
    L.append('')
    L += ['// Every flag this site turns on, each under the prerequisites the LIBRARY',
          '// declares. The guard is "unless explicitly turned off", so precedence',
          '// stays command line > site > header default.']
    on = [f for f, v in t['features'].items() if v]
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
          '// This site turns every optional feature ON, and its table reserves rows',
          '// on that basis. Built with one OFF, the table declares rows the library',
          '// never registers, which misaligns every register after them - so refuse',
          '// rather than drift. Here rather than in a header the site hand-writes,',
          '// because the flags have their defaults by the time this is read again.',
          '#define BOTANIK_SITE_ASSERT_FEATURES \\']
    L += ['    BOTANIK_FEATURE_%s && \\' % f for f in on]
    L += ['    1', '']
    L += ['', '#endif', '']
    open(os.path.join(site, 'generated', 'config.gen.h'), 'w').write('\n'.join(L))
    print('  config.gen.h   %d values, %d features' % (len(val), len(on)))


def emit_plant_cpp(site, prog, writes):
    w = max(len(e['pou']) for e in prog)
    wi = max(len(e['instance'].upper()) for e in prog)
    wn = max(len(e['name']) for e in prog)
    L = ['/**', ' * @file plant.cpp',
         ' * @brief GENERATED by scripts/tables.py from tables/programs.csv.',
         ' *',
         ' * The only translation unit that compiles matiec\'s resource, because the',
         ' * generated bodies are static in it. That is why the trampolines are here:',
         ' * a table of function pointers needs one wrapper per POU.',
         ' */', '',
         '#include "libbotanik/plc/runtime/program.h"',
         '#include "libbotanik/plc/runtime/runtime.h"',
         '#include "libbotanik/plc/runtime/bank.h"',
         '#include "libbotanik/plc/runtime/resolve.h"', '',
         '#pragma GCC diagnostic push',
         '#pragma GCC diagnostic ignored "-Wunused-parameter"',
         '#pragma GCC diagnostic ignored "-Wunused-function"',
         '#pragma GCC diagnostic ignored "-Wunused-but-set-variable"',
         '#pragma GCC diagnostic ignored "-Wshadow"',
         '#include "plc.c"',
         '#pragma GCC diagnostic pop', '',
         '/* Named by every generated read, write and resolve. */',
         'namespace Botanik { namespace Plc { namespace Runtime { Vars::Bank* io = 0; } } }',
         '', 'namespace Botanik { namespace Plc { namespace Runtime {', '',
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
    # Checked HERE because nothing downstream can: an ST row resolves
    # VAR_IN_OUT, so the bank's writer claim records no owner and has caught
    # nothing since the migration.
    #
    # A row DECLARED writable and never written is a mis-declaration and fails
    # the build - it belongs in VAR_EXTERNAL CONSTANT, and leaving it here puts
    # a row the program only reads on its safe-state roster. Two programs that
    # both WRITE one row is different: read-modify-written shared state is the
    # design (the irrigation force flags), so it is reported, not refused.
    ghosts, author, shared = [], {}, []
    for e in prog:
        wr = set(writes.get(e['pou'], ()))
        for n, _t, ro in var_externals(e['path']):
            if ro: continue
            if n.upper() not in wr: ghosts.append((e['name'], n)); continue
            if n in author: shared.append((n, author[n], e['name']))
            else: author[n] = e['name']
    if ghosts:
        for who, n in ghosts:
            print('  %s: declares %s writable and never writes it' % (who, n),
                  file=sys.stderr)
        sys.exit('%d row(s) declared writable but never written; '
                 'move them to VAR_EXTERNAL CONSTANT' % len(ghosts))
    if shared:
        print('  %d row(s) with two authors (read-modify-write): %s'
              % (len(shared), ', '.join(n for n, _a, _b in shared)))

    for e in prog:
        wr = set(writes.get(e['pou'], ()))
        wr = [n for n, _t, ro in var_externals(e['path']) if not ro and n.upper() in wr]
        L.append('static const char* const _writes_%s[] = {%s};'
                 % (e['instance'].upper(),
                    ''.join(' "%s",' % n for n in wr) or ' 0'))
    L += ['',
          'const Program programs[] = {',
          '#define X(name, POU, INST, PERIOD)                                             \\',
          '    { #name, PERIOD, &_init_##INST, &_run_##INST, &_task_##INST,               \\',
          '      _writes_##INST,                                                          \\',
          '      (uint8_t)(sizeof _writes_##INST / sizeof _writes_##INST[0]) },',
          '    PLANT_PROGRAM_LIST(X)', '#undef X', '};', '',
          'const uint8_t PROGRAM_COUNT =',
          '    (uint8_t)(sizeof(programs) / sizeof(programs[0]));', '',
          '}}}  // namespace Botanik::Plc::Runtime', '']
    open(os.path.join(site, 'generated', 'plant.cpp'), 'w').write('\n'.join(L))


def var_externals(path):
    """Every row one ST file names, as (name, type, read_only). `VAR_EXTERNAL
    CONSTANT` is read-only and plain `VAR_EXTERNAL` is written - that IS the
    site's in/out declaration, there is no table saying so."""
    out = []
    # Comments FIRST: roof_lockout opens with a block explaining why it uses
    # VAR_EXTERNAL, and matching that swallowed the real CONSTANT block behind
    # it - every row in it came back writable. Anchored to a line start too,
    # for the same reason.
    src = re.sub(r'\(\*.*?\*\)', ' ', open(path).read(), flags=re.S)
    for m in re.finditer(r'^VAR_EXTERNAL(\s+CONSTANT)?$(.*?)^END_VAR',
                         src, re.S | re.M):
        for line in m.group(2).split('\n'):
            mm = re.match(r'\s*([A-Za-z_]\w*)\s*:\s*([A-Z]+)\s*;', line)
            if mm: out.append((mm.group(1), mm.group(2), bool(m.group(1))))
    return out


def writes_by_pou(site):
    """What each PROGRAM writes, off matiec's own generated C.

    A write is `__SET_EXTERNAL` and a read is `__GET_EXTERNAL` - our macros,
    emitted from matiec's parse. So this is the COMPILER's answer, not a second
    reading of the ST: no comments to skip, and a call's named arguments cannot
    look like an assignment. Only PROGRAM bodies write externals; a function
    block has no VAR_EXTERNAL to write.

    Names come back matiec-folded (upper); the caller maps them home.
    """
    src = open(os.path.join(site, 'generated', 'POUS.c')).read()
    out = {}
    for m in re.finditer(r'^static void (\w+)_body__\(.*?^\}', src, re.M | re.S):
        w = re.findall(r'__SET_EXTERNAL\(\w+->,\s*(\w+)\s*,', m.group(0))
        if w: out[m.group(1)] = w
    return out


def pou_of(path):
    m = re.search(r'^PROGRAM (\w+)', open(path).read(), re.M)
    return m.group(1).upper() if m else sys.exit('%s: no PROGRAM' % path)


def plant(site):
    """plant.st and plant.cpp from programs.csv, then matiec."""
    import subprocess, tempfile
    prog = read_table(os.path.join(site, 'tables', 'programs.csv'),
                      AUX['programs'].split())
    for e in prog:
        e['path'] = os.path.join(site, e['source'])
        e['pou'] = pou_of(e['path'])
    rows = {}
    for p in st_files(site):
        for n, t, _ in var_externals(p):
            if rows.get(n, t) != t: sys.exit('type clash on %s' % n)
            rows[n] = t
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
         '   entry: its health row, its cost and missed counts, its writer slot,',
         '   and its own INVALID when a name it binds resolves to nothing.',
         '',
         '   The TASK below is a formality - matiec refuses a RESOURCE without a',
         '   periodic one. Nothing binds to it and nothing reads its interval. *)',
         '', 'CONFIGURATION gh_mlb41', '', '  VAR_GLOBAL']
    L += ['    %-*s : %s;' % (w, n, t) for n, t in sorted(rows.items())]
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
    r = subprocess.run([IEC2C, '-I', 'st/lib', '-T', os.path.join(site, 'generated'), tmp],
                       capture_output=True, text=True)
    os.unlink(tmp)
    err = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l.lower()]
    for l in err[:6]: print('  ' + l)
    if err: return 1

    emit_plant_cpp(site, prog, writes_by_pou(site))
    print('  plant.st       %d globals, %d POUs' % (len(rows), len(st_files(site))))
    return 0


def emit_rows(fac, cols, recs):
    out = []
    for r in recs:
        a = [quote(r.get(c, ''), c) for c in cols]
        while a and a[-1] == '': a.pop()
        out.append('    %s(%s),' % (fac, ', '.join(a)))
    return out


def generate(site):
    t = lambda stem, cols: read_table(os.path.join(site, 'tables', stem + '.csv'), cols.split())
    cal  = t('analog_cal', AUX['analog_cal'])
    home = t('home', AUX['home'])
    tom  = toml(os.path.join(site, 'site.toml'))
    mdl, dev = t('modbus_models', AUX['modbus_models']), t('modbus_devices', AUX['modbus_devices'])
    per = {f: t(stem, cols) for f, (stem, cols) in ROWS.items()}
    n_par = sum(len(per[f]) for f in ('P', 'PE', 'PS'))
    n_row = sum(len(per[f]) for f in ROWS if f not in ('P', 'PE', 'PS'))

    L = [H, '', '#include "libbotanik.h"', '', 'namespace Site {', '',
         'using namespace Botanik;', 'using namespace Botanik::Vars;', '',
         '// Inside the namespace and after the usings: the header names every',
         '// type unqualified, because it is only ever included here and by site.h.',
         '#include "vars.gen.h"', '']
    for c in cal:
        L.append('const AnalogCal %s = {' % c['cal'])
        for k in ('raw_min', 'raw_max', 'eng_min', 'eng_max', 'min', 'max'):
            L.append('    R("%s", %s),' % (c[k + '_row'], c[k]))
        L += ['};', '']
    L.append('const AnalogCal* const g_analog_cals[] = {')
    L += ['    &%s,' % c['cal'] for c in cal] + ['};', '']
    # ONE table, parameters FIRST: the ROLE_PARAM run has to abut the
    # library's, and emitting the two runs in order is the whole of that rule.
    L.append('const Descriptor rows[] = {')
    L.append('    // parameters, first and contiguous')
    for f in ('P', 'PE', 'PS'):
        L += emit_rows(f, ROWS[f][1].split(), per[f])
    L.append('    // ...and everything else')
    for f in ('A', 'SV', 'SVE', 'G', 'MI', 'MO', 'AI', 'AO', 'DI', 'PI', 'DO', 'RO'):
        L += emit_rows(f, ROWS[f][1].split(), per[f])
    L += ['};', '']
    for m in mdl:
        args = '"%s", %s' % (m['model_name'], m['regs']) + (', %s' % m['words'] if m['words'] else '')
        L += ['const Modbus::DeviceModel %s = Modbus::%s(%s);' % (m['symbol'], m['kind'], args)]
    L += ['', 'const Modbus::Device g_devices[] = {']
    L += ['    { %s, &%s, "%s" },' % (d['addr'], d['model'], d['tag']) for d in dev]
    L += ['};', '', 'const Modbus::DeviceTable g_device_table = {',
          '    g_devices, (uint8_t)(sizeof(g_devices) / sizeof(g_devices[0]))', '};', '',
          '// Home-screen readings: label + variable. A caption is a person\'s',
          '// choice and nothing in a row\'s name says it belongs on the landing',
          '// page, so it is the one table no rule derives.',
          'const HomeVar g_home_fields[] = {']
    L += ['    { "%s", "%s" },' % (h['label'], h['var']) for h in home]
    L += ['};', '',
          '// Filled at bring-up; extern in the header, or every TU including it',
          '// would carry a definition.',
          'VarRef home_refs[g_home_field_count] = {};', '',
          '/** @brief The site\'s shell commands: none. A site table is matched',
          ' *  BEFORE the library built-ins, so a name reused there shadows one. */',
          'const Botanik::Shell::ShellCommand* const g_commands = nullptr;',
          'const uint8_t g_command_count = 0;', '',
          '// Field order matches Botanik::SiteDescriptor; any table may be null.',
          '// Alarms are not here (a SCOPE_A row) and neither are the status groups',
          '// (a variable\'s group is its name prefix).',
          'const Botanik::SiteDescriptor g_site = {',
          '    Botanik::Config::SITE_NAME,',
          '    g_commands,    g_command_count,',
          '    g_home_fields, g_home_field_count, home_refs,',
          '    &g_device_table,',
          '    rows,          ROWS_COUNT,',
          '};', '',
          '}  // namespace Site', '']
    open(os.path.join(site, 'generated', 'vars.gen.cpp'), 'w').write('\n'.join(L))

    en = t('enums', AUX['enums'])
    guard = site_guard(tom)
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
    G += ['// The build this table was cut for. @see BOTANIK_SITE_ASSERT_FEATURES',
          '#if !(BOTANIK_SITE_ASSERT_FEATURES)',
          '#  error "this site assumes every optional feature it declares is enabled"',
          '#endif', '']
    G += ['extern const AnalogCal %s;' % c['cal'] for c in cal]
    G += ['', 'extern const AnalogCal* const g_analog_cals[];',
          'const uint8_t g_analog_cal_count = %d;' % len(cal), '',
          '// ONE table, parameters first. Registering it is one call, and the',
          '// ROLE_PARAM run stays contiguous because the emit order is the rule.',
          'extern const Descriptor rows[];', 'const uint16_t ROWS_COUNT = %d;' % (n_par + n_row), '',
          ]
    G += ['extern const Modbus::DeviceModel %s;' % m['symbol'] for m in mdl]
    G += ['extern const Modbus::Device g_devices[];',
          'extern const Modbus::DeviceTable g_device_table;', '',
          '// The one table no rule derives: a caption is a person\'s choice, and',
          '// nothing in a row\'s name says it belongs on the landing page.',
          'extern const HomeVar g_home_fields[];',
          'const uint8_t g_home_field_count = %d;' % len(home),
          '// The home screen silently ignores fields past its columns.',
          'static_assert(g_home_field_count <= Config::HOME_FIELDS_MAX,',
          '              "too many home fields: the HomePage would drop the tail");', '',
          '// Where those names resolve to, filled at bring-up. VAR_NONE until',
          '// then, which reads as the inert sink rather than as row 0.',
          'extern VarRef home_refs[g_home_field_count];', '',
          '// Rows the LIBRARY registers ahead of these, from site.toml. One row',
          '// past VAR_MAX and registration is REFUSED: every name in it resolves',
          '// to the sink for the life of the run, and only refused_count() sees it.',
          'const uint16_t LIBRARY_ROWS = %d;' % tom['limits']['library_rows'],
          'static_assert(LIBRARY_ROWS + ROWS_COUNT <= Config::VAR_MAX,',
          '              "site table exceeds VAR_MAX - raise it in site.toml "',
          '              "(about 36 bytes of RAM per row)");', '',
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
    print('  vars.gen.cpp   %d params, %d rows' % (n_par, n_row))


# ---- points.md -------------------------------------------------------------

LIB_TABLE  = 'src/libbotanik/vars/vars_table.h'
DECLARE_H  = 'src/libbotanik/vars/declare.h'

# BY SCOPE, in the order Vars::VarScope declares them - which is also the order
# a reader wants: what an operator sets, what alarms, what the plant computes,
# then the wiring. One factory can only ever land in one scope.
FAC_SCOPE = {
    'P': 'P', 'PE': 'P', 'PS': 'P',
    'A': 'A', 'G': 'G', 'SV': 'SV', 'SVE': 'SV',
    'MI': 'MI', 'AI': 'AI', 'DI': 'DI', 'PI': 'DI',
    'MO': 'MO', 'AO': 'AO', 'DO': 'DO', 'RO': 'RO',
}
SCOPES = [
    ('A',  'Alarms',          'the boolean an AlarmCondition reads',
     'name code on_s off_s recent_s hold_s reg desc notes'),
    ('P',  'Parameters',      'persisted, editable',
     'name type def accepts unit access reg desc notes'),
    ('G',  'Computed values', 'HMI only, not published',
     'name type def digits unit desc notes'),
    ('SV', 'Status values',   'published: MQTT and the Modbus TCP slave',
     'name type def digits unit reg desc notes'),
    ('MI', 'Direct inputs',   'register 1:1 (device 0 = local)',
     'name type device fc reg regs unit desc notes'),
    ('AI', 'Analog inputs',   'raw count through scale/offset',
     'name cal device fc reg regs unit desc notes'),
    ('DI', 'Digital inputs',  'one bit, polled - or an edge COUNT, which is a PI row',
     'name type device fc reg desc notes'),
    ('MO', 'Direct outputs',  'register 1:1',
     'name type device fc reg safe desc notes'),
    ('DO', 'Digital outputs', 'one bit',
     'name type device fc reg safe invert desc notes'),
    ('AO', 'Analog outputs',  'engineering value through scale/offset',
     'name cal device fc reg safe desc notes'),
    ('RO', 'Relay outputs',   'a coil',
     'name device coil safe invert desc notes'),
]

# What a factory's rows ARE, where the CSV carries no `type` column of its own.
FAC_TYPE = {'A': 'T_BOOL', 'PE': 'T_ENUM', 'PS': 'T_STR', 'SVE': 'T_ENUM',
            'DI': 'T_BOOL', 'DO': 'T_BOOL', 'RO': 'T_BOOL',
            'AI': 'T_F32', 'AO': 'T_F32', 'PI': 'T_U32'}


def _cell(v):
    """One table cell: markdown-safe, and '-' rather than an empty column."""
    v = (v or '').strip().strip('"')
    return v.replace('|', '\\|').replace('\n', ' ') if v else '-'


def _point_row(fac, r, want):
    lo, hi = r.get('lo', ''), r.get('hi', '')
    out = []
    for c in want.split():
        if   c == 'accepts': out.append('%s .. %s' % (_cell(lo), _cell(hi))
                                        if lo or hi else '-')
        elif c == 'access':  out.append('RW' if r.get('writable') == 'true' else 'RO')
        elif c == 'type':    out.append(_cell(r.get('type') or FAC_TYPE.get(fac, '')))
        else:                out.append(_cell(r.get(c)))
    return out


# The CSV's keys are terse where a reference table wants words.
HEADINGS = {'def': 'default', 'desc': 'description', 'cal': 'calibration',
            'on_s': 'on (s)', 'off_s': 'off (s)', 'recent_s': 'recent (s)',
            'hold_s': 'hold (s)', 'regs': 'registers'}


def _table(rows, want):
    head = [HEADINGS.get(c, c) for c in want.split()]
    return (['| ' + ' | '.join(head) + ' |', '|' + '---|' * len(head)]
            + ['| ' + ' | '.join(r) + ' |' for r in rows])


def _lib_rows():
    """The library's own rows, off the one file that declares them. PARSED, not
    counted: it is C++, but it is the same factory calls the CSVs become."""
    out = {}
    for fac, args in parse(LIB_TABLE):
        r = dict(zip(ROWS[fac][1].split(), [unquote(a) for a in args]))
        out.setdefault(fac, []).append(r)
    return out


def _st_interface(path):
    """What one law reads and writes, off its VAR_EXTERNAL blocks."""
    ext = var_externals(path)
    return ([n for n, _, ro in ext if ro], [n for n, _, ro in ext if not ro])


def points(site):
    """docs/reference/points.md, from the tables and the ST."""
    tom  = toml(os.path.join(site, 'site.toml'))
    name = tom['site']['name']
    per  = {f: read_table(os.path.join(site, 'tables', ROWS[f][0] + '.csv'),
                          ROWS[f][1].split()) for f in ROWS}
    lib  = _lib_rows()

    def of(src, sc):
        return [(f, r) for f in ROWS if FAC_SCOPE.get(f) == sc
                for r in src.get(f, [])]

    # Grouped ONCE: the summary below counts what the sections then render.
    group = {sc: (of(per, sc), of(lib, sc)) for sc, _, _, _ in SCOPES}
    n_site = sum(len(v) for v in per.values())
    n_lib  = sum(len(v) for v in lib.values())
    L = ['# %s: variables and programs' % name, '',
         'GENERATED by `scripts/tables.py points`. Do not edit.', '',
         'Every row the firmware exposes, what it is for, and where an integrator',
         'finds it. The site half comes from `tables/*.csv`, the library half from',
         '`%s`, and the programs from the ST itself.' % LIB_TABLE, '',
         'One section per SCOPE, in the order `Vars::VarScope` declares them.', '',
         '%d rows: %d declared by the site, %d by the library.'
         % (n_site + n_lib, n_site, n_lib), '',
         '| scope | what it is | site | library |', '|---|---|---|---|']
    for sc, title, blurb, _ in SCOPES:
        L.append('| %s | %s | %d | %d |'
                 % (title, blurb, len(group[sc][0]), len(group[sc][1])))
    L.append('')

    for sc, title, blurb, want in SCOPES:
        rows, libr = group[sc]
        if not rows and not libr: continue
        L += ['## %s' % title, '', '`SCOPE_%s`: %s.' % (sc, blurb), '']
        if rows:
            L += ['### %s (%d)' % (name, len(rows)), '']
            L += _table([_point_row(f, r, want) for f, r in rows], want) + ['']
        if libr:
            L += ['### Library (%d)' % len(libr), '']
            L += _table([_point_row(f, r, want) for f, r in libr], want) + ['']

    cal = read_table(os.path.join(site, 'tables', 'analog_cal.csv'),
                     AUX['analog_cal'].split())
    if cal:
        L += ['## Analog scaling', '',
              'A raw reading maps to engineering units through four parameters.',
              '`scale` and `offset` are DERIVED rather than stored, so a',
              'recalibration lands the moment the parameter commits.', '']
        L += _table([[_cell(c[k]) for k in ('cal', 'raw_min_row', 'raw_min',
                                            'raw_max_row', 'raw_max',
                                            'eng_min_row', 'eng_min',
                                            'eng_max_row', 'eng_max')] for c in cal],
                    'block raw_min_row raw_min raw_max_row raw_max '
                    'eng_min_row eng_min eng_max_row eng_max') + ['']

    prog = read_table(os.path.join(site, 'tables', 'programs.csv'),
                      AUX['programs'].split())
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
        path = os.path.join(site, e['source'])
        iface[e['name']] = _st_interface(path)
        L.append('| %d | %s | %s | %d | %d | %s |'
                 % (i, e['name'], e['rate'], len(iface[e['name']][0]),
                    len(iface[e['name']][1]), _cell(e['desc'])))
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
    print('  points.md      %d rows, %d programs' % (n_site + n_lib, len(prog)))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if   cmd == 'export':   export(SITE)
    elif cmd == 'check':    sys.exit(1 if check(SITE) else 0)
    elif cmd == 'generate': generate(SITE)
    elif cmd == 'plant':    sys.exit(plant(SITE))
    elif cmd == 'config':   config(SITE)
    elif cmd == 'points':   points(SITE)
    else:                   sys.exit(__doc__)
