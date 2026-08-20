#!/usr/bin/env python3
"""The IEC 61131-3 configuration: programs, and what each connects to.

The variable tables say what EXISTS; this says who reads and who produces. That
is what makes single-ownership checkable before the firmware is built, rather
than being noticed at bring-up - or, for a row only a fault path drives, months
in.
"""

import os
import sys

SECTIONS = ("in", "out", "inout")
TASKS = ("FAST", "SLOW", "BACKGROUND")


def load(path):
    """Every program as {name, class, task, conns: [(member, section, row, idx)]}."""
    try:
        import tomllib
    except ImportError:                       # pragma: no cover
        sys.exit("programs.toml needs Python 3.11+ for tomllib")
    if not os.path.exists(path):
        return []
    with open(path, "rb") as f:
        doc = tomllib.load(f)
    out = []
    for p in doc.get("program", []):
        conns = []
        for sec in SECTIONS:
            for member, rows in (p.get(sec) or {}).items():
                if isinstance(rows, str):
                    conns.append((member, sec, rows, None))
                else:
                    for i, r in enumerate(rows):
                        conns.append((member, sec, r, i))
        out.append({"name": p.get("name", "?"), "class": p.get("class", "?"),
                    "task": p.get("task", "?"), "health": p.get("health", ""),
                    "conns": conns,
                    "arrays": {m for sec in SECTIONS
                               for m, v in (p.get(sec) or {}).items()
                               if isinstance(v, list)}})
    return out


def check(programs, rows, err, mentioned=(), cal_params=(), library=()):
    """Every rule the configuration has to satisfy.

    @p rows is the effective table as {name: (scope, row)}. @p mentioned is the
    row names the site's own hand-written C++ spells out - `Site::simulate()`
    animates rows by name, and a row it writes is produced whatever this file
    says. @p cal_params is the parameters an analog calibration names.
    @p library is every name libbotanik declares: a site row RESTATING one is
    read and written by library code, which has no entry in this file.
    """
    seen_names, seen_classes = {}, {}
    producers = {}          # row -> the program that declares it an out
    readers = set()         # rows any program declares an in
    shared = set()          # rows declared inout by anyone

    for p in programs:
        where = f"programs.toml [{p['name']}]"
        if p["name"] in seen_names:
            err(f"{where}: two programs are called '{p['name']}'")
        seen_names[p["name"]] = p
        if p["class"] in seen_classes:
            err(f"{where}: class {p['class']} is already instantiated as "
                f"'{seen_classes[p['class']]}'")
        seen_classes[p["class"]] = p["name"]
        if p["task"] not in TASKS:
            err(f"{where}: task '{p['task']}' is not one of " + ", ".join(TASKS))

        if p.get("health"):
            row = p["health"]
            if row not in rows:
                err(f"{where}: health = '{row}', which is not a row")
            elif row in producers and producers[row] != p["name"]:
                err(f"{where}: '{row}' is already produced by "
                    f"'{producers[row]}'")
            else:
                producers[row] = p["name"]

        members = {}
        for member, sec, row, idx in p["conns"]:
            key = (member, idx)
            if key in members:
                err(f"{where}: member '{member}' is connected twice")
            members[key] = row

            if row not in rows:
                err(f"{where}: {member} names '{row}', which is not a row")
                continue
            scope, r = rows[row]

            # Every connection becomes a typed reference, and T_STR has none:
            # its text lives in the bank's pool, so it can be neither read nor
            # driven through one.
            if ctype(scope, r) is None:
                err(f"{where}: '{row}' is a text row, which no typed reference "
                    f"reaches - read it with Bank::str_get()")

            if sec == "out":
                # Exactly one producer. This is the whole point of the file: the
                # runtime finds a second author at bring-up, this finds it before
                # the firmware is built.
                if row in producers and producers[row] != p["name"]:
                    err(f"{where}: '{row}' is already produced by "
                        f"'{producers[row]}'. Declare it [inout] in both if the "
                        f"sharing is the design.")
                producers[row] = p["name"]

                if scope in ("MI", "AI", "DI"):
                    err(f"{where}: '{row}' is an INPUT; the next scan overwrites "
                        f"anything a program drives into it")
                if scope in ("P", "PE", "PS"):
                    err(f"{where}: '{row}' is a PARAMETER. drive() writes the "
                        f"value but skips the unsaved mark, so it never reaches "
                        f"flash - declare it [inout] and write it with "
                        f"param_set()")
            elif sec == "inout":
                shared.add(row)
            if sec in ("in", "inout"):
                readers.add(row)

    o = orphans(rows, producers, readers, shared, mentioned, cal_params, library)
    _unproduced(o, err)
    _unused(o, err)
    return o


