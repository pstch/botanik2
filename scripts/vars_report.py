#!/usr/bin/env python3
"""Two documents out of the variable tables.

    --schema           the LIBRARY's reference, into docs/integration/:
                       the rows libbotanik declares, and the shape of the CSV
                       and TOML files a site writes.

    --site <name>      that SITE's report: every variable and program it will
                       actually have, the library's included, since the two are
                       one table once registered.

Markdown always, from the standard library alone. A PDF as well when reportlab
imports; without it the Markdown still lands and the script says so rather than
failing. Neither output is part of the build.
"""

import argparse
import os
import re
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vars_csv
import vars_devices
import vars_gen
import vars_programs
import vars_schema
from vars_extract import FACTORIES, FILES, GATE

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Which rows share a section. `P`, `PE` and `PS` are three FACTORIES and one
# kind of row - an operator setting a value does not care which spelling
# declares it - so they read as one table, with the columns a given factory does
# not use left empty. `SV`/`SVE` likewise.
GROUP_OF = {"PE": "P", "PS": "P", "SVE": "SV", "SVR": "SV"}

# Sections split into what the site declares and what libbotanik does. Only
# these two: a status value or a field row is the site's by construction, and
# the library's handful of readings read as part of the same picture.
SPLIT_BY_ORIGIN = ("P", "A")

# Where the PDF starts a new page. An HTML comment, so the Markdown carries it
# without showing it: the two outputs stay one document.
PAGE_BREAK = "<!-- page -->"

SCOPE_TITLES = OrderedDict([
    ("P",   "Parameters"),
    ("A",   "Alarms"),
    ("SV",  "Status values"),
    ("G",   "Computed globals"),
    ("GR",  "Computed globals, retained"),
    ("MI",  "Direct inputs"),
    ("AI",  "Analog inputs"),
    ("DI",  "Digital inputs"),
    ("MO",  "Direct outputs"),
    ("AO",  "Analog outputs"),
    ("DO",  "Digital outputs"),
    ("RO",  "Relay outputs"),
])

# Columns worth a table cell in a document, per scope. The rest go to the prose
# under each row: a report nobody can read across is not a report.
SHOWN = {
    "P":  ["name", "type", "def", "min", "max", "unit", "reg"],
    "PE": ["name", "def", "str", "reg"],
    "PS": ["name", "def", "cap", "kind", "reg"],
    "A":  ["name", "code", "on_s", "off_s", "hold_s", "recent_s", "reg"],
    "SV": ["name", "type", "digits", "unit", "reg"],
    "SVE": ["name", "def", "str", "reg"],
    "G":  ["name", "type", "def", "unit"],
    "GR": ["name", "type", "def", "unit"],
    "MI": ["name", "type", "dev", "fc", "reg", "regs", "min", "max", "timeout", "unit"],
    "AI": ["name", "dev", "fc", "reg", "wire", "timeout", "unit", "_scaling"],
    "DI": ["name", "dev", "reg", "isr", "invert"],
    "MO": ["name", "type", "dev", "fc", "reg", "regs", "safe", "unit"],
    "AO": ["name", "dev", "fc", "reg", "wire", "safe", "unit", "_scaling"],
    "DO": ["name", "dev", "fc", "reg", "safe", "invert"],
    "RO": ["name", "dev", "coil", "safe", "invert"],
}


# The vocabularies the LIBRARY defines, transcribed. Static because they are:
# `Logs::level_label` and `Plc::health_label` are switch statements in
# logs/topics.h and plc/program.h, and a site's table only names them. Kept here
# so an integrator's point list is complete rather than pointing at a header
# they do not have - and pinned from the other side by
# `enum vocabularies: the library's two are what this document says` in
# tests/test_vars.cpp, so a renumbering breaks a test rather than a document.
LIBRARY_ENUMS = OrderedDict([
    ("Logs::level_label", {
        "what": "A log severity. BIT VALUES, not a run: the gaps are what lets "
                "a sink hold a mask of levels, and a code that is not one of "
                "these is REFUSED rather than shown.",
        "members": [("1", "debug", "LOG_DEBUG", "everything, including the "
                                                "per-beat detail"),
                    ("2", "info", "LOG_INFO", "what the plant did"),
                    ("4", "notice", "LOG_NOTICE", "worth looking up for"),
                    ("8", "warning", "LOG_WARNING", "wrong, and still running"),
                    ("16", "error", "LOG_ERROR", "wrong, and something stopped"),
                    ("32", "critical", "LOG_CRITICAL", "undroppable: the rate "
                                                       "limiter never sheds it")],
    }),
    ("Plc::health_label", {
        "what": "What a control program could do on its last activation. The "
                "first three are a SEVERITY RUN and are ordered, so a "
                "supervisor takes the worst across the programs in one "
                "comparison. INHIBITED is off that axis: its control has "
                "stopped and its actuators are parked, but it is still "
                "reporting.",
        "members": [("0", "HEALTHY", "Health::HEALTHY", "normal operation; "
                                                        "every output valid"),
                    ("1", "DEGRADED", "Health::DEGRADED", "an input is invalid; "
                                                          "the outputs are still good"),
                    ("2", "FAULTED", "Health::FAULTED", "at least one output is "
                                                        "invalid"),
                    ("3", "INHIBITED", "Health::INHIBITED", "control stopped and "
                                                            "actuators parked; still reporting")],
    }),
])


# The six cells of an analog calibration, in the order the block declares them.
CAL_ROLES = [("cal_raw_min", "raw min"), ("cal_raw_max", "raw max"),
             ("cal_eng_min", "engineering min"), ("cal_eng_max", "engineering max"),
             ("cal_min", "plausible min"), ("cal_max", "plausible max")]


def _cal_param(cell):
    """The parameter a calibration cell names, or None for a fixed constant."""
    if not cell or cell.startswith("="):
        return None
    return cell.split("@")[0]


