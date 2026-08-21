#!/usr/bin/env python3
"""The IEC 61131-3 configuration: programs, and what each connects to.

The variable tables say what EXISTS; this says who reads and who produces. That
is what makes single-ownership checkable before the firmware is built, rather
than being noticed at bring-up - or, for a row only a fault path drives, months
in.
"""

import os
import re
import sys
import textwrap

SECTIONS = ("in", "out", "inout")
# A program's one-line summary, as long as two wrapped @brief lines.
DESC_MAX = 160
TASKS = ("FAST", "SLOW", "BACKGROUND")


# The library's function block vocabulary, and the feature flag each needs. A
# block named without an `include` has to be one of these; one WITH an include
# is the site's own and is emitted verbatim.
PLC_BLOCKS = {
    "Ton": "PLC_BLK_BASE", "Tof": "PLC_BLK_BASE", "Tp": "PLC_BLK_BASE",
    "RTrig": "PLC_BLK_BASE", "FTrig": "PLC_BLK_BASE", "Sr": "PLC_BLK_BASE",
    "Rs": "PLC_BLK_BASE",
    "Hysteresis": "PLC_BLK_BASE", "Hysteresis3": "PLC_BLK_BASE",
    "Staircase": "PLC_BLK_BASE", "Ramp": "PLC_BLK_BASE",
    "Deadband": "PLC_BLK_BASE", "Integral": "PLC_BLK_BASE",
    "Derivative": "PLC_BLK_BASE", "Actuator": "PLC_BLK_BASE",
    "Pid": "PLC_BLK_BASE",
    "Ewma": "PLC_BLK_FILTERS", "MedianOf3": "PLC_BLK_FILTERS",
    "MovingAverage": "PLC_BLK_FILTERS",
}

# Plc::Program's own surface. A row or block member takes the name it is given
# in the table, in the program's scope, so one of these would shadow the base.
RESERVED = frozenset((
    "bind", "on_bind", "run", "observe", "parking", "degraded", "faulted",
    "group", "slot", "set_slot", "io", "name", "health", "health_row",
    "set_health_row", "set_inputs", "set_safe", "is_parked", "register_tasks",
    "late_ms", "late_max_ms", "safe_requested", "disable_requested",
    "inhibit_requested", "request_safe", "request_disable",
))


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
        conns, counts = [], {}
        for sec in SECTIONS:
            for member, rows in (p.get(sec) or {}).items():
                if isinstance(rows, dict):
                    counts[(sec, member)] = rows.get("count")
                    rows = rows.get("rows", "")
                if isinstance(rows, str):
                    conns.append((member, sec, rows, None))
                else:
                    for i, r in enumerate(rows):
                        conns.append((member, sec, r, i))
        out.append({"name": p.get("name", "?"), "class": p.get("class", "?"),
                    "task": p.get("task", "?"), "health": p.get("health", ""),
                    "stage": p.get("stage", ""), "blocks": _blocks(p),
                    "vars": _decls(p.get("var")), "fns": _decls(p.get("fn")),
                    "hooks": list(p.get("hooks") or []),
                    "desc": p.get("desc", ""), "notes": p.get("notes", ""),
                    "conns": conns, "counts": counts,
                    "arrays": {m for sec in SECTIONS
                               for m, v in (p.get(sec) or {}).items()
                               if isinstance(v, list)}})
    return out


def _decls(section):
    """A [program.var] or [program.fn] section: name -> {type/sig, init, doc}.

    A bare string is the type (or the signature); the table form carries the
    initial value and the one-line comment the declaration gets.
    """
    out = []
    for name, spec in (section or {}).items():
        if isinstance(spec, str):
            spec = {"type": spec}
        out.append({"name": name, "type": spec.get("type", ""),
                    "count": int(spec.get("count", 0) or 0),
                    "init": spec.get("init", ""), "doc": spec.get("doc", "")})
    return out