# Which scopes SOMETHING inside the firmware has to write, and which have to be
# read. An input is written by the field and an output is read by the driver, so
# each kind is only asked the question it can answer.
WRITTEN = ("A", "G", "GR", "SV", "SVR", "SVE", "MO", "AO", "DO", "RO")
READ    = ("P", "PE", "PS")


def orphans(rows, producers, readers, shared, mentioned=(), cal_params=(),
            library=()):
    """Every row with an end missing: {'unwritten': [...], 'unread': [...]}.

    Each entry is (name, scope, why). The one definition of what an orphan is,
    so the gate and the report cannot disagree about it.
    """
    out = {"unwritten": [], "unread": []}
    for name, (scope, r) in sorted(rows.items()):
        if r.get("_set") == "libbotanik" or name in library:
            continue          # the library's row, restated or not: it owns it
        written = (name in producers or name in shared or name in mentioned)
        read    = (name in readers or name in shared or name in mentioned
                   or name in cal_params)
        if scope in WRITTEN and not written:
            out["unwritten"].append((name, scope, WHY_UNWRITTEN.get(scope, "")))
        if scope in READ and not read:
            out["unread"].append((name, scope, "no law reads it"))
    return out


# What the missing end MEANS, per kind: the same sentence the gate prints and
# the report puts beside the row.
WHY_UNWRITTEN = {
    "A":   "an alarm nothing raises: it can never go true",
    "G":   "computed, but nothing computes it",
    "GR":  "computed, but nothing computes it",
    "SV":  "publishes its declared default for ever",
    "SVR": "publishes its declared default for ever",
    "SVE": "publishes its declared default for ever",
    "MO":  "an output nothing drives: it sits at its safe state",
    "AO":  "an output nothing drives: it sits at its safe state",
    "DO":  "an output nothing drives: it sits at its safe state",
    "RO":  "an output nothing drives: it sits at its safe state",
}


def _unproduced(o, err):
    """A row nothing writes.

    It reads its declared default for ever, which on a page looks exactly like a
    working reading, and on a coil looks exactly like a plant that is not
    calling for anything. An ERROR: whatever the row is for, the answer is
    either a program or deleting it.

    Only the SITE's rows are asked about - a library row is written by library
    code, which has no entry here - and only the kinds SOMETHING has to write
    (@ref WRITTEN).
    An input row is written by the field.
    """
    for name, _scope, why in o["unwritten"]:
        err(f"{name}: {why}")


def _unused(o, err):
    """A PARAMETER nothing reads: an operator setting that changes nothing.

    An ERROR, like a row nothing writes. It was a note for as long as the site
    had thresholds waiting on laws; every one of them has a law now, and the
    two halves of the rule are the same rule - a variable with one end missing
    is a variable that does not work.

    Only parameters. Every other kind is either written by the field, published
    to MQTT and the pages, or scanned by the alarm controller, and a reading
    nobody's law uses is still a recorded measurement.
    """
    for name, _scope, _why in o["unread"]:
        err(f"{name}: a parameter nothing reads - an operator setting that "
            f"changes nothing")


# The C++ type a row is reached through. A factory that fixes its type answers
# from the factory; everything else from the row's own `type` column.
IMPLIED = {"A": "bool", "DI": "bool", "DO": "bool", "RO": "bool",
           "AI": "float", "AO": "float", "PE": "uint8_t", "SVE": "uint8_t"}
TYPES = {"T_BOOL": "bool", "T_F32": "float",
         "T_I8": "int8_t",  "T_I16": "int16_t",  "T_I32": "int32_t",  "T_I64": "int64_t",
         "T_U8": "uint8_t", "T_U16": "uint16_t", "T_U32": "uint32_t", "T_U64": "uint64_t"}
# Section -> the reference template and the resolver that claims for it.
REFS = {"in": ("RefIn", "resolve_in"), "out": ("RefOut", "resolve_out"),
        "inout": ("RefInOut", "resolve_inout")}
SECTION_DOC = {"in": "VAR_INPUT", "out": "VAR_OUTPUT", "inout": "VAR_IN_OUT"}


def ctype(kind, row):
    """The C++ type of a row, or None when it has none: T_STR lives in the
    bank's pool, and a reference to it could neither be read nor driven."""
    if kind in IMPLIED:
        return IMPLIED[kind]
    return TYPES.get(row.get("type", ""))