def scaling_groups(rows):
    """The analog rows' calibrations, grouped by the parameters they NAME.

    Sharing a calibration is not a pointer in this model - it is two rows naming
    the same six parameters - so the group is derived rather than declared, and
    a group is exactly a set of rows whose six cells agree. Named after the stem
    those parameters share, which is what an operator sees on the PARAMS page.
    """
    groups = OrderedDict()
    for kind, r in rows:
        if kind not in ("AI", "AO"):
            continue
        key = tuple(r.get(c, "") for c, _ in CAL_ROLES)
        if key not in groups:
            params = [p for p in (_cal_param(c) for c in key) if p]
            # The stem the named parameters share: `pwr_dc1_rawMin_mV` and its
            # five siblings give `pwr_dc1`. A calibration of pure constants has
            # no parameters to take a name from, so it borrows the row's.
            if params:
                stem = os.path.commonprefix(params).rstrip("_")
                name = stem or params[0]
            else:
                name = r["name"] + " (fixed)"
            groups[key] = {"name": name, "rows": [], "cells": key}
        groups[key]["rows"].append(r["name"])
        r["_scaling"] = groups[key]["name"]
    return groups


def label(col):
    return vars_schema.COLUMNS.get(col, (col, ""))[0]


def load_set(table_dir, name):
    """Every row of one table set, as (scope, row), in file order."""
    out = []
    for fname, (kinds, _) in FILES.items():
        path = os.path.join(table_dir, fname)
        # enums.csv declares vocabularies, not rows: it has no factory.
        if not os.path.exists(path) or not kinds:
            continue
        for r in vars_csv.read(path):
            kind = r.get("scope") or (("GR" if r.get("retain") else "G")
                                      if fname == "globals.csv" else kinds[0])
            r["_set"] = name
            out.append((kind, r))
    return out


ARRAY_RE = re.compile(r"(.*?)(\d+)")


def row_range(names):
    """A run of rows differing only by a trailing index, as one cell."""
    if len(names) < 2:
        return names[0] if names else ""
    parts = [ARRAY_RE.fullmatch(n) for n in names]
    stems = {p.group(1) for p in parts if p}
    if len(parts) == len(names) and len(stems) == 1 and all(parts):
        idx = sorted(int(p.group(2)) for p in parts)
        return f"{stems.pop()}{idx[0]}..{idx[-1]}"
    return ", ".join(names)


def by_scope(rows):
    out = OrderedDict((k, []) for k in SCOPE_TITLES)
    for kind, r in rows:
        r["_kind"] = kind
        out.setdefault(GROUP_OF.get(kind, kind), []).append(r)
    return OrderedDict((k, v) for k, v in out.items() if v)


# --- Markdown -------------------------------------------------------------

def md_table(header, body):
    if not body:
        return []
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join("---" for _ in header) + "|"]
    for row in body:
        out.append("| " + " | ".join(c.replace("|", "\\|") or "-" for c in row) + " |")
    return out + [""]


# A section that merges factories needs cells that mean the same thing across
# all of them: a numeric with a band, an enum with a converter and a string with
# a capacity are all "a type" and "what it accepts".
MERGED_COLS = {
    "P":  ["name", "type", "def", "range", "unit", "access", "reg"],
    "SV": ["name", "type", "digits", "unit", "range", "reg"],
}


def _merged_cell(col, r):
    kind = r.get("_kind", "P")
    if col == "type":
        # Not the capacity: `text[BOTANIK_HOST_STR_LEN]` is a declaration
        # detail, it wraps to three lines, and the CSV is where it belongs.
        return {"PE": "enum", "SVE": "enum", "PS": "text"}.get(
            kind, r.get("type", ""))
    if col == "range":
        if kind in ("PE", "SVE"):
            return "`" + r.get("str", "") + "`"
        if kind == "PS":
            return r.get("kind", "")
        lo, hi = r.get("min", ""), r.get("max", "")
        return ("%s .. %s" % (lo, hi)) if lo or hi else ""
    if col == "access":
        return r.get("access") or "RW"
    return r.get(col, "")


def compact_arrays(rows):
    """Rows differing only by a trailing index, as one row.

    PARAMETERS only. The eight tray settings are one knob an operator turns
    eight times, and listing each is eight lines saying the same sentence. The
    FIELD tables are not compacted: those are the address map, and every row
    there has to be findable against the device and register it lands on.
    """
    groups, order = {}, []
    for r in rows:
        m = ARRAY_RE.fullmatch(r["name"])
        key = (m.group(1), True) if m and m.group(1) else (r["name"], False)
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(r)

    out = []
    for key in order:
        members = groups[key]
        if len(members) < 2:
            out += members
            continue
        merged = dict(members[0])
        merged["name"] = row_range([m["name"] for m in members])
        for col in set().union(*(set(m) for m in members)) - {"name"}:
            vals = [str(m.get(col, "")) for m in members]
            if len(set(vals)) == 1:
                continue
            nums = [v for v in vals if v.strip().lstrip("-").isdigit()]
            if len(nums) == len(vals):
                n = sorted(int(v) for v in nums)
                step = {n[i + 1] - n[i] for i in range(len(n) - 1)}
                merged[col] = (f"{n[0]}..{n[-1]} by {step.pop()}"
                               if len(step) == 1 and step != {1}
                               else f"{n[0]}..{n[-1]}")
            else:
                # Differing only where the index sits: "tray 1 valve" and its
                # siblings are one sentence with a number in the middle.
                norm = {re.sub(r"\d+", "n", v) for v in vals}
                merged[col] = norm.pop() if len(norm) == 1 else vals[0] + " .."
        out.append(merged)
    return out


def md_rows(kind, rows, effective=None):
    if kind == "P":
        rows = compact_arrays(rows)
    merged = kind in MERGED_COLS
    cols = (list(MERGED_COLS[kind]) if merged
            else [c for c in SHOWN.get(kind, ["name"])
                  if any(r.get(c) for r in rows)])
    cell = _merged_cell if merged else (lambda c, r: r.get(c, ""))
    # Description and notes are COLUMNS, not a prose list underneath: a reader
    # should not have to join two structures by hand to learn what a row is.
    cols = cols + [c for c in ("desc", "notes") if any(r.get(c) for r in rows)]

    body = []
    for r in rows:
        cells = [cell(c, r) for c in cols]
        if effective is not None and r["name"] in effective:
            cells[0] += " *(restated)*"
        body.append(cells)
    return md_table([label(c) for c in cols], body)


