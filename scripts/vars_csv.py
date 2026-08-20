#!/usr/bin/env python3
"""The CSV dialect the variable tables are written in, in one place.

Both the extractor and the generator go through this, so the tables cannot be
read one way and written another.

The dialect:

  - lines whose first non-space character is `#` are comments, and carry the
    group headers and the register-block legend that used to sit between the
    C++ rows
  - the first non-comment line is the header; COLUMNS ARE ADDRESSED BY NAME,
    never by position, so adding one does not renumber the rest
  - every cell is padded for alignment and STRIPPED on read. The padding is
    presentation, exactly like the aligned C++ these tables came from; it is
    never part of a value
  - a cell holding a comma, a quote or a newline is RFC 4180 quoted
  - an empty cell means "this column does not apply to this row", and the
    generator checks that it really does not
  - a cell otherwise holds the C++ token text: `T_F32` stays a symbol,
    `30*24*3600` stays an expression, `BOTANIK_HOST_STR_LEN` stays a macro
"""

import csv
import io
import os


def read(path):
    """Every row of a table as a {column: cell} mapping, comments dropped.

    Each row also carries `_file` and `_line` - its own place in the file - so a
    check can report against the line somebody has to edit rather than against a
    row index nobody can find. They are stripped before the row is written back.
    """
    with open(path, encoding="utf-8", newline="") as f:
        kept = [(n, l) for n, l in enumerate(f, 1)
                if not l.lstrip().startswith("#")]
    text = "".join(l for _, l in kept)
    lineno = [n for n, _ in kept]
    # skipinitialspace is load-bearing, not cosmetic: without it the space
    # the alignment puts before a cell stops that cell being seen as QUOTED,
    # and a comma inside a note then splits the row.
    rows = list(csv.reader(io.StringIO(text), skipinitialspace=True))
    if not rows:
        return []
    header = [h.strip() for h in rows[0]]
    base = os.path.basename(path)
    out = []
    for i, r in enumerate(rows[1:], 1):
        if not any(c.strip() for c in r):
            continue
        cells = [c.strip() for c in r] + [""] * (len(header) - len(r))
        row = dict(zip(header, cells))
        row["_file"] = base
        row["_line"] = lineno[i] if i < len(lineno) else 0
        out.append(row)
    return out


def header_of(path):
    """The column names, in file order."""
    with open(path, encoding="utf-8", newline="") as f:
        for line in f:
            if line.lstrip().startswith("#") or not line.strip():
                continue
            return [h.strip() for h in
                    next(csv.reader(io.StringIO(line), skipinitialspace=True))]
    return []


def write(path, header, rows, preamble=()):
    """Write a table column-aligned, with @p preamble as leading `#` comments.

    Alignment is what makes a 143-row table readable as a table, which is how
    the C++ it replaces read. It is re-derived on every write, so the generator
    can reformat in place the way a code formatter does.
    """
    header = [h for h in header if not h.startswith("_")]

    def cell(v):
        v = "" if v is None else str(v)
        return '"' + v.replace('"', '""') + '"' if any(c in v for c in ',"\n\r') else v

    grid = [[cell(r.get(h, "")) for h in header] for r in rows]
    width = [max([len(h)] + [len(g[i]) for g in grid]) for i, h in enumerate(header)]

    def line(cells):
        return ", ".join(c.ljust(width[i]) for i, c in enumerate(cells)).rstrip() + "\n"

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as f:
        for p in preamble:
            f.write(("# " + p).rstrip() + "\n")
        if preamble:
            f.write("#\n")
        f.write(line(header))
        for g in grid:
            f.write(line(g))
