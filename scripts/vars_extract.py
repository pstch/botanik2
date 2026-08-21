#!/usr/bin/env python3
"""One-shot: today's hand-written variable tables -> the CSV table sets.

Reads the factory calls out of the site's vars.h and the library's vars.cpp and
writes tables/*.csv beside each. Disposable - it exists so 331 rows reach the
new format without a transcription error, and stops being useful the moment
vars_gen.py can produce the C++ back.

The rule that makes the round trip provable: a cell holds the C++ token text
EXACTLY, minus string quoting. `30*24*3600` stays an expression, `T_F32` stays a
symbol, `BOTANIK_LOG_MIN_LEVEL` stays a macro. The generator passes them back
through, so tests/data/table_golden.txt can prove nothing moved.

    python3 scripts/vars_extract.py            # write the CSVs
    python3 scripts/vars_extract.py --dry-run  # report what it found
"""

import argparse
import os
import re
import sys

import vars_csv

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# One entry per factory in vars/declare.h: its positional parameters, in order.
# Trailing optionals are absent from a call that does not pass them.
FACTORIES = {
    "A":  ["name", "code", "on_s", "off_s", "recent_s", "hold_s", "reg", "desc", "notes"],
    "P":  ["name", "type", "def", "min", "max", "digits", "unit", "reg", "desc",
           "notes", "access", "retain"],
    "PE": ["name", "def", "str", "reg", "desc", "notes", "access", "retain"],
    "PS": ["name", "def", "cap", "kind", "reg", "desc", "notes", "access", "retain"],
    "G":  ["name", "type", "def", "digits", "unit", "desc", "notes"],
    "GR": ["name", "type", "def", "digits", "unit", "desc", "notes"],
    "SV": ["name", "type", "def", "digits", "unit", "reg", "desc", "notes"],
    "SVR": ["name", "type", "def", "digits", "unit", "reg", "desc", "notes"],
    "SVE": ["name", "def", "str", "reg", "desc", "notes"],
    "MI": ["name", "type", "dev", "fc", "reg", "regs", "min", "max", "fallback",
           "timeout", "digits", "unit", "desc", "notes", "poll", "word_swap"],
    "AI": ["name", "cal", "dev", "fc", "reg", "regs", "wire", "fallback",
           "timeout", "digits", "unit", "desc", "notes", "poll", "word_swap"],
    "DI": ["name", "dev", "fc", "reg", "fallback", "timeout", "invert",
           "desc", "notes", "poll", "_pulse"],
    "MO": ["name", "type", "dev", "fc", "reg", "regs", "safe", "digits", "unit",
           "desc", "notes", "word_swap"],
    "AO": ["name", "cal", "dev", "fc", "reg", "regs", "wire", "safe", "digits",
           "unit", "desc", "notes", "word_swap"],
    "DO": ["name", "dev", "fc", "reg", "safe", "invert", "desc", "notes"],
    "RO": ["name", "dev", "coil", "safe", "invert", "desc", "notes"],
}

CAL_FIELDS = ["cal_raw_min", "cal_raw_max", "cal_eng_min", "cal_eng_max",
              "cal_min", "cal_max"]

# The library gates rows on BOTANIK_FEATURE_*; the site does not, and its column
# is simply always empty. Last, so it does not push the prose off the screen.
GATE = "feature"
# Source line, kept only long enough to order a file that holds more than one
# factory. params.csv interleaves P, PE and PS the way the source did, so
# `net_enable` and `net_ip` stay next to each other; grouping by factory would
# scatter a group across the PARAMS page.
ORDER = "_line"

# Which file each factory's rows land in, and that file's column order.
FILES = {
    "enums.csv":           ([], ["enum", "member", "code", "label", "desc"]),
    "params.csv":          (["P", "PE", "PS"],
                            ["scope", "name", "type", "def", "min", "max", "digits",
                             "unit", "cap", "kind", "str", "reg", "access", "retain",
                             "desc", "notes"]),
    "alarms.csv":          (["A"], FACTORIES["A"]),
    "globals.csv":         (["G", "GR"],
                            ["name", "type", "def", "digits", "unit", "retain",
                             "desc", "notes"]),
    "status.csv":          (["SV", "SVR", "SVE"],
                            ["scope", "name", "type", "def", "digits", "unit",
                             "str", "reg", "desc", "notes"]),
    "direct_inputs.csv":   (["MI"],
                            ["name", "type", "dev", "fc", "reg", "regs", "min", "max",
                             "fallback", "timeout", "poll", "word_swap", "digits",
                             "unit", "desc", "notes"]),
    "analog_inputs.csv":   (["AI"],
                            ["name", "dev", "fc", "reg", "regs", "wire", "fallback",
                             "timeout", "poll", "word_swap", "digits", "unit"]
                            + CAL_FIELDS + ["desc", "notes"]),
    "digital_inputs.csv":  (["DI"],
                            ["name", "dev", "fc", "reg", "fallback", "timeout",
                             "poll", "invert", "isr", "desc", "notes"]),
    "direct_outputs.csv":  (["MO"],
                            ["name", "type", "dev", "fc", "reg", "regs", "safe",
                             "word_swap", "digits", "unit", "desc", "notes"]),
    "analog_outputs.csv":  (["AO"],
                            ["name", "dev", "fc", "reg", "regs", "wire", "safe",
                             "word_swap", "digits", "unit"]
                            + CAL_FIELDS + ["desc", "notes"]),
    "digital_outputs.csv": (["DO"],
                            ["name", "dev", "fc", "reg", "safe", "invert",
                             "desc", "notes"]),
    "relay_outputs.csv":   (["RO"], FACTORIES["RO"]),
}