def _scaling_doc(groups, by_name):
    """One table per calibration: what scales a raw count into a reading, and
    which parameter carries each end of it."""
    if not groups:
        return []
    out = ["## Analog scaling", "",
           f"{len(groups)} calibrations over the site's analog rows. Each maps a",
           "raw count onto a reading and says what range of that reading is",
           "believable - a dead 4-20 mA loop reads full scale, and no timeout",
           "catches that.", "",
           "**`scale` and `offset` are never stored.** They are derived from the",
           "four corners every time they are used, so a recalibration lands on",
           "the next reading rather than at the next boot.", "",
           "A calibration is SHARED by being named, not by being pointed at:",
           "two rows naming the same six parameters are scaled by the same",
           "numbers, and there is no third object to keep in step with them.", ""]
    for g in groups.values():
        out += [f"### {g['name']}", "",
                "Scales " + ", ".join(f"`{r}`" for r in g["rows"]) + ".", ""]
        body = []
        for (col, role), cell in zip(CAL_ROLES, g["cells"]):
            name = _cal_param(cell)
            if name is None:
                body.append([role, "-", cell.lstrip("="), "", "fixed in firmware"])
                continue
            kind, r = by_name.get(name, ("?", {}))
            body.append([role, name, r.get("def", ""), r.get("unit", ""),
                         r.get("desc", "")])
        out += md_table(["role", "parameter", "default", "unit", "description"],
                        body)
    return out


def schema_doc():
    """The library's own rows, and the shape of a site's input files."""
    lib = load_set(os.path.join(ROOT, "src", "libbotanik", "vars", "tables"),
                   "libbotanik")
    out = ["# Variable tables {#int_tables_schema}", "",
           "GENERATED by `scripts/vars_report.py --schema`. Edit the tables or",
           "`scripts/vars_schema.py`, not this file.", "",
           "A site declares its rows as CSV under `tables/`, and its programs as",
           "`tables/programs.toml`. `scripts/vars_gen.py` turns both into the C++",
           "under `generated/`, which is COMMITTED: the build has no Python",
           "dependency, and `make check` runs `check-vars` to catch a table edited",
           "without regenerating.", "",
           "## How a cell is read", ""]
    out += [f"- {r}" for r in vars_schema.CELL_RULES] + [""]

    out += ["## The files", "",
            "One file per row factory. Each file's columns are exactly that",
            "factory's arguments, so a row that could not be spelled in C++",
            "cannot be spelled here either. `enums.csv` is the exception: it",
            "declares no rows, only the vocabularies they are written over.", ""]
    out += md_table(["file", "rows it holds", "columns"],
                    [[f"`{f}`", ", ".join(k) or "-",
                      str(len(FACTORIES[k[0]]) + 1 if k else len(c))]
                     for f, (k, c) in FILES.items()])

    for fname, (kinds, cols) in FILES.items():
        out += [f"### `{fname}`", ""]
        missing = [c for c in cols if c not in vars_schema.COLUMNS]
        if missing:
            sys.exit(f"{fname}: columns with no entry in vars_schema.COLUMNS: {missing}")
        out += md_table(["column", "meaning"],
                        [[f"`{c}`", vars_schema.COLUMNS[c][1]] for c in cols]
                        + [[f"`{GATE}`", vars_schema.COLUMNS[GATE][1]]])

    out += ["## The field bus: `device_models.csv` and `devices.csv`", "",
            "What answers at each unit address on the RS-485 segment, and what a",
            "request to it must look like. Two files because a model is shared",
            "and a unit is not: twelve identical sensors are twelve device rows",
            "over one model row. Neither holds a signal map - where a reading",
            "lives is the variable row's business.", "",
            "A unit a row addresses and no file declares is an ERROR, not a",
            "default: the client would cut its requests against the generic",
            "model, which is the protocol's own limits and no alignment rule.",
            "The reverse - a device nobody reads - is reported as a note.", ""]
    out += ["### `device_models.csv`", ""]
    out += md_table(["column", "meaning"],
                    [[f"`{c}`", vars_schema.DEVICE_COLUMNS[c][1]]
                     for c in vars_devices.MODEL_COLS])
    out += ["### `devices.csv`", ""]
    out += md_table(["column", "meaning"],
                    [[f"`{c}`", vars_schema.DEVICE_COLUMNS[c][1]]
                     for c in vars_devices.DEVICE_COLS])

    out += ["## `programs.toml`: a site's control programs", "",
            "The IEC 61131-3 CONFIGURATION layer: which programs exist, what task",
            "each runs under, and which table row each of its variables is",
            "connected to. A `Plc::Program` is a PROGRAM POU and takes no",
            "arguments, so this file is what connects its declared interface to",
            "the global table.", "",
            "```toml",
            "[[program]]",
            'name  = "supply_watch"      # the instance, and its writer identity',
            'class = "SupplyWatch"       # the C++ class it is an instance of',
            'task  = "SLOW"              # the rate class it is dispatched at',
            'desc  = "The supply rails, watched."   # one line: what it is FOR',
            'notes = """                 # the paragraphs under it, optional',
            "Everything a reader needs that the interface below does not say.",
            '"""',
            "",
            "# The POU's VAR section: the function block instances it holds. A",
            "# bare string is the type, resolved in Botanik::Plc; the table form",
            "# carries `count` for an array, `include` for a block of the site's",
            "# own (site-root relative), and `doc` for the one-line comment the",
            "# declaration gets.",
            "[program.block]",
            'settle    = "Tof"',
            'rail_avg  = { type = "Ewma", count = 2, doc = "one per rail" }',
            "",
            "# A member is `name = \"row\"`, a list for an array, or a PATTERN",
            "# where `*` is one or more digits - eight trays without writing",
            "# eight names. The member takes the FORMAL name, so one class can",
            "# serve two sites.",
            "[program.in]",
            'ln    = "grid_LN_V"',
            'rail  = { rows = "dc_rail*_V", count = 4 }',
            "",
            "[program.out]",
            'locked = "sys_supply_lockout"',
            'win    = ["cli_window1_opn", "cli_window2_opn"]',
            "```", "",
            "The generator emits one BASE CLASS per program, into",
            "`programs/<stage>/<class>/<class>.gen.h`: the rows as members typed",
            "from the table, the blocks it declares, the constructor with the",
            "instance name and rate class baked in, and the `bind()` that",
            "resolves it all. The site derives it and writes the body.",
            "",
            "`desc` is the one place a program says what it is for: it becomes",
            "the generated class's `@brief` AND this site report's heading for",
            "it, so a header and a binder page cannot describe one program two",
            "ways. `notes` is where the paragraphs under it go, and stays in",
            "the documents - rationale does not belong beside the code.", ""]
    out += ["",
            "A PROGRAM POU may connect a formal parameter to a global or name",
            "the global directly; both spellings land on the same three modes,",
            "so there is one section per mode rather than one per spelling.", ""]
    out += md_table(["section", "IEC 61131-3", "rule"], [
        ["`in`", "VAR_INPUT / VAR_EXTERNAL CONSTANT",
         "read-only: `drive()` on it does not compile"],
        ["`out`", "VAR_OUTPUT / VAR_EXTERNAL",
         "this program is the row's one producer, claimed at bind time"],
        ["`inout`", "VAR_IN_OUT",
         "read-modify-write, SHARED by declaration: the row is marked "
         "`VF_SHARED` and records no single owner"],
    ])
    out += ["A row carrying text has no typed reference - its value lives in",
            "the bank's pool - so naming one from a program is refused.", "",
            "`*` matches DIGITS and nothing else, so `irr_tray*` takes the eight",
            "numbered trays and not `irr_traysPerDay`, and the order it expands",
            "in is numeric - `tray10` follows `tray9`. `count` is optional and",
            "asserted: it is how a row quietly added or removed becomes an error",
            "rather than an array that changed size. The generated `bind()` lists",
            "every name it resolved to, and that file is committed, so an",
            "expansion that moves shows up as a diff.", "",
            "## `enums.csv`: the vocabularies a row is written over", "",
            "One line per MEMBER. The generator emits the C++ enum AND the",
            "converter a `PE`/`SVE` row takes the address of, so the type and",
            "its labels cannot part - gh_mlb41 declared each of its two twice",
            "before this file existed, once by hand and once in",
            "`vars.cpp`.", "",
            "```",
            "enum    , member     , code, label  , desc",
            "IrrState, IRR_IDLE   , 0   , IDLE   , no tray is being served",
            "IrrState, IRR_FILLING, 1   , FILLING, inlet open; tray open",
            "```", "",
            "A row is written over one by naming it in `str`, and defaults to a",
            "MEMBER rather than to a number - `def = IRR_IDLE`. The checks: no",
            "two members of a vocabulary share a code or a label, a member name",
            "is unique across the FILE (an unscoped enum puts them all in one",
            "scope), and a row's default is a member of the vocabulary it names.",
            "",
            "A row whose vocabulary belongs to the LIBRARY names that instead -",
            "`Logs::level_label` - and its codes come from the library's own",
            "header. The log level is the logger's type, used across the tree;",
            "generating it here would invert the include order.", ""]
    out += ["## The library's own rows", "",
            f"libbotanik declares {len(lib)} rows of its own - alarms it raises,",
            "readings it publishes, parameters it reads. They register BEFORE a",
            "site's and share one flat index space, so the two read as one table.",
            "Registers 900-985 are the library's, and its alarms take 986-999.", "",
            "A site may restate any of them by declaring the same name: the site's",
            "wins and the library's is compacted out. Keep the register the library",
            "assigned, and keep the type - the library binds these by both.", ""]
    for kind, rows in by_scope(lib).items():
        out += [f"### {SCOPE_TITLES[kind]}", ""] + md_rows(kind, rows)
    return "\n".join(out) + "\n"