def _blocks(p):
    """The POU's VAR section: its function block instances.

    `name = "Type"`, or the table form for the ones that need more: `count`
    makes it an array, `include` is the site header a block of the site's own
    lives in and is what marks it as NOT one of the library's.
    """
    out = []
    for name, spec in (p.get("block") or {}).items():
        if isinstance(spec, str):
            spec = {"type": spec}
        out.append({"name": name, "type": spec.get("type", ""),
                    "count": int(spec.get("count", 0) or 0),
                    "include": spec.get("include", ""),
                    "doc": spec.get("doc", "")})
    return out


def expand(programs, rows, err):
    """Turn every `prefix*suffix` member into the rows it matches, in order.

    `*` is one or more DIGITS, and nothing else. An array member indexes
    numbered instances - eight trays, four windows - so the number is what
    varies, and matching anything else would take `irr_traysPerDay` for a tray.
    It also settles the ORDER, which is the part a wildcard has to get right:
    numeric, so tray10 follows tray9 rather than tray1.

    The generated bind() lists what it resolved to, and that file is committed,
    so an expansion that changes shows up as a diff rather than as behaviour.
    """
    for p in programs:
        conns = []
        for member, sec, row, idx in p["conns"]:
            if "*" not in row:
                conns.append((member, sec, row, idx))
                continue
            where = f"programs.toml [{p['name']}]: {sec} '{member}'"
            head, _star, tail = row.partition("*")
            hits = []
            for name in rows:
                if name.startswith(head) and name.endswith(tail) \
                        and len(name) > len(head) + len(tail):
                    cap = name[len(head):len(name) - len(tail)]
                    if cap.isdigit():
                        hits.append((int(cap), name))
            if not hits:
                err(f"{where}: '{row}' matches no row")
                continue
            if len(hits) == 1:
                err(f"{where}: '{row}' matches only {hits[0][1]} - name the row")
                continue
            hits.sort()
            want = p["counts"].get((sec, member))
            if want is not None and want != len(hits):
                err(f"{where}: '{row}' matches {len(hits)} rows, not the {want} "
                    f"it declares")
            p["arrays"].add(member)
            for i, (_h, name) in enumerate(hits):
                conns.append((member, sec, name, i))
        p["conns"] = conns