def read_gates(src):
    """Line number -> the BOTANIK_FEATURE_* a row on that line is guarded by.

    The library declares rows inside `#if BOTANIK_FEATURE_X` blocks, so the gate
    is part of the row and has to reach the CSV. Only the shape actually used is
    accepted - a single macro per #if, and no #else - and anything else stops the
    extraction rather than being guessed at. NESTED guards are kept as their
    conjunction rather than reduced to the innermost: `CONSOLE && HOME_PAGE` is
    what the source says, and the round trip has to be exact before it can be
    tidied.
    """
    gates, stack = {}, []
    for n, line in enumerate(src.split("\n"), 1):
        t = line.strip()
        if t.startswith("#if"):
            m = re.fullmatch(r"#if\s+(BOTANIK_FEATURE_\w+)", t)
            if m:
                stack.append(m.group(1))
            elif re.match(r"#ifn?def\s+\w+", t):
                stack.append(None)          # an include guard
            else:
                sys.exit(f"line {n}: unsupported preprocessor condition: {t}")
        elif t.startswith("#else") or t.startswith("#elif"):
            sys.exit(f"line {n}: #else in a variable table is not supported")
        elif t.startswith("#endif"):
            if stack:
                stack.pop()
        gates[n] = " && ".join(g for g in stack if g)
    return gates


def blank_directives(src):
    """Blank every preprocessor line, keeping offsets and line numbers, so a row
    after an #endif is still seen at an element position."""
    return "\n".join(" " * len(l) if l.strip().startswith("#") else l
                      for l in src.split("\n"))


def strip_comments(src):
    """Blank out // and /* */ without touching string or char literals."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q, j = c, i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_args(text):
    """Split a call's argument list on TOP-LEVEL commas."""
    args, depth, start, i, n = [], 0, 0, 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            q, j = c, i + 1
            while j < n and text[j] != q:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            args.append(text[start:i].strip())
            start = i + 1
        i += 1
    tail = text[start:].strip()
    if tail:
        args.append(tail)
    return args


def find_calls(src, names):
    """Every `NAME(...)` call at an element position, as (name, [args], line)."""
    out = []
    pattern = re.compile(r"(?<![A-Za-z0-9_])(" + "|".join(names) + r")\s*\(")
    for m in pattern.finditer(src):
        # An element starts after '{' or ',' or a line break, never after '.' or
        # '>' - which is what keeps `io.find(` and `Payload(` out.
        before = src[:m.start()].rstrip()
        if before and before[-1] not in "{,":
            continue
        depth, i, n = 1, m.end(), len(src)
        while i < n and depth:
            c = src[i]
            if c in "\"'":
                q, j = c, i + 1
                while j < n and src[j] != q:
                    j += 2 if src[j] == "\\" else 1
                i = j + 1
                continue
            depth += (c == "(") - (c == ")")
            i += 1
        out.append((m.group(1), split_args(src[m.end():i - 1]),
                    src.count("\n", 0, m.start()) + 1))
    return out