def site_doc(site):
    """Everything one site will actually have, the library's rows included."""
    lib = load_set(os.path.join(ROOT, "src", "libbotanik", "vars", "tables"),
                   "libbotanik")
    own = load_set(os.path.join(ROOT, "src", "sites", site, "tables"), site)

    # What the firmware ends up with: a site restatement replaces the library row.
    restated = {r["name"] for _, r in own} & {r["name"] for _, r in lib}
    rows = [(k, r) for k, r in lib if r["name"] not in restated] + own

    out = [f"# {site}: variables and programs", "",
           f"GENERATED by `scripts/vars_report.py --site {site}`.", "",
           f"{len(rows)} rows: {len(own)} declared by the site, "
           f"{len(lib) - len(restated)} inherited from libbotanik"
           + (f", {len(restated)} restated" if restated else "") + ".", ""]

    counts = [[SCOPE_TITLES[k], str(len(v))] for k, v in by_scope(rows).items()]
    out += md_table(["kind", "rows"], counts)

    # Before the tables: it stamps each analog row with the group that scales
    # it, which those tables then name in a column of their own.
    groups = scaling_groups(rows)

    # Parameters and alarms are read by two different people: the site's are
    # this greenhouse's, the library's belong to the firmware and are the same
    # on every install. Split rather than interleaved, so neither list has to be
    # filtered by eye.
    own_names = {r["name"] for _, r in own}
    for kind, group in by_scope(rows).items():
        out += [f"## {SCOPE_TITLES[kind]}", ""]
        if kind not in SPLIT_BY_ORIGIN:
            out += md_rows(kind, group, restated)
            continue
        mine = [r for r in group if r["name"] in own_names]
        libs = [r for r in group if r["name"] not in own_names]
        if mine:
            out += [f"### {site} ({len(mine)})", ""]
            out += md_rows(kind, mine, restated)
        if libs:
            out += [f"### Library-level ({len(libs)})", "",
                    "Declared by the firmware, on every site that compiles the",
                    "feature. A site may restate any of them - the restated row",
                    "then appears above, in its own list.", ""]
            out += md_rows(kind, libs, restated)

    by_name = {r['name']: (k, r) for k, r in rows}
    out += _scaling_doc(groups, by_name)
    out += programs_doc(site, by_name)
    out += orphans_doc(site, by_name)

    # The register map is what a SCADA integrator actually needs, and it cannot
    # be read off any one table: it spans both sets and sorts by register.
    def block(kinds):
        return sorted([(k, r) for k, r in rows if k in kinds],
                      key=lambda kr: int(kr[1]["reg"])
                      if kr[1]["reg"].isdigit() else 0)

    params, status, alarms = (block(("P", "PE", "PS")),
                              block(("SV", "SVR", "SVE")), block(("A",)))
    mapped = params + status + alarms

    out += field_bus_doc(site, rows)

    out += ["## Modbus TCP register map", "",
            "What an external master reads. Hand-assigned, and a contract: adding a",
            "row never renumbers the others. Split by KIND rather than sorted into",
            "one run, because an integrator arrives looking for one of the three.", "",
            "- an **enum** is ONE register carrying its numeric CODE, never its",
            "  label. The codes are the contract: a converter may gain labels, it",
            "  may never renumber them.",
            "- a **text** row is RESERVED, NOT SERVED. A 16-bit register cannot",
            "  hold a hostname, so the slave answers `EX_ILLEGAL_ADDRESS` rather",
            "  than a zero that would read as a value. Set these over the console",
            "  or the shell. The register is kept so the numbering stays stable if",
            "  strings are ever packed onto the wire.", ""]

    out += ["### Parameters", "",
            "**RW** is served and written; **RO** is served and never written.",
            "`mb_write_enable` is the slave-wide switch above that: off, nothing",
            "is written whatever a row says.", ""]

    def access_of(kind, r):
        if kind == "PS":
            return "reserved"
        return r.get("access") or "RW"

    out += md_table(["reg", "name", "access", "unit", "description"],
                    [[r["reg"], r["name"], access_of(kind, r),
                      r.get("unit", ""), r.get("desc", "")]
                     for kind, r in params])

    out += ["### Status values", "",
            "Read-only: a master may not write a reading. An `SVE` is one",
            "register carrying its numeric code.", ""]
    out += md_table(["reg", "name", "unit", "description"],
                    [[r["reg"], r["name"], r.get("unit", ""), r.get("desc", "")]
                     for _, r in status])

    out += ["### Alarms", "",
            "Read-only as registers, and each has an ACK coil beside it - the one",
            "thing a master may write on this side of the map.", ""]
    out += md_table(["reg", "name", "code", "description"],
                    [[r["reg"], r["name"], r.get("code", ""), r.get("desc", "")]
                     for _, r in alarms])

    enums = [r for k, r in mapped if k in ("PE", "SVE")]
    if enums:
        out += ["### Enum rows", "",
                "Each is one register carrying a numeric CODE, never its label.",
                "The vocabulary each is written over is below; a row whose",
                "vocabulary is the library's names that instead, and its codes",
                "come from the library's own header.", ""]
        out += md_table(["reg", "name", "vocabulary", "notes"],
                        [[r["reg"], r["name"], "`" + r.get("str", "") + "`",
                          r.get("notes", "")] for r in enums])

        named = {r.get("str", "") for r in enums}
        vocab = vars_gen.load_enums(
            os.path.join(ROOT, "src", "sites", site, "tables"))
        for name, members in vocab.items():
            if name not in named:
                continue
            out += [f"#### {name}", "",
                    "The code is the CONTRACT: a vocabulary may gain members and",
                    "must never renumber them. The label is what an operator",
                    "reads and types, and may change freely.", ""]
            out += md_table(["code", "label", "C++", "meaning"],
                            [[m["code"], m["label"], "`" + m["member"] + "`",
                              m.get("desc", "")] for m in members])
        for name, v in LIBRARY_ENUMS.items():
            if name not in named:
                continue
            out += [f"#### `{name}`", "", v["what"], ""]
            out += md_table(["code", "label", "C++", "meaning"],
                            [[c, l, "`" + cpp + "`", why]
                             for c, l, cpp, why in v["members"]])

    published = [r for k, r in rows if k not in ("P", "PE", "PS", "A")]
    out += ["## MQTT telemetry", "",
            f"{len(published)} rows are published: everything except a parameter",
            "and an alarm, which each have a page of their own.", ""]
    return "\n".join(out) + "\n"