def check(programs, rows, err, mentioned=(), cal_params=(), library=(),
          features=None):
    """Every rule the configuration has to satisfy.

    @p rows is the effective table as {name: (scope, row)}. @p mentioned is the
    row names the site's own hand-written C++ spells out - `Site::simulate()`
    animates rows by name, and a row it writes is produced whatever this file
    says. @p cal_params is the parameters an analog calibration names.
    @p library is every name libbotanik declares: a site row RESTATING one is
    read and written by library code, which has no entry in this file.
    @p features is the site's BOTANIK_FEATURE_* map, for the block families.
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

        # What the program is FOR, in the table rather than in a comment: it
        # becomes the generated class's @brief and the report's page heading,
        # so the two cannot say different things.
        if not p["desc"]:
            err(f"{where}: no desc - what does this program do?")
        elif "\n" in p["desc"]:
            err(f"{where}: desc is a single line; the paragraphs go in `notes`")
        elif len(p["desc"]) > DESC_MAX:
            err(f"{where}: desc is {len(p['desc'])} chars, over {DESC_MAX}: it "
                f"has to read as one @brief")

        # Every program carries its own declaration part now, and `stage` is
        # the directory it lives in: there is no second shape to fall back to.
        if not p["stage"]:
            err(f"{where}: no stage - which directory under programs/ is it in?")
        elif not re.match(r"^[a-z][a-z0-9_]*$", p["stage"]):
            err(f"{where}: stage '{p['stage']}' becomes a directory name, so it "
                f"has to be lowercase letters, digits and underscores")

        # The table's names land in the program's own scope, so they answer to
        # the base class as well as to each other.
        declared = {}
        for what, names in (("member", [m for m, _s, _r, _i in p["conns"]]),
                            ("block", [b["name"] for b in p["blocks"]])):
            for n in names:
                if n in RESERVED:
                    err(f"{where}: {what} '{n}' is Plc::Program's own, and would "
                        f"shadow it")
                elif n.startswith("_"):
                    err(f"{where}: {what} '{n}' starts with an underscore, which "
                        f"belongs to the hand-written half")
                elif n in declared and declared[n] != what:
                    err(f"{where}: '{n}' is declared both as a {declared[n]} and "
                        f"as a {what}")
                declared[n] = what

        # The internals keep their underscore: the interface is bare, and the
        # two are told apart the same way they always were.
        for what, items in (("var", p["vars"]), ("fn", p["fns"])):
            for d in items:
                if not d["name"].startswith("_"):
                    err(f"{where}: {what} '{d['name']}' is internal, so its name "
                        f"starts with an underscore")
                elif d["name"] in {x["name"] for x in
                                   (p["fns"] if what == "var" else p["vars"])}:
                    err(f"{where}: '{d['name']}' is declared both as a var and "
                        f"as a fn")
                if not d["type"]:
                    err(f"{where}: {what} '{d['name']}' names no type")

        for h in p["hooks"]:
            if h not in ("observe", "on_bind"):
                err(f"{where}: hook '{h}' is not one of observe, on_bind")

        seen_blocks = set()
        for b in p["blocks"]:
            if b["name"] in seen_blocks:
                err(f"{where}: block '{b['name']}' is declared twice")
            seen_blocks.add(b["name"])
            if not b["type"]:
                err(f"{where}: block '{b['name']}' names no type")
            elif not b["include"] and "::" not in b["type"]:
                if b["type"] not in PLC_BLOCKS:
                    err(f"{where}: block '{b['name']}' is a {b['type']}, which is "
                        f"not one of the library's blocks - qualify it, or give "
                        f"a block of the site's own its `include`")
                elif features and not features.get(
                        "BOTANIK_FEATURE_" + PLC_BLOCKS[b["type"]], True):
                    err(f"{where}: block '{b['name']}' is a {b['type']}, and this "
                        f"site builds with {PLC_BLOCKS[b['type']]} off")

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


# Scopes whose value can stop being believable. A PARAMETER is always valid -
# it is an operator's number, not a measurement - so watching one would cost a
# check per activation to learn nothing.
WATCHABLE = ("MI", "AI", "DI", "G", "GR", "SV", "SVR", "SVE",
             "MO", "AO", "DO", "RO", "A")


def _watched(p, rows):
    """The rows a program READS that can go invalid, in declaration order.

    What the base sweeps before each activation to decide DEGRADED, so a law
    does not have to remember to ask about every input it reads.
    """
    out = []
    for member, sec, row, idx in p["conns"]:
        if sec == "out" or row not in rows:
            continue
        scope, _r = rows[row]
        if scope in WATCHABLE:
            out.append((member, idx, row))
    return out


def _is_array(p, member):
    """Whether @p member is connected to more than one row."""
    return sum(1 for m, _s, _r, _i in p["conns"] if m == member) > 1


def _program_list(programs):
    """SITE_PROGRAM_LIST: the site's programs, once, for site.h to expand.

    The member, its constructor init and its register_tasks() call are the same
    list three times over, and the three had already drifted: the declaration
    order, the registration order and this file disagreed about where sun_watch
    ran. One list is what stops that.
    """
    rows = [(p["class"], "_" + p["name"]) for p in programs]
    width = max(len(c) for c, _m in rows) + 1
    lines = ["#define SITE_PROGRAM_LIST(X)"]
    lines += [f"    X({(c + ',').ljust(width)} {m})" for c, m in rows]
    col = max(len(x) for x in lines) + 1
    return (["/** @name The site's programs",
             " *  In table order, which is EXECUTION order: a law reading a value",
             " *  computed this beat needs its producer registered first.",
             " *  site.h expands this for the members, the constructor init and",
             " *  the registration, so none of the three can drift.",
             " *  @{ */", ""]
            + [x.ljust(col) + "\\" for x in lines[:-1]] + [lines[-1]]
            + ["", "/** @} */", ""])


def snake(cls):
    """A class name as the directory and file name it gets: TempRHZone ->
    temp_rh_zone."""
    out = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", cls)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", out).lower()


def _brief(desc):
    """@brief, wrapped. The table's one line, as the doc block's first."""
    lines = textwrap.wrap(desc, 71)
    return [" * @brief " + lines[0]] + [" *        " + x for x in lines[1:]]


def _block_type(b):
    """A block's C++ type.

    A bare name is one of the library's function blocks and is reached through
    `Plc::`. Anything QUALIFIED is written as given: a library type from another
    namespace (`Sun::SunPositionTracker`) arrives with `libbotanik.h`, and a
    type of the site's own resolves in the enclosing `Site` and says where it
    lives through `include`.
    """
    return b["type"] if ("::" in b["type"] or b["include"]) else "Plc::" + b["type"]


def _sizes(p, rows):
    """Every array member's length, as (CONSTANT, count) in declaration order.

    A body loops over these rather than over a site constant, so what it walks
    and what the table declares cannot disagree.
    """
    out = []
    for sec, member, _cpp, names in _members(p, rows):
        if len(names) > 1:
            out.append(("N_" + member.upper(), len(names)))
    for b in p["blocks"]:
        if b["count"] > 1:
            out.append(("N_" + b["name"].upper(), b["count"]))
    return out


def _decl_group(title, decls, doc=()):
    """One @name group of member declarations, types column-aligned."""
    if not decls:
        return []
    width = max(len(t) for t, _n, _d in decls)
    named = max(len(n) for _t, n, _d in decls) + 1 if any(d for _t, _n, d in decls) else 0
    out = [f"    /** @name {title}"] + [f"     *  {x}" for x in doc] + ["     *  @{ */", ""]
    for t, n, d in decls:
        line = f"    {t.ljust(width)} {(n + ';').ljust(named)}"
        out.append((line + f"  ///< {d}") if d else line.rstrip())
    return out + ["", "    /** @} */"]


def program_header(p, rows, banner):
    """<class>.gen.h: the POU's whole declaration part.

    Everything the class has - its rows, its blocks, its internal variables, its
    helpers - is declared from the table, so the site writes one file: the .cpp
    that defines the bodies.
    """
    cls, guard = p["class"], "SITE_PROGRAM_" + snake(p["class"]).upper() + "_GEN_H"
    includes = []
    for b in p["blocks"]:
        # Site-root relative in the table, emitted relative to this file: a site
        # can live outside this tree, where an in-tree path would silently
        # resolve to the reference site instead.
        inc = f'#include "../../../{b["include"]}"'
        if b["include"] and inc not in includes:
            includes.append(inc)

    out = banner + [f"#ifndef {guard}", f"#define {guard}", "",
                    "#include <cstdint>", "", '#include "libbotanik.h"', "",
                    '#include "../../../vars.h"', ""]
    if includes:
        out += includes + [""]
    out += ["namespace Site {", "", "using namespace Botanik;", "", "/**"]
    out += _brief(p["desc"] or f"The {cls} POU.")
    out += [" *"]
    out += [" * " + x for x in textwrap.wrap(
        f"`{p['name']}`, dispatched {p['task']}. Everything here is declared in "
        f"tables/programs.toml; the body is hand-written beside this file. A "
        f"name without a leading underscore is a row or a block the POU is "
        f"connected to; one with is its own.", 74)]
    out += [" */",
            f"class {cls} final : public Plc::Program {{", ""]

    watched = _watched(p, rows)
    out += ["public:", ""]

    ms = _members(p, rows)
    for sec in SECTIONS:
        group = [m for m in ms if m[0] == sec]
        if not group:
            continue
        tmpl, _r = REFS[sec]
        out += _decl_group(SECTION_DOC[sec],
                           [(f"Vars::{tmpl}<{m[2]}>",
                             m[1] + (f"[{len(m[3])}]" if len(m[3]) > 1 else ""), "")
                            for m in group]) + [""]

    if p["blocks"] or p["vars"] or _sizes(p, rows) or watched:
        out += ["private:", ""]
    out += _decl_group("VAR - function blocks",
                       [(_block_type(b),
                         b["name"] + (f"[{b['count']}]" if b["count"] > 1 else ""),
                         b["doc"]) for b in p["blocks"]])
    if p["blocks"]:
        out += [""]
    out += _decl_group("VAR - what this law remembers",
                       [(v["type"],
                         v["name"] + (f"[{v['count']}]" if v["count"] else "")
                         + (f" = {v['init']}" if v["init"]
                            and "ctx." not in v["init"] else ""),
                         v["doc"]) for v in p["vars"]])
    if p["vars"]:
        out += [""]

    sizes = _sizes(p, rows)
    if sizes:
        width = max(len(n) for n, _c in sizes)
        out += ["    /** @name Sizes",
                "     *  How many rows or instances each array member has, from"
                " the",
                "     *  table. What a body loops over, so a site constant cannot"
                " get",
                "     *  out of step with what is declared.",
                "     *  @{ */", ""]
        out += [f"    static const uint8_t {n.ljust(width)} = {c};" for n, c in sizes]
        out += ["", "    /** @} */", ""]

    if watched:
        out += ["    /** @brief Every readable row this program connects to, as"
                " bare",
                "     *  indices: what the base sweeps for validity before each",
                "     *  activation. Parameters are not here - an operator's"
                " number is",
                "     *  always valid, and checking one would cost a walk to"
                " learn nothing. */",
                f"    Vars::VarRef _watched[{len(watched)}];", ""]

    ctx_init = [v for v in p["vars"] if "ctx." in (v["init"] or "")]
    init = "".join(f", {v['name']}({v['init']})" for v in ctx_init)
    out += ["public:",
            "    /** @brief The instance name and rate class programs.toml",
            "     *  declares, so neither can drift from the configuration. */",
            f"    {cls}(const SetupContext& ctx, Vars::Bank& io)",
            f'        : Program(ctx, "{p["name"]}", Tasks::GROUP_{p["task"]},'
            f' io){init} {{}}',
            ""]

    out += ["protected:",
            "    /** @brief Resolve every name. Runs inside the program's writer",
            "     *  window, so an output is CLAIMED here. */",
            "    void bind(Vars::Bank& io) override;", ""]
    hooks = [("run", "    /** @brief One activation of the law. */",
              "    void run(ProgramContext& c) override;")]
    if "observe" in p["hooks"]:
        hooks.append(("observe",
                      "    /** @brief What this program SAYS, in every state -"
                      " including\n     *  while the plant is parked. */",
                      "    void observe(ProgramContext& c) override;"))
    for _n, doc, decl in hooks:
        out += [doc, decl, ""]
    if "on_bind" in p["hooks"]:
        out += ["    /** @brief What the law wires once the rows are resolved: a"
                " block",
                "     *  taking a reference, a fail direction read off a row. */",
                "    void on_bind();", ""]
    if p["fns"]:
        out += ["private:", ""]
        width = max(len(f["type"].split("(")[0].strip()) for f in p["fns"])
        for f in p["fns"]:
            ret, rest = f["type"].split("(", 1)
            if f["doc"]:
                out += [f"    /** @brief {f['doc']} */"]
            out += [f"    {ret.strip().ljust(width)} {f['name']}({rest.strip()};"]
        out += [""]
    out = out[:-1] if out[-1] == "" else out
    out += ["};", ""]
    out += _program_bind(p, rows)
    out += ["}  // namespace Site", "", f"#endif // {guard}"]
    return "\n".join(out) + "\n"


def _program_bind(p, rows):
    """bind(), out of line so the class above reads as the declaration part."""
    cls = p["class"]
    lhs, rhs = [], []
    for sec, member, cpp, names in _members(p, rows):
        _t, resolve = REFS[sec]
        for i, row in enumerate(names):
            lhs.append(member + (f"[{i}]" if len(names) > 1 else ""))
            rhs.append(f'io.{resolve}<{cpp}>("{row}");')
    out = [f"inline void {cls}::bind(Vars::Bank& io) {{"]
    if lhs:
        width = max(len(x) for x in lhs)
        out += [f"    {l.ljust(width)} = {r}" for l, r in zip(lhs, rhs)]
    else:
        out += ["    (void)io;"]

    watched = _watched(p, rows)
    if watched:
        out += ["", "    // The readings this law is entitled to expect. The base"
                    " walks them",
                "    // before each activation, so an input that has stopped being"
                    " believable",
                "    // is DEGRADED whether or not the law thought to ask."]
        for n, (member, idx, _row) in enumerate(watched):
            ref = member + (f"[{idx}]" if _is_array(p, member) else "")
            out.append(f"    _watched[{n}] = {ref}.r;")
        out.append(f"    set_inputs(_watched, {len(watched)});")
    if p.get("health"):
        out += ["    set_health_row("
                f'io.resolve_out<uint8_t>("{p["health"]}"));']
    if "on_bind" in p["hooks"]:
        out.append("    on_bind();")
    return out + ["}", ""]


def _flow(programs, rows):
    """The map that was `tasks.h`'s header comment: what each program passes to
    another, and to whom.

    Only the rows a program produces that ANOTHER program reads. The rest -
    a totalizer, an actuator's position, an alarm - go to the pages and the bus
    and are not how these laws reach each other. Generated, because a
    hand-written map of 21 programs is a map that rots.
    """
    if not programs:
        return []
    produced, read_by = {}, {}
    for p in programs:
        for member, sec, row, _i in p["conns"]:
            if sec in ("out", "inout"):
                produced.setdefault(row, p["name"])
            if sec in ("in", "inout"):
                read_by.setdefault(row, []).append(p["name"])

    out = ["/**",
           " * @brief How the site's programs reach each other.",
           " *",
           " * NEVER directly: no program holds a reference to another, or to an",
           " * instrument another owns. They meet in the variable table and nowhere",
           " * else, which is what lets any of them be constructed with",
           " * (setup_context(), io()) alone. Order below is EXECUTION order, so a",
           " * law reading a value computed this beat finds it already written.",
           " */", ""]
    body, stage = [], None
    for p in programs:
        if p["stage"] != stage:
            stage = p["stage"]
            body.append(("", "", ""))
            body.append((f"[{stage}]", "", ""))
        for member, sec, row, _i in p["conns"]:
            if sec == "out" and produced.get(row) == p["name"]:
                readers = [r for r in read_by.get(row, []) if r != p["name"]]
                if readers:
                    body.append((p["name"], row, ", ".join(sorted(set(readers)))))
    if not any(b[1] for b in body):
        return out
    w0 = max(len(b[0]) for b in body)
    w1 = max(len(b[1]) for b in body)
    lines = ["// The signal flow, from the table: a row one program produces and",
             "// another reads. Terminal rows - a totalizer, a position, an alarm -",
             "// are not here; they go to the pages and the bus."]
    for a, b, c in body:
        if not b:
            lines.append("//" + (f" {a}" if a else ""))
        else:
            lines.append(f"//   {a.ljust(w0)}  {b.ljust(w1)}  -> {c}")
    return out + lines + [""]


def generate(programs, rows, banner):
    """programs.gen.h: what the site's C++ needs to know about its programs.

    The includes, the map of what they pass to each other, and the one list
    site.h expands three times. Every program carries its own interface as a
    base class of its own, so nothing here describes one.
    """
    out = banner + ["#ifndef SITE_PROGRAMS_GEN_H", "#define SITE_PROGRAMS_GEN_H", ""]
    out += _flow(programs, rows)
    out += ["#include <cstdint>", "", '#include "libbotanik.h"', ""]
    if programs:
        out += ["// Every program, in table order: the declaration part"
                " generated from",
                "// the table, with its body in the .cpp beside it."]
        out += [f'#include "../programs/{p["stage"]}/{snake(p["class"])}/'
                f'{snake(p["class"])}.gen.h"' for p in programs]
        out += [""]
    out += _program_list(programs)
    out += ["#endif  // SITE_PROGRAMS_GEN_H"]
    return "\n".join(out) + "\n"
