#!/usr/bin/env python3
"""Line markdown tables up in the source, so a table reads as a table.

A table is a run of lines starting with `|` whose second line is the delimiter.
Cells split on unescaped pipes, `\\|` stays in the cell. Every column is padded
to its widest cell, and the delimiter keeps whatever alignment colons it had.

Importable as `align(text)`; as a command it rewrites the files it is given and
names the ones it changed, with `--check` to only name them.
"""

import re
import sys

_DELIM = re.compile(r'^:?-+:?$')


def _split(line):
    """The cells of @p line, unescaped pipes only, outer pipes dropped."""
    cells, cur, i = [], '', 0
    while i < len(line):
        c = line[i]
        if c == '\\' and i + 1 < len(line):
            cur += line[i:i + 2]
            i += 2
            continue
        if c == '|':
            cells.append(cur)
            cur = ''
        else:
            cur += c
        i += 1
    cells.append(cur)
    return [c.strip() for c in cells[1:-1]]


def _is_delim(cells):
    return bool(cells) and all(_DELIM.match(c) for c in cells)


def _rule(spec, width):
    """The delimiter cell for @p spec (its colons), filling @p width."""
    left, right = spec.startswith(':'), spec.endswith(':')
    dashes = max(width - left - right, 3 - left - right)
    return (':' if left else '') + '-' * dashes + (':' if right else '')


def _lay(cell, spec, width):
    if spec.endswith(':') and not spec.startswith(':'):
        return cell.rjust(width)
    if spec.startswith(':') and spec.endswith(':'):
        return cell.center(width)
    return cell.ljust(width)


def _table(rows, specs):
    """@p rows (lists of cells) as lines, every column at its widest."""
    n = max(len(r) for r in rows)
    specs = (specs + [''] * n)[:n]
    rows = [r + [''] * (n - len(r)) for r in rows]
    body = rows[:1] + rows[2:]
    width = [max(len(r[i]) for r in body) for i in range(n)]
    out = []
    for j, r in enumerate(rows):
        if j == 1:
            cells = [_rule(specs[i], width[i] + 2) for i in range(n)]
            out.append('|' + '|'.join(cells) + '|')
        else:
            cells = [' %s ' % _lay(r[i], specs[i], width[i]) for i in range(n)]
            out.append('|' + '|'.join(cells) + '|')
    return out


def align(text):
    """@p text with every markdown table padded into columns."""
    lines = text.split('\n')
    out, i, fence = [], 0, None
    while i < len(lines):
        line = lines[i]
        stripped = line.lstrip()
        if fence:
            if stripped.startswith(fence):
                fence = None
            out.append(line)
            i += 1
            continue
        if stripped.startswith('```') or stripped.startswith('~~~'):
            fence = stripped[:3]
            out.append(line)
            i += 1
            continue
        head = _split(line) if stripped.startswith('|') else None
        delim = _split(lines[i + 1]) if head and i + 1 < len(lines) else None
        if not head or not delim or not _is_delim(delim):
            out.append(line)
            i += 1
            continue
        indent = line[:len(line) - len(stripped)]
        rows, j = [], i
        while j < len(lines) and lines[j].lstrip().startswith('|'):
            rows.append(_split(lines[j]))
            j += 1
        out += [indent + l for l in _table(rows, delim)]
        i = j
    return '\n'.join(out)


def main(argv):
    check = '--check' in argv
    paths = [a for a in argv if not a.startswith('-')]
    changed = []
    for p in paths:
        with open(p, encoding='utf-8') as f:
            src = f.read()
        out = align(src)
        if out != src:
            changed.append(p)
            if not check:
                with open(p, 'w', encoding='utf-8') as f:
                    f.write(out)
    for p in changed:
        print(p)
    return 1 if check and changed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