def unquote(tok):
    """A C string literal, possibly adjacent-concatenated, as its text."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', tok)
    if not parts:
        return tok.strip()
    return "".join(parts).replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")


def scalar(tok):
    """SF(1.0f)/SU(5)/SI(-3)/SB(true)/ST("x") -> the value the wrapper carries."""
    tok = tok.strip()
    m = re.fullmatch(r"S([FUIB])\s*\((.*)\)", tok, re.S)
    if m:
        return m.group(2).strip().rstrip("f") if m.group(1) != "B" else m.group(2).strip()
    if re.fullmatch(r"ST\s*\(.*\)", tok, re.S):
        return unquote(tok)
    # A bare string literal: a PS row's default is text, not a wrapped Scalar.
    if tok.startswith('"'):
        return unquote(tok)
    return tok


def tunable(tok):
    """R("p", 4000.0f) -> p@4000.0 ; K(4.0f) -> =4.0"""
    tok = " ".join(tok.split())
    m = re.fullmatch(r'R\s*\(\s*"([^"]*)"\s*(?:,\s*(.*?))?\s*\)', tok)
    if m:
        return m.group(1) + "@" + (m.group(2) or "0.0").rstrip("f")
    m = re.fullmatch(r"K\s*\(\s*(.*?)\s*\)", tok)
    if m:
        return "=" + m.group(1).rstrip("f")
    return tok


def read_cal_blocks(src):
    """const AnalogCal NAME = { six tunables };"""
    blocks = {}
    for m in re.finditer(r"const\s+AnalogCal\s+(\w+)\s*=\s*\{(.*?)\}\s*;", src, re.S):
        vals = [tunable(a) for a in split_args(m.group(2)) if a.strip()]
        if len(vals) != 6:
            sys.exit(f"{m.group(1)}: expected 6 tunables, found {len(vals)}")
        blocks[m.group(1)] = vals
    return blocks


def to_row(kind, args, cals, gate="", line=0):
    """One factory call as a {column: cell} mapping."""
    params = FACTORIES[kind]
    if len(args) > len(params):
        sys.exit(f"{kind}({args[0]}): {len(args)} arguments, {len(params)} parameters")
    row = dict(zip(params, args))

    row["name"] = unquote(row["name"])
    for k in ("desc", "notes", "unit", "code"):
        if k in row:
            row[k] = unquote(row[k])
    for k in ("def", "min", "max", "fallback", "safe"):
        if k in row:
            row[k] = scalar(row[k])
    if "str" in row:
        row["str"] = row["str"].lstrip("&").strip()
    if "cal" in row:
        name = row.pop("cal").lstrip("&").strip()
        if name not in cals:
            sys.exit(f"{row['name']}: unknown calibration block {name}")
        row.update(dict(zip(CAL_FIELDS, cals[name])))
    if kind in ("P", "PE", "PS", "SV", "SVE"):
        row["scope"] = kind
    if kind in ("G", "GR"):
        row["retain"] = "true" if kind == "GR" else ""
    row[GATE] = gate
    row[ORDER] = line
    out = {k: " ".join(str(v).split()) if k not in ("desc", "notes") else v
           for k, v in row.items()}
    out[ORDER] = line
    return out


def extract(src_path, out_dir, dry):
    raw = strip_comments(open(src_path, encoding="utf-8").read())
    gates = read_gates(raw)
    src = blank_directives(raw)
    cals = read_cal_blocks(src)
    calls = find_calls(src, list(FACTORIES))

    by_kind = {}
    for kind, args, ln in calls:
        by_kind.setdefault(kind, []).append(
            to_row(kind, args, cals, gates.get(ln, ""), ln))

    print(f"\n{os.path.relpath(src_path, ROOT)}")
    print(f"  {len(cals)} calibration blocks, {len(calls)} rows: "
          + ", ".join(f"{k}={len(v)}" for k, v in sorted(by_kind.items())))
    gated = sorted({r[GATE] for rows in by_kind.values() for r in rows if r[GATE]})
    if gated:
        n = sum(1 for rows in by_kind.values() for r in rows if r[GATE])
        print(f"  {n} rows gated on {len(gated)} flags: " + ", ".join(
            g.replace("BOTANIK_FEATURE_", "") for g in gated))

    if not dry:
        os.makedirs(out_dir, exist_ok=True)
    written = 0
    for fname, (kinds, base) in FILES.items():
        header = base + [GATE]
        rows = sorted((r for k in kinds for r in by_kind.get(k, [])),
                      key=lambda r: r[ORDER])
        if not rows:
            continue
        written += len(rows)
        if dry:
            print(f"    {fname:22s} {len(rows):3d} rows")
        else:
            vars_csv.write(os.path.join(out_dir, fname), header, rows,
                           preamble=(f"{fname} - GENERATED ONCE from the hand-written C++.",
                                     "From here on this file is the source of truth."))
    if written != len(calls):
        sys.exit(f"  {len(calls) - written} rows landed in no file")
    return len(calls)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true", help="report, write nothing")
    ap.add_argument("--site", default="gh_mlb41")
    a = ap.parse_args()

    total = 0
    for src, out in (
        (f"src/sites/{a.site}/vars.h",   f"src/sites/{a.site}/tables"),
        ("src/libbotanik/vars/vars.cpp", "src/libbotanik/vars/tables"),
    ):
        total += extract(os.path.join(ROOT, src), os.path.join(ROOT, out), a.dry_run)
    print(f"\n{total} rows total"
          + (" (dry run)" if a.dry_run else ""))


if __name__ == "__main__":
    main()
