#!/usr/bin/env python3
"""Site tables: CSV <-> C++.

    tables.py export     vars.cpp -> tables/*.csv   (bootstrap, run once)
    tables.py check      the two must agree
    tables.py generate   tables/*.csv -> vars.gen.{h,cpp}

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
    'programs':       'name instance source rate',
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
    was, now = {}, {}
    for f, a in parse(os.path.join(site, 'vars.cpp')): was.setdefault(f, []).append(a)
    for f, a in load(site): now.setdefault(f, []).append(a)
    bad = 0
    for f in ROWS:
        a, b = was.get(f, []), now.get(f, [])
        if len(a) != len(b):
            print('  %s: %d cpp, %d csv' % (f, len(a), len(b))); bad += 1; continue
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                print('  %s[%d]\n    cpp: %s\n    csv: %s' % (f, i, x, y)); bad += 1
    print('ROUND TRIP CLEAN' if not bad else 'MISMATCH')
    return bad


H = '// GENERATED by scripts/tables.py from tables/*.csv. Do not edit.\n'
IEC2C = os.environ.get('IEC2C', 'iec2c')
# ORDER MATTERS: matiec needs a block declared before it is instanced, and
# there is no forward declaration. filters before analog, because DC_RAIL holds
# an EWMA.
LIBST = ['src/libbotanik/plc/functions/math.st',
         'src/libbotanik/plc/functions/psychro.st',
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


def prereqs():
    out = {}
    for m in re.finditer(r'#\s*error "BOTANIK_FEATURE_(\w+) requires BOTANIK_FEATURE_(\w+)"',
                         open('src/libbotanik_features.h').read()):
        out.setdefault(m.group(1), []).append(m.group(2))
    return out


def config(site):
    t, dep = toml(os.path.join(site, 'site.toml')), prereqs()
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
         '#ifndef BOTANIK_SITE_CONFIG_GEN_H', '#define BOTANIK_SITE_CONFIG_GEN_H', '']
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


def emit_plant_cpp(site, prog):
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
          'const Program programs[] = {',
          '#define X(name, POU, INST, PERIOD)                                             \\',
          '    { #name, PERIOD, &_init_##INST, &_run_##INST, &_task_##INST },',
          '    PLANT_PROGRAM_LIST(X)', '#undef X', '};', '',
          'const uint8_t PROGRAM_COUNT =',
          '    (uint8_t)(sizeof(programs) / sizeof(programs[0]));', '',
          '}}}  // namespace Botanik::Plc::Runtime', '']
    open(os.path.join(site, 'generated', 'plant.cpp'), 'w').write('\n'.join(L))


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
        for m in re.finditer(r'VAR_EXTERNAL(?:\s+CONSTANT)?(.*?)END_VAR', open(p).read(), re.S):
            for line in m.group(1).split('\n'):
                mm = re.match(r'\s*([A-Za-z_]\w*)\s*:\s*([A-Z]+)\s*;', line)
                if mm:
                    n, t = mm.group(1), mm.group(2)
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
    emit_plant_cpp(site, prog)

    with tempfile.NamedTemporaryFile('w', suffix='.st', delete=False) as fh:
        for f in LIBST + st_files(site) + [pl]: fh.write(open(f).read() + '\n')
        tmp = fh.name
    r = subprocess.run([IEC2C, '-I', 'st/lib', '-T', os.path.join(site, 'generated'), tmp],
                       capture_output=True, text=True)
    os.unlink(tmp)
    err = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l.lower()]
    for l in err[:6]: print('  ' + l)
    print('  plant.st       %d globals, %d POUs' % (len(rows), len(st_files(site))))
    return 1 if err else 0


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
    L.append('const Descriptor vars[] = {')
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
          '    vars,          VARS_COUNT,',
          '};', '',
          '}  // namespace Site', '']
    open(os.path.join(site, 'generated', 'vars.gen.cpp'), 'w').write('\n'.join(L))

    en = t('enums', AUX['enums'])
    G = [H, '// Included from INSIDE namespace Site, so it opens none of its own.', '']
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
          'extern const Descriptor vars[];', 'const uint16_t VARS_COUNT = %d;' % (n_par + n_row), '',
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
          'static_assert(LIBRARY_ROWS + VARS_COUNT <= Config::VAR_MAX,',
          '              "site table exceeds VAR_MAX - raise it in site.toml "',
          '              "(about 36 bytes of RAM per row)");', '',
          '// The whole handoff to the library: this site, as data.',
          'extern const Botanik::SiteDescriptor g_site;', '']
    open(os.path.join(site, 'generated', 'vars.gen.h'), 'w').write('\n'.join(G))
    print('  vars.gen.cpp   %d params, %d rows' % (n_par, n_row))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if   cmd == 'export':   export(SITE)
    elif cmd == 'check':    sys.exit(1 if check(SITE) else 0)
    elif cmd == 'generate': generate(SITE)
    elif cmd == 'plant':    sys.exit(plant(SITE))
    elif cmd == 'config':   config(SITE)
    else:                   sys.exit(__doc__)