# What a row on the bus is doing there, per scope: the direction is not in the
# CSV, it is what the factory means.
FIELD_DIR = {"MI": "read", "AI": "read", "DI": "read",
             "MO": "write", "AO": "write", "DO": "write", "RO": "write"}


def _field_addr(kind, r):
    """Where on the device the row sits: a coil, or a register run."""
    if kind == "RO":
        return "coil " + r.get("coil", ""), ""
    return r.get("reg", ""), r.get("regs", "")


def field_bus_doc(site, rows):
    """Every unit on the segment, and the rows that address it.

    The report read the other way round: the variable tables are sorted by what
    a value MEANS, and an engineer at the cabinet needs them sorted by what they
    are wired to.
    """
    models, devices = vars_devices.load(
        os.path.join(ROOT, "src", "sites", site, "tables"))
    field = [(k, r) for k, r in rows if k in FIELD_DIR and r.get("dev", "")]
    if not field:
        return []

    by_unit = OrderedDict()
    for k, r in field:
        by_unit.setdefault(int(r["dev"]), []).append((k, r))
    by_model = {m["model"]: m for m in models}

    out = ["## The field bus", "",
           "The RS-485 segment, by unit: what answers there and which rows it",
           "carries. Same rows as the tables above, addressed the way an",
           "engineer at the cabinet has them - by the device they are wired to.",
           "", "Unit 0 is the controller's own terminals, on no bus at all.", ""]
    out += md_table(["unit", "tag", "model", "shape", "rows"],
                    [[str(d["unit"]), d["tag"],
                      by_model.get(d["model"], {}).get("desc", d["model"]),
                      by_model.get(d["model"], {}).get("shape", ""),
                      str(len(by_unit.get(int(d["unit"]), [])))]
                     for d in devices])

    for unit in sorted(by_unit):
        d = next((x for x in devices if int(x["unit"]) == unit), None)
        m = by_model.get(d["model"], {}) if d else {}
        if unit == 0:
            out += ["### Unit 0: the controller's own terminals", "",
                    "`XP_LOCAL`: no bus, no unit address. The `reg` column is the",
                    "OPTA channel, and `LocalInputs` publishes it at PHASE_INPUT.",
                    ""]
        else:
            out += [f"### Unit {unit}: {d['tag'] if d else 'UNDECLARED'}", ""]
            if m:
                limit = (f"{m['size']} coils" if m["shape"] == "relay_board"
                         else f"{m['size']} registers")
                out += [f"{m['desc']} ({m['shape']}), at most {limit} per request"
                        + (f", addresses aligned to {m['align']}"
                           if m.get("align") else "") + ".", ""]
            if d and d.get("notes"):
                out += [d["notes"] + ".", ""]
        out += _kept(["name", "dir", "fc", "at", "n", "unit", "description"],
                     [[r["name"], FIELD_DIR[k], r.get("fc", "")]
                      + list(_field_addr(k, r))
                      + [r.get("unit", ""), r.get("desc", "")]
                      for k, r in by_unit[unit]])
    return out