def _members(p, rows):
    """One entry per declared member: (section, name, ctype, [row, ...])."""
    out, seen = [], {}
    for member, sec, row, idx in p["conns"]:
        if member not in seen:
            seen[member] = len(out)
            out.append([sec, member, None, []])
        e = out[seen[member]]
        e[3].append(row)
        if row in rows:
            e[2] = ctype(*rows[row])
    return [e for e in out if e[2]]


def _struct(p, rows):
    """The Refs struct for one program: its members, grouped by section."""
    ms = _members(p, rows)
    out = [f"/** @brief The rows `{p['name']}` connects to, one typed reference",
           f" *  each. {p['class']}, dispatched {p['task']}. */",
           f"struct {p['class']} {{", ""]
    for sec in SECTIONS:
        group = [m for m in ms if m[0] == sec]
        if not group:
            continue
        tmpl, _ = REFS[sec]
        decls = [(f"Vars::{tmpl}<{m[2]}>",
                  m[1] + (f"[{len(m[3])}]" if len(m[3]) > 1 else "")) for m in group]
        width = max(len(d[0]) for d in decls)
        out.append(f"    /** @name {SECTION_DOC[sec]}")
        out.append("     *  @{ */")
        out += [f"    {t.ljust(width)} {n};" for t, n in decls]
        out += ["", "    /** @} */", ""]
    if p.get("health"):
        out += ["    /** @name Health",
                "     *  Where this program's health is published. Written by a",
                "     *  SUPERVISOR walking every program, not by the program: a",
                "     *  program that is not being activated is exactly when a",
                "     *  reader wants the row. @see Plc::publish_health()",
                "     *  @{ */",
                "    Vars::RefOut<uint8_t> health;", "",
                "    /** @} */", ""]
    out += ["    /** @brief The instance name and the rate class programs.toml",
            "     *  declares. The constructor takes them from here, so neither can",
            "     *  drift from the configuration. */",
            f'    static const char* name() {{ return "{p["name"]}"; }}',
            f"    static Tasks::GroupId group() {{ return Tasks::GROUP_{p['task']}; }}",
            "",
            "    /** @brief Resolve every name. Runs inside the program's writer",
            "     *  window, so an output is CLAIMED here.",
            "     *  @param owner the program this belongs to: a declared health",
            "     *         row is handed to it here. */",
            "    void bind(Vars::Bank& io, Plc::Program& owner);", "};", ""]
    return out


def _bind(p, rows):
    """The bind() body: one resolve per connection, in declaration order."""
    ms = _members(p, rows)
    lhs, rhs = [], []
    for sec, member, cpp, names in ms:
        _, resolve = REFS[sec]
        for i, row in enumerate(names):
            lhs.append(member + (f"[{i}]" if len(names) > 1 else ""))
            rhs.append(f'io.{resolve}<{cpp}>("{row}");')
    if p.get("health"):
        lhs.append("health")
        rhs.append(f'io.resolve_out<uint8_t>("{p["health"]}");')
    if not lhs:
        return [f"inline void {p['class']}::bind(Vars::Bank& io, Plc::Program&)"
                f" {{ (void)io; }}", ""]
    width = max(len(x) for x in lhs)
    out = [f"inline void {p['class']}::bind(Vars::Bank& io, Plc::Program& owner) {{"]
    out += [f"    {l.ljust(width)} = {r}" for l, r in zip(lhs, rhs)]
    out += ["    owner.set_health_row(health);"] if p.get("health") \
        else ["    (void)owner;"]
    return out + ["}", ""]


def generate(programs, rows, banner):
    """programs.gen.h: the Refs struct and the bind() body of every program."""
    out = banner + ["#ifndef SITE_PROGRAMS_GEN_H", "#define SITE_PROGRAMS_GEN_H", "",
                    "#include <cstdint>", "", '#include "libbotanik.h"', "",
                    "namespace Site { namespace Refs {", "",
                    "using namespace Botanik;", ""]
    for p in programs:
        out += _struct(p, rows)
    out += ["/** @name Binding",
            " *  Out of line, so the structs above read as the interface.",
            " *  @{ */", ""]
    for p in programs:
        out += _bind(p, rows)
    out += ["/** @} */", "", "}}  // namespace Site::Refs", "",
            "#endif  // SITE_PROGRAMS_GEN_H"]
    return "\n".join(out) + "\n"
