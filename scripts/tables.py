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
    'DI':  ('digital_in',  'name device fc reg def timeout_ms invert desc notes poll_ms pulse'),
    'DO':  ('digital_out', 'name device fc reg safe invert desc notes'),
    'RO':  ('relay_out',   'name device coil safe invert desc notes'),
    'PE':  ('params_enum', 'name def label reg desc notes writable retained'),
    'PS':  ('params_text', 'name def cap kind reg desc notes writable retained'),
    'G':   ('globals',     'name type def digits unit desc notes retained'),
}
TEXT = {'name', 'unit', 'desc', 'notes', 'code', 'tag', 'model_name'}

AUX = {  # stem -> columns, for the tables that are not rows
    'analog_cal':     'cal raw_min_row raw_min raw_max_row raw_max eng_min_row eng_min '
                      'eng_max_row eng_max min_row min max_row max',
    'pulse_inputs':   'name channel edge',
    'modbus_models':  'symbol kind model_name regs words',
    'modbus_devices': 'addr model tag',
    'enums':          'enum ctype symbol value label desc',
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
    return '"%s"' % v.replace('"', '\\"') if col in TEXT else v


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
        'pulse_inputs': [list(t) for t in re.findall(r'\{\s*"([^"]+)",\s*(\d+),\s*HAL::Pulse::(\w+)\s*\}', s)],
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


def emit_rows(fac, cols, recs):
    out = []
    for r in recs:
        a = [quote(r.get(c, ''), c) for c in cols]
        while a and a[-1] == '': a.pop()
        out.append('    %s(%s),' % (fac, ', '.join(a)))
    return out


def generate(site):
    t = lambda stem, cols: read_table(os.path.join(site, 'tables', stem + '.csv'), cols.split())
    cal, pul = t('analog_cal', AUX['analog_cal']), t('pulse_inputs', AUX['pulse_inputs'])
    mdl, dev = t('modbus_models', AUX['modbus_models']), t('modbus_devices', AUX['modbus_devices'])
    per = {f: t(stem, cols) for f, (stem, cols) in ROWS.items()}
    n_par = sum(len(per[f]) for f in ('P', 'PE', 'PS'))
    n_row = sum(len(per[f]) for f in ROWS if f not in ('P', 'PE', 'PS'))

    L = [H, '', '#include "vars.h"', '', 'namespace Site {', '']
    for c in cal:
        L.append('const AnalogCal %s = {' % c['cal'])
        for k in ('raw_min', 'raw_max', 'eng_min', 'eng_max', 'min', 'max'):
            L.append('    R("%s", %s),' % (c[k + '_row'], c[k]))
        L += ['};', '']
    L.append('const AnalogCal* const g_analog_cals[] = {')
    L += ['    &%s,' % c['cal'] for c in cal] + ['};', '']
    L.append('const Descriptor params[] = {')
    for f in ('P', 'PE', 'PS'):
        L += emit_rows(f, ROWS[f][1].split(), per[f])
    L += ['};', '']
    L.append('const Descriptor rows[] = {')
    for f in ('A', 'SV', 'SVE', 'G', 'MI', 'MO', 'AI', 'AO', 'DI', 'DO', 'RO'):
        L += emit_rows(f, ROWS[f][1].split(), per[f])
    L += ['};', '']
    L.append('const PulseInput pulse_inputs[] = {')
    L += ['    { "%s", %s, HAL::Pulse::%s },' % (p['name'], p['channel'], p['edge']) for p in pul]
    L += ['};', '', '#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT', '']
    for m in mdl:
        args = '"%s", %s' % (m['model_name'], m['regs']) + (', %s' % m['words'] if m['words'] else '')
        L += ['const Modbus::DeviceModel %s = Modbus::%s(%s);' % (m['symbol'], m['kind'], args)]
    L += ['', 'const Modbus::Device g_devices[] = {']
    L += ['    { %s, &%s, "%s" },' % (d['addr'], d['model'], d['tag']) for d in dev]
    L += ['};', '', 'const Modbus::DeviceTable g_device_table = {',
          '    g_devices, (uint8_t)(sizeof(g_devices) / sizeof(g_devices[0]))', '};', '',
          '#endif', '',
          '// Filled at bring-up; extern in the header, or every TU including it',
          '// would carry a definition.',
          'VarRef home_refs[HOME_FIELDS] = {};', '',
          '}  // namespace Site', '']
    open(os.path.join(site, 'vars.gen.cpp'), 'w').write('\n'.join(L))

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
    G += ['extern const AnalogCal %s;' % c['cal'] for c in cal]
    G += ['', 'extern const AnalogCal* const g_analog_cals[];',
          'const uint8_t g_analog_cal_count = %d;' % len(cal), '',
          'extern const Descriptor params[];', 'const uint16_t PARAMS_COUNT = %d;' % n_par, '',
          'extern const Descriptor rows[];',   'const uint16_t ROWS_COUNT = %d;' % n_row, '',
          'extern const PulseInput pulse_inputs[];',
          'const uint8_t pulse_input_count = %d;' % len(pul), '',
          '#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT']
    G += ['extern const Modbus::DeviceModel %s;' % m['symbol'] for m in mdl]
    G += ['extern const Modbus::Device g_devices[];',
          'extern const Modbus::DeviceTable g_device_table;', '#endif', '']
    open(os.path.join(site, 'vars.gen.h'), 'w').write('\n'.join(G))
    print('  vars.gen.cpp   %d params, %d rows' % (n_par, n_row))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if   cmd == 'export':   export(SITE)
    elif cmd == 'check':    sys.exit(1 if check(SITE) else 0)
    elif cmd == 'generate': generate(SITE)
    else:                   sys.exit(__doc__)