def _kept(header, body):
    """The table without its empty columns. One device is one KIND of thing, so
    a coil board has no function code and no engineering unit on any row, and a
    column of dashes is width taken from the descriptions."""
    keep = [i for i in range(len(header))
            if header[i] == "name" or any(r[i] for r in body)]
    return md_table([header[i] for i in keep],
                    [[r[i] for i in keep] for r in body])


def orphans_doc(site, rows):
    """Every row with an end missing, and what the missing end means.

    The same computation the gate runs (vars_programs.orphans), so the document
    and `check-vars` cannot disagree about what an orphan is. An empty section
    is not printed - a report that says "none" every time stops being read.
    """
    d = os.path.join(ROOT, "src", "sites", site, "tables")
    progs = vars_programs.load(os.path.join(d, "programs.toml"))
    vars_programs.expand(progs, rows, lambda _m: None)
    if not progs:
        return []
    producers, readers, shared = set(), set(), set()
    for p in progs:
        if p.get("health"):
            producers.add(p["health"])
        for member, sec, row, idx in p["conns"]:
            if sec == "out":
                producers.add(row)
            elif sec == "inout":
                shared.add(row)
            if sec in ("in", "inout"):
                readers.add(row)

    lib = {r["name"] for _, r in load_set(
        os.path.join(ROOT, "src", "libbotanik", "vars", "tables"), "libbotanik")}
    o = vars_programs.orphans(
        {n: (k, r) for n, (k, r) in rows.items()}, producers, readers, shared,
        vars_gen.site_mentions(site), vars_gen.cal_params(
            [(k, r) for k, r in rows.values()]), lib)

    # A third list, and a NOTE rather than either error: a row a law computes
    # and no law reads. Most are terminal by design - an instrument's mirror,
    # an actuator's position, a totalizer - and exist for the page and the bus.
    # The one that matters is an INTERMEDIATE: a quantity the control system
    # derives, publishes, and then bypasses by keeping its own copy. Those are
    # invisible to an operator and impossible to force, so the rule is that
    # whatever a law decides on, it decides on the ROW.
    bypassed = [[n, k, rows[n][1].get("desc", "")]
                for n, (k, r) in sorted(rows.items())
                if k in ("SV", "SVE", "SVR", "G", "GR")
                and n in (producers | shared) and n not in readers]

    body = ([[n, k, "nothing writes it", why] for n, k, why in o["unwritten"]]
            + [[n, k, "nothing reads it", why] for n, k, why in o["unread"]])
    if not body and not bypassed:
        return []
    out = ["## Orphans", "",
           f"{len(body)} rows with one end missing. A row nothing writes is an",
           "ERROR and stops the build; a parameter nothing reads is a NOTE,",
           "because a threshold declared ahead of the law that will read it is",
           "how this site gets built. The other kind - a parameter whose law",
           "was rewritten around it - looks identical from inside the table,",
           "which is why the list is printed rather than kept.", ""]
    out += md_table(["row", "kind", "missing", "what that means"], body)

    if bypassed:
        out += ["### Computed, and no law reads it", "",
                f"{len(bypassed)} rows. Most are TERMINAL by design - an "
                "instrument's mirror, an actuator's position, a totalizer - "
                "and exist for the page and the field bus. What to look for "
                "here is an INTERMEDIATE: a quantity the control system "
                "derives, publishes, and then bypasses by keeping its own "
                "copy. That row cannot be forced and does not steer anything, "
                "so it lies the moment an operator leans on it. Whatever a law "
                "decides on, it should decide on the ROW.", ""]
        out += md_table(["row", "kind", "what it is"], bypassed)
    return out


def programs_doc(site, rows):
    """The site's control programs, and the networks their signals form."""
    path = os.path.join(ROOT, "src", "sites", site, "tables", "programs.toml")
    progs = vars_programs.load(path)
    vars_programs.expand(progs, rows, lambda _m: None)
    if not progs:
        return ["## Programs", "",
                "No `tables/programs.toml`, so the control programs are still",
                "hand-written in C++ and cannot be reported here.", ""]

    out = ["## Programs", "",
           f"{len(progs)} programs, "
           f"{sum(len(p['conns']) for p in progs)} connections. Declared in",
           "`tables/programs.toml`, which is the IEC 61131-3 CONFIGURATION: it",
           "joins each POU's interface to the global table.", "",
           "`in` may not be written; `out` has exactly one producer, checked",
           "before the firmware is built; `inout` is read-modify-write, shared",
           "by declaration.", ""]
    out += md_table(["program", "class", "task", "what it does"],
                    [[p["name"], f"`{p['class']}`", p["task"], p.get("desc", "")]
                     for p in progs])

    produced = {}
    for p in progs:
        for member, sec, row, idx in p["conns"]:
            if sec in ("out", "inout"):
                produced.setdefault(row, []).append(p["name"])

    # A page each: the per-program table is what an engineer reads at the
    # cabinet, and a binder page per program is the form that gets printed.
    for p in progs:
        out += [PAGE_BREAK, "",
                f"### {p['name']}", "",
                f"`{p['class']}`, dispatched at {p['task']}.", ""]
        # What the program is for, from the table. The same sentence is the
        # generated class's @brief, so the binder page and the header cannot
        # describe the program differently.
        if p.get("desc"):
            out += [p["desc"], ""]
        for para in (p.get("notes") or "").strip().split("\n\n"):
            if para.strip():
                out += [" ".join(para.split()), ""]
        # One line per MEMBER, not per connection: an eight-tray array is one
        # thing the program declares, and the variable tables above are where
        # each of its rows is mapped to a device and a register.
        body, seen = [], {}
        for member, sec, row, idx in p["conns"]:
            key = (member, sec)
            if key not in seen:
                seen[key] = len(body)
                kind = rows.get(row, ("?", {}))[0]
                desc = rows.get(row, ("?", {}))[1].get("desc", "")
                body.append([member, {"in": "VAR_INPUT", "out": "VAR_OUTPUT",
                                      "inout": "VAR_IN_OUT"}[sec], [row], kind,
                             re.sub(r"\d+", "n", desc)])
            else:
                body[seen[key]][2].append(row)
        for line in body:
            if len(line[2]) > 1:
                line[0] += f"[{len(line[2])}]"
            line[2] = row_range(line[2])
        out += md_table(["member", "section", "row", "kind", "description"], body)

    out += _networks(progs)
    return out


def _signal_graph(progs):
    """(edges, networks): who produces what another reads, and the disjoint
    groups that makes. A network is what IEC 61131-3 calls one: a set of
    programs that can only affect each other."""
    produced = {}
    for p in progs:
        for member, sec, row, idx in p["conns"]:
            if sec in ("out", "inout"):
                produced.setdefault(row, []).append(p["name"])

    edges = set()
    for p in progs:
        for member, sec, row, idx in p["conns"]:
            if sec not in ("in", "inout"):
                continue
            for src in produced.get(row, []):
                if src != p["name"]:
                    edges.add((src, p["name"], row))

    parent = {p["name"]: p["name"] for p in progs}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b, _ in edges:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    nets = {}
    for p in progs:
        nets.setdefault(find(p["name"]), []).append(p["name"])
    return edges, produced, nets


def _networks(progs):
    """The signal flow, as a FOREST.

    A network is what IEC 61131-3 calls one: a set of programs that can only
    affect each other. Reported as the edges themselves - who produces what
    another reads - because that is the fact, and a reader chasing one signal
    wants the row name rather than a picture of the shape.
    """
    edges, produced, nets = _signal_graph(progs)

    out = ["## Signal networks", "",
           f"{len(nets)} networks over {len(progs)} programs and {len(edges)}",
           "connections. A network is a set of programs that can reach each",
           "other through the table; programs in different networks cannot",
           "affect one another, whatever order they run in.", ""]
    for i, (root, members) in enumerate(sorted(nets.items(),
                                               key=lambda kv: (-len(kv[1]), kv[0])), 1):
        inside = [e for e in edges if e[0] in members and e[1] in members]
        out += [f"### Network {i}: " + ", ".join(sorted(members)), ""]
        if inside:
            out += md_table(["produces", "row", "is read by"],
                            [[a, row, b] for a, b, row in sorted(inside)])
            continue
        # A network of ONE still reads and writes rows - just none another
        # program touches. The same question deserves the same table, with the
        # field on the other end of it instead of a program.
        out += ["Stands alone: it produces nothing another program reads, and",
                "reads nothing another produces. What it does read and write:",
                ""]
        out += _lone_table(members[0], progs)
    return out


def _lone_table(name, progs):
    """One program's own rows, as the produces/consumes table a network of
    several gets. `-` on an end means the field rather than a program."""
    p = next((q for q in progs if q["name"] == name), None)
    if p is None:
        return []
    body, seen = [], {}
    for member, sec, row, idx in p["conns"]:
        key = (member, sec)
        if key in seen:
            body[seen[key]][1].append(row)
            continue
        seen[key] = len(body)
        produced = sec in ("out", "inout")
        body.append([name if produced else "-", [row],
                     "-" if produced else name])
    return md_table(["produces", "row", "is read by"],
                    [[a, row_range(rows), b] for a, rows, b in body])


# --- PDF ------------------------------------------------------------------


def to_pdf(md_text, path, title):
    """The Markdown as a paginated document. reportlab only; absent is not an
    error, because the Markdown is the output that always lands."""
    try:
        from reportlab.lib import colors
        from reportlab.lib.pagesizes import A4
        from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
        from reportlab.lib.units import mm
        from reportlab.pdfbase.pdfmetrics import stringWidth
        from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer,
                                        Table, TableStyle, PageBreak)
    except ImportError:
        print("  reportlab not installed - Markdown only "
              "(pip install reportlab for the PDF)")
        return False

    page_w = A4[0] - 26 * mm          # what the frame below leaves for content

    ss = getSampleStyleSheet()
    # keepWithNext on every heading: a heading alone at the foot of a page is
    # the one layout fault a reader actually trips over.
    h1 = ParagraphStyle("h1", parent=ss["Heading1"], fontSize=17, spaceAfter=8,
                        keepWithNext=1)
    h2 = ParagraphStyle("h2", parent=ss["Heading2"], fontSize=13, spaceBefore=14,
                        spaceAfter=4, keepWithNext=1)
    h3 = ParagraphStyle("h3", parent=ss["Heading3"], fontSize=10.5,
                        spaceBefore=10, spaceAfter=3, keepWithNext=1)
    body = ParagraphStyle("body", parent=ss["BodyText"], fontSize=8.5,
                          leading=11.5, spaceAfter=5, spaceBefore=0)
    bullet = ParagraphStyle("bullet", parent=body, leftIndent=9,
                            bulletIndent=1, spaceAfter=2)
    small = ParagraphStyle("small", parent=body, fontSize=6.6, leading=8,
                           spaceAfter=0)

    def esc(t):
        """Markdown's two inline marks into reportlab's, XML-escaped first."""
        t = (t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
              .replace("\\|", "|"))
        t = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", t)
        t = re.sub(r"\*(.+?)\*", r"<i>\1</i>", t)
        return re.sub(r"`(.+?)`", r"<font face='Courier'>\1</font>", t)

    story, pending = [], []
    fenced = [False]

    def widths(head, rows):
        """Column widths from the CONTENT, so a description column takes the
        slack instead of squeezing the numbers next to it. reportlab's own
        auto-fit divides the page evenly and makes a `reg` column as wide as a
        sentence."""
        cells = [[head[i]] + [r[i] for r in rows if i < len(r)]
                 for i in range(len(head))]
        # Measured, not counted: a character-width guess is what puts
        # "maintainer" on two lines in a column that looked wide enough.
        pad = 7.0
        def w_of(t):
            return stringWidth(t, "Helvetica", small.fontSize)

        want = [max(w_of(c) for c in col) + pad for col in cells]
        # The longest WORD is what a column cannot be narrower than: wrapping a
        # sentence is fine, splitting a word across two lines is not.
        # Capped at an even share: one 31-character C++ expression in a `default`
        # cell would otherwise claim the floor for the whole table and push
        # every short column back to nothing. That token wraps; "maintainer"
        # does not.
        cap = page_w / len(head)
        floor = [min(cap, max([w_of(w) for c in col for w in c.split()] or [0])
                     + pad) for col in cells]
        # Square-rooted: a cell ten times longer needs more room, not ten times
        # the room, because it wraps.
        weight = [max(1.0, x) ** 0.5 for x in want]
        span = page_w
        total = sum(weight)
        if sum(floor) >= span:
            return [span * x / total for x in weight]
        # Everything gets its floor; the slack is what the weights divide.
        slack = span - sum(floor)
        return [floor[i] + slack * weight[i] / total for i in range(len(head))]

    def flush():
        if not pending:
            return
        head, rows = pending[0], pending[1:]
        data = [[Paragraph(f"<b>{esc(c)}</b>", small) for c in head]] + \
               [[Paragraph(esc(c), small) for c in r] for r in rows]
        t = Table(data, repeatRows=1, hAlign="LEFT",
                  colWidths=widths(head, rows))
        t.setStyle(TableStyle([
            ("GRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#bcc4b8")),
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#e8ece6")),
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 3),
            ("RIGHTPADDING", (0, 0), (-1, -1), 3),
            ("TOPPADDING", (0, 0), (-1, -1), 2),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
        ]))
        story.append(t)
        story.append(Spacer(1, 6))
        pending.clear()

    para = []

    def flush_para():
        """Markdown wraps its source; a document does not. Consecutive lines are
        ONE paragraph, or every wrapped sentence lands as its own block with a
        gap through the middle of it."""
        if not para:
            return
        text = " ".join(para)
        para.clear()
        if text.startswith("- "):
            story.append(Paragraph(esc(text[2:]), bullet, bulletText="\u2022"))
        else:
            story.append(Paragraph(esc(text), body))

    for line in md_text.split("\n"):
        if line.strip() == PAGE_BREAK:
            flush_para()
            flush()
            if story:
                story.append(PageBreak())
            continue
        if line.startswith("```"):
            fenced[0] = not fenced[0]
            flush_para()
            continue
        if fenced[0]:
            continue
        if line.startswith("|"):
            flush_para()
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if all(set(c) <= set("-: ") for c in cells):
                continue
            pending.append(cells)
            continue
        flush()
        if line.startswith("#"):
            flush_para()
            level = len(line) - len(line.lstrip("#"))
            story.append(Paragraph(esc(line[level + 1:]),
                                   {1: h1, 2: h2}.get(level, h3)))
        elif not line.strip():
            flush_para()
        elif line.startswith("- ") and para:
            # A bullet ENDS the paragraph before it, and starts its own.
            flush_para()
            para.append(line)
        else:
            para.append(line.strip())
    flush_para()
    flush()

    SimpleDocTemplate(path, pagesize=A4, title=title,
                      leftMargin=14 * mm, rightMargin=12 * mm,
                      topMargin=14 * mm, bottomMargin=14 * mm).build(story)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--schema", action="store_true",
                    help="write the library reference into docs/integration/")
    ap.add_argument("--site", help="write that site's report")
    ap.add_argument("--out", help="where to write (default: alongside the source)")
    ap.add_argument("--no-pdf", action="store_true")
    a = ap.parse_args()

    if not a.schema and not a.site:
        ap.error("one of --schema or --site")

    if a.schema:
        path = a.out or os.path.join(ROOT, "docs", "integration", "tables_schema.md")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w", encoding="utf-8").write(schema_doc())
        print(f"  {os.path.relpath(path, ROOT)}: written")

    if a.site:
        md = site_doc(a.site)
        base = a.out or os.path.join(ROOT, "docs", "generated", f"{a.site}_report")
        os.makedirs(os.path.dirname(base), exist_ok=True)
        open(base + ".md", "w", encoding="utf-8").write(md)
        print(f"  {os.path.relpath(base + '.md', ROOT)}: written")
        if not a.no_pdf and to_pdf(md, base + ".pdf", f"{a.site}: variables and programs"):
            print(f"  {os.path.relpath(base + '.pdf', ROOT)}: written")


if __name__ == "__main__":
    main()
