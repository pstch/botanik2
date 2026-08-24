#!/usr/bin/env python3
"""The IEC 61131-3 configuration: programs, and what each connects to.

The variable tables say what EXISTS; this says who reads and who produces. That
is what makes single-ownership checkable before the firmware is built, rather
than being noticed at bring-up - or, for a row only a fault path drives, months
in.
"""

import glob
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
    "Staircase3": "PLC_BLK_BASE", "Ramp": "PLC_BLK_BASE",
    "Deadband": "PLC_BLK_BASE", "Integral": "PLC_BLK_BASE",
    "Derivative": "PLC_BLK_BASE", "Actuator": "PLC_BLK_BASE",
    "Pid": "PLC_BLK_BASE",
    "Ewma": "PLC_BLK_FILTERS", "MedianOf3": "PLC_BLK_FILTERS",
    "MovingAverage": "PLC_BLK_FILTERS",
}

# Plc::Program's own surface. A row or block member takes the name it is given
# in the table, in the program's scope, so one of these would shadow the base.
# Plc::Program's and Component's own internals. A `[var]` or `[fn]` name takes
# an underscore here, so one of these would land on top of the base's.
# The pins a node presents, as (inputs, outputs). A block's OWN parameter names,
# never IEC's: these pages explain THIS code, and these are what a reader finds
# in the .cpp. Shared with logic_report, which draws exactly what this checks.
#
# A lowercase key is a stateless FUNCTION, which no table declares and nothing
# holds; a CamelCase one is a function block, and the program's [block] section
# has to declare it.
PINS = {
    "Ton":        (("in", "delay_ms", "dt"), ("q",)),
    "Tof":        (("in", "delay_ms", "dt"), ("q",)),
    "Tp":         (("in", "width_ms", "dt"), ("q",)),
    "Sr":         (("set", "reset"), ("q",)),
    "Rs":         (("set", "reset"), ("q",)),
    "RTrig":      (("in",), ("q",)),
    "FTrig":      (("in",), ("q",)),
    "Hysteresis": (("value", "low", "high"), ("q",)),
    "Hysteresis3": (("value", "low", "high", "hyst"), ("band",)),
    "Staircase3": (("value", "edge1", "edge2", "edge3", "hyst"), ("level",)),
    "Ramp":       (("target", "rate_per_s", "dt"), ("out",)),
    "Deadband":   (("value", "band"), ("out",)),
    "Ewma":       (("x", "tau_s", "dt"), ("out",)),
    # push is the CALL, not a parameter: this block samples only when the law
    # asks it to, and a page that did not show what asks is missing the design.
    "ExpMovingAverage": (("x", "push"), ("out",)),
    # plc/functions/ - the vocabulary a law calls by name.
    "sub":        (("in1", "in2"), ("out",)),
    "abs_of":     (("in",), ("out",)),
    "mux4":       (("k", "in0", "in1", "in2", "in3"), ("out",)),
    "max_of":     (("in1", "in2"), ("out",)),
    "max4":       (("in1", "in2", "in3", "in4"), ("out",)),
    "min_of":     (("in1", "in2"), ("out",)),
    "sel":        (("g", "in0", "in1"), ("out",)),
    "limit":      (("v", "lo", "hi"), ("out",)),
    "in_range":   (("v", "lo", "hi"), ("out",)),
    "ladder":     (("v", "lo", "hi"), ("out",)),
    "signed_ladder": (("v", "neg_full", "neg_start", "pos_start", "pos_full"),
                      ("out",)),
    "scale":      (("v", "in_lo", "in_hi", "out_lo", "out_hi"), ("out",)),
    "deadzone":   (("v", "band"), ("out",)),
    "median3":    (("in1", "in2", "in3"), ("out",)),
    "dew_point":  (("t", "rh"), ("out",)),
    "sat_vapour_pressure": (("t",), ("out",)),
    "dew_point_margin": (("t", "rh"), ("out",)),
    "vapour_pressure_deficit": (("t", "rh"), ("out",)),
    # The OPERATORS a law writes inline. No Plc:: function stands behind these -
    # `a > b` is GT whatever it is spelled as, and an FBD has to draw the
    # comparison that decides. Anything with a naming CHOICE gets a real
    # function instead, which is why mux4 is one and gt is not.
    "gt":         (("in1", "in2"), ("out",)),
    "ge":         (("in1", "in2"), ("out",)),
    "lt":         (("in1", "in2"), ("out",)),
    "le":         (("in1", "in2"), ("out",)),
    "eq":         (("in1", "in2"), ("out",)),
    "and_of":     (("in1", "in2"), ("out",)),
    "or_of":      (("in1", "in2"), ("out",)),
    "or4":        (("in1", "in2", "in3", "in4"), ("out",)),
    "not_of":     (("in",), ("out",)),
    "add":        (("in1", "in2"), ("out",)),
    "mul":        (("in1", "in2"), ("out",)),
    "div":        (("in1", "in2"), ("out",)),
    "expt":       (("in1", "in2"), ("out",)),
    "MedianOf3":  (("x",), ("out",)),
    "MovingAverage": (("x",), ("out",)),
    "Integral":   (("rate_per_s", "dt", "cutoff"), ("total",)),
    "Derivative": (("quantity", "window_ms", "dt"), ("rate",)),
    "Pid":        (("sp", "pv", "tuning", "dt"), ("out",)),
    "Actuator":   (("target", "cfg", "dt"), ("position", "moving", "open", "close")),
}


def site_blocks(site_dir):
    """The site's OWN function blocks, out of blocks/blocks.toml.

    The library's are above; a site's are its own and are declared with it, so
    this file knows what every block presents without knowing any site.
    """
    try:
        import tomllib
    except ImportError:                       # pragma: no cover
        return {}
    path = os.path.join(site_dir, "blocks", "blocks.toml")
    if not os.path.exists(path):
        return {}
    with open(path, "rb") as f:
        return tomllib.load(f)


def is_function(type_name):
    """A FUNCTION node rather than a block. Stateless, so no member declares it
    and the table cannot be asked to: `Plc::sub` against `Plc::Staircase3`."""
    return type_name.split("::")[-1][:1].islower()


def pins_of(type_name):
    """The (inputs, outputs) a node type presents, template arguments dropped."""
    return PINS.get(type_name.split("::")[-1].split("<")[0])


RESERVED_INTERNAL = frozenset((
    "_io", "_group", "_slot", "_late_ms", "_late_max_ms", "_health",
    "_health_row", "_health_said", "_safe_asked", "_disable_asked",
    "_travelling", "_watched", "_watched_count", "_watch_rows", "_tasks",
    "_logger", "_name", "_started",
    "_activate", "_run_task", "_watch_inputs", "_worsen",
))

RESERVED = frozenset((
    "bind", "on_bind", "run", "observe", "parking", "degraded", "faulted",
    "group", "slot", "set_slot", "io", "name", "health", "health_row",
    "set_health_row", "set_inputs", "set_safe", "is_parked", "register_tasks",
    "late_ms", "late_max_ms", "safe_requested", "disable_requested",
    "inhibit_requested", "request_safe", "request_disable",
    "watched", "watched_count", "publish_health",
    # ...and Component's, which Program derives.
    "setup", "started", "step", "announce", "announce_task", "healthy",
    "on_setup", "on_input", "on_config", "on_service", "on_control",
    "on_output", "on_present",
))


def load(site_dir):
    """Every program the site declares, in the order it runs.

    One file per program, `programs/<stage>/<class>/<class>.toml`, beside the
    body it declares. `programs/order.toml` is the ORDER they run in, which is
    the one thing about a program that is not a property of the program.
    """
    try:
        import tomllib
    except ImportError:                       # pragma: no cover
        sys.exit("the program tables need Python 3.11+ for tomllib")

    for _name, _spec in site_blocks(site_dir).items():
        PINS[_name] = (tuple(_spec.get("in", ())), tuple(_spec.get("out", ())))

    root = os.path.join(site_dir, "programs")
    if not os.path.isdir(root):
        return []
    listed = order(site_dir)

    found = {}
    for path in sorted(glob.glob(os.path.join(root, "*", "*", "*.toml"))):
        with open(path, "rb") as f:
            doc = tomllib.load(f)
        stage = os.path.basename(os.path.dirname(os.path.dirname(path)))
        found[doc.get("name", os.path.basename(path)[:-5])] = _program(doc, stage, path)

    out = [found.pop(n) for n in listed if n in found]
    out += [found[n] for n in sorted(found)]      # not in the order file: kept,
    return out                                    # so check() can say so


def _where(p):
    """A program's file, as the message names it."""
    return os.path.relpath(p["path"], os.getcwd())


def order(site_dir):
    """The names programs/order.toml lists, in the order it lists them."""
    try:
        import tomllib
    except ImportError:                       # pragma: no cover
        return []
    path = os.path.join(site_dir, "programs", "order.toml")
    if not os.path.exists(path):
        return []
    with open(path, "rb") as f:
        return list(tomllib.load(f).get("order") or [])


def _program(p, stage, path):
    """One program's file as the dict everything downstream reads."""
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
    return {"name": p.get("name", "?"), "class": p.get("class", "?"),
            "task": p.get("task", "?"), "health": p.get("health", ""),
            "stage": stage, "path": path, "blocks": _blocks(p),
            "vars": _decls(p.get("var")), "fns": _decls(p.get("fn")),
            "hooks": list(p.get("hooks") or []),
            # Collapsed, so a long one can WRAP in the table: it is one
            # sentence, not one physical line. A blank line in it means the
            # author wanted `notes`.
            "desc": " ".join(p.get("desc", "").split()),
            "desc_paras": "\n\n" in p.get("desc", "").strip(),
            "notes": p.get("notes", ""),
            "conns": conns, "counts": counts, "net": _net(p)}


def _decls(section):
    """A [program.var] or [program.fn] section: name -> {type/sig, init, doc}.

    A bare string is the type (or the signature); the table form carries the
    initial value and the one-line comment the declaration gets.
    """
    out = []
    for name, spec in (section or {}).items():
        if isinstance(spec, str):
            spec = {"type": spec}
        # The table names it as the law thinks of it; the underscore that says
        # "internal, not an interface" is added here, once.
        out.append({"name": "_" + name, "given": name, "type": spec.get("type", ""),
                    "count": int(spec.get("count", 0) or 0),
                    "init": spec.get("init", ""),
                    # An initialiser naming the constructor's own argument has
                    # to be a ctor-init entry, not an NSDMI.
                    "from_ctx": bool(spec.get("from_ctx", False)),
                    "doc": spec.get("doc", "")})
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


def _net(p):
    """The [net.*] sections: this program's FBD network, node by node.

    A node is one BLOCK it holds (the name matches its [block] entry) or one
    FUNCTION it calls (`fn = "sub"`). Its [net.x.in] table sources every input
    pin and [net.x.out] says where each output lands. A source is a local name,
    another node's `node.pin`, or - prefixed `=` - something that is not a
    signal at all: a literal, a constant, the scan's own dt.
    """
    out = []
    for name, spec in (p.get("net") or {}).items():
        out.append({"name": name, "fn": spec.get("fn", ""),
                    "in": dict(spec.get("in") or {}),
                    "out": dict(spec.get("out") or {})})
    return out


def net_type(n, blocks):
    """A net node's C++ type: a function is reached through `Plc::` like any
    other, and a block's type is whatever its [block] entry declared."""
    if n["fn"]:
        return n["fn"] if "::" in n["fn"] else "Plc::" + n["fn"]
    for b in blocks:
        if b["name"] == n["name"]:
            return block_type(b)
    return ""


# What a pin may read besides a local or a node output: the scan context, the
# base's own inhibit, and the two boolean literals. Everything else has to be a
# name the program declares - a diagram whose labels are prose says whatever
# the author felt like, and nothing can check it.
NET_BASE = frozenset(("c.dt_ms", "c.first_scan", "inhibited", "true", "false"))

_NET_CHARS = re.compile(r"^[A-Za-z0-9_.\s+\-*/()\[\]<>=!&|?:^%,]*$")
_NET_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)?")


def _check_expr(src, at, nodes, locals_, blocks, err):
    """One `=` source: a formula over names this program actually declares.

    Numbers, operators and ALL_CAPS constants pass; a bare word does not, which
    is what keeps a pin label from drifting back into prose.
    """
    if not _NET_CHARS.match(src):
        err(f"{at} reads '{src}', which is not a formula")
        return
    for m in _NET_IDENT.finditer(src):
        tok = m.group(0)
        head, _, tail = tok.partition(".")
        if tok in NET_BASE or head in NET_BASE:
            continue
        if head in nodes:
            outs = (pins_of(net_type(nodes[head], blocks)) or ((), ()))[1]
            if tail and tail not in outs:
                err(f"{at} reads '{tok}', which {head} does not output")
            continue
        if head in locals_:
            if tail and tail != "valid":
                err(f"{at} reads '{tok}', and a row has no '{tail}'")
            continue
        if head.upper() == head:
            continue                      # a constant, by the usual spelling
        if pins_of(head) is not None:
            continue                      # one of the vocabulary's own functions
        err(f"{at} reads '{tok}', which this program does not declare")


def check_net(p, err):
    """One program's [net]: every pin sourced, every source a real signal.

    The wiring used to be transcribed into the report by hand and nothing
    checked it, so two pins pointed at rows that had never existed. Here the
    names are the program's own, and a name it does not declare is an error.
    """
    net = p["net"]
    if not net:
        return
    where = _where(p)
    nodes = {n["name"]: n for n in net}
    locals_ = set(c[0] for c in p["conns"]) | set(v["given"] for v in p["vars"])
    outs = set(c[0] for c in p["conns"] if c[1] in ("out", "inout"))

    declared = set(b["name"] for b in p["blocks"])
    for miss in sorted(declared - set(nodes)):
        err(f"{where}: block '{miss}' is declared and not wired into [net]")

    for n in net:
        typ = net_type(n, p["blocks"])
        if not typ:
            err(f"{where}: [net.{n['name']}] is neither a declared block nor a "
                f"function (give it `fn`)")
            continue
        pins = pins_of(typ)
        if pins is None:
            err(f"{where}: [net.{n['name']}] is a {typ}, whose pins nothing "
                f"declares - add it to vars_programs.PINS")
            continue
        pi, po = pins
        for pin in pi:
            if pin not in n["in"]:
                err(f"{where}: [net.{n['name']}.in] does not source '{pin}'")
        for pin, src in n["in"].items():
            if pin not in pi:
                err(f"{where}: [net.{n['name']}.in] '{pin}' is not a pin of "
                    f"{typ.split('::')[-1]}")
                continue
            if src.startswith("="):
                _check_expr(src[1:], f"{where}: [net.{n['name']}.in] '{pin}'",
                            nodes, locals_, p["blocks"], err)
                continue
            head, _, tail = src.partition(".")
            if tail:
                if head not in nodes:
                    err(f"{where}: [net.{n['name']}.in] '{pin}' reads "
                        f"'{src}', and there is no node '{head}'")
                else:
                    other = pins_of(net_type(nodes[head], p["blocks"])) or ((), ())
                    if tail not in other[1]:
                        err(f"{where}: [net.{n['name']}.in] '{pin}' reads "
                            f"'{src}', which {head} does not output")
            elif src not in locals_:
                err(f"{where}: [net.{n['name']}.in] '{pin}' reads '{src}', "
                    f"which this program does not declare")
        for pin, dst in n["out"].items():
            if pin not in po:
                err(f"{where}: [net.{n['name']}.out] '{pin}' is not an output "
                    f"of {typ.split('::')[-1]}")
            elif dst not in outs:
                err(f"{where}: [net.{n['name']}.out] '{pin}' lands on '{dst}', "
                    f"which is not one of this program's outputs")


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
        listed = {}
        for member, sec, row, idx in p["conns"]:
            if "*" not in row:
                conns.append((member, sec, row, idx))
                listed[(sec, member)] = listed.get((sec, member), 0) + 1
                continue
            where = f"{_where(p)}: {sec} '{member}'"
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
            for i, (_h, name) in enumerate(hits):
                conns.append((member, sec, name, i))
        for key, n in listed.items():
            want = p["counts"].get(key)
            if want is not None and want != n:
                err(f"{_where(p)}: {key[0]} '{key[1]}' lists {n} rows, not the "
                    f"{want} it declares")
        p["conns"] = conns


def check(programs, rows, err, mentioned=(), cal_params=(), library=(),
          features=None, order=()):
    """Every rule the configuration has to satisfy.

    @p rows is the effective table as {name: (scope, row)}. @p mentioned is the
    row names the site's own hand-written C++ spells out - `Site::simulate()`
    animates rows by name, and a row it writes is produced whatever this file
    says. @p cal_params is the parameters an analog calibration names.
    @p library is every name libbotanik declares: a site row RESTATING one is
    read and written by library code, which has no entry in this file.
    @p features is the site's BOTANIK_FEATURE_* map, for the block families.
    @p order is programs/order.toml: every program has to be in it.
    """
    seen_names, seen_classes = {}, {}
    producers = {}          # row -> the program that declares it an out
    readers = set()         # rows any program declares an in
    shared = set()          # rows declared inout by anyone

    for p in programs:
        where = _where(p)
        check_net(p, err)
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
        elif p["desc_paras"]:
            err(f"{where}: desc is one sentence; the paragraphs go in `notes`")
        elif len(p["desc"]) > DESC_MAX:
            err(f"{where}: desc is {len(p['desc'])} chars, over {DESC_MAX}: it "
                f"has to read as one @brief")

        # The file is the declaration and its path is the rest of it: the
        # directory names the class, the one above it names the stage.
        want = snake(p["class"]) + ".toml"
        got = os.path.basename(p["path"])
        if got != want:
            err(f"{where}: class {p['class']} belongs in {want}, not {got}")
        elif os.path.basename(os.path.dirname(p["path"])) != snake(p["class"]):
            err(f"{where}: class {p['class']} belongs in a directory called "
                f"{snake(p['class'])}")
        if p["name"] not in order:
            err(f"{where}: not in programs/order.toml, so nothing says when it "
                f"runs")

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

        # An internal is declared under its plain name and DECLARED with an
        # underscore, which is what tells it from the interface in the body.
        for what, items in (("var", p["vars"]), ("fn", p["fns"])):
            for d in items:
                if d["given"].startswith("_"):
                    err(f"{where}: {what} '{d['given']}': the underscore that "
                        f"says INTERNAL is the generator's, not the table's")
                elif d["name"] in RESERVED_INTERNAL:
                    err(f"{where}: {what} '{d['given']}' becomes {d['name']}, "
                        f"which is Plc::Program's own")
                elif d["name"] in {x["name"] for x in
                                   (p["fns"] if what == "var" else p["vars"])}:
                    err(f"{where}: '{d['given']}' is declared both as a var and "
                        f"as a fn")
                if not d["type"]:
                    err(f"{where}: {what} '{d['given']}' names no type")

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
                base = b["type"].split("<")[0]
                if base not in PLC_BLOCKS:
                    err(f"{where}: block '{b['name']}' is a {b['type']}, which is "
                        f"not one of the library's blocks - qualify it, or give "
                        f"a block of the site's own its `include`")
                elif features and not features.get(
                        "BOTANIK_FEATURE_" + PLC_BLOCKS[base], True):
                    err(f"{where}: block '{b['name']}' is a {b['type']}, and this "
                        f"site builds with {PLC_BLOCKS[base]} off")

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
    return (["/** @name The site's programs, in EXECUTION order",
             " *  site.h expands this three times: the members, the constructor",
             " *  init, the registration.",
             " *  @{ */", ""]
            + [x.ljust(col) + "\\" for x in lines[:-1]] + [lines[-1]]
            + ["", "/** @} */", ""])


def snake(cls):
    """A class name as the directory and file name it gets: TempRHZone ->
    temp_rh_zone."""
    out = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", cls)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", out).lower()


def _doc_block(text, indent="    "):
    """A doc comment above a member, wrapped. The trailing form (`///<`) is the
    caller's business; this is the one that does not fit on the line."""
    out = textwrap.wrap(" ".join(text.split()), DECL_COLS - 3,
                        initial_indent=indent + "/** @brief ",
                        subsequent_indent=indent + " *  ")
    out[-1] += " */"
    return out


def _brief(desc):
    """@brief, wrapped. The table's one line, as the doc block's first."""
    lines = textwrap.wrap(desc, 71)
    return [" * @brief " + lines[0]] + [" *        " + x for x in lines[1:]]


def block_type(b):
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


# A declaration and its trailing ///< have to fit a line; a longer note goes
# above the member as a wrapped block, which is what a hand-written header did.
DECL_COLS = 79


def _decl_group(title, decls, doc=()):
    """One @name group of member declarations, types column-aligned."""
    if not decls:
        return []
    width = max(len(t) for t, _n, _d in decls)
    short = [(t, n, " ".join(d.split())) for t, n, d in decls]
    short = [(t, n, d if len(f"    {t.ljust(width)} {n};  ///< {d}") <= DECL_COLS
              else "") for t, n, d in short]
    named = max(len(n) for _t, n, d in short if d) + 1 if any(
        d for _t, _n, d in short) else 0
    out = [f"    /** @name {title}"] + [f"     *  {x}" for x in doc] + ["     *  @{ */", ""]
    for (t, n, _d), (_t, _n, note) in zip(decls, short):
        long_ = " ".join(_d.split()) if _d and not note else ""
        if long_:
            out += _doc_block(long_)
        line = f"    {t.ljust(width)} {(n + ';').ljust(named)}"
        out.append((line + f"  ///< {note}") if note else line.rstrip())
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
                       [(block_type(b),
                         b["name"] + (f"[{b['count']}]" if b["count"] > 1 else ""),
                         b["doc"]) for b in p["blocks"]])
    if p["blocks"]:
        out += [""]
    out += _decl_group("VAR - what this law remembers",
                       [(v["type"],
                         v["name"] + (f"[{v['count']}]" if v["count"] else "")
                         + (f" = {v['init']}" if v["init"] and not v["from_ctx"]
                            else ""),
                         v["doc"]) for v in p["vars"]])
    if p["vars"]:
        out += [""]

    sizes = _sizes(p, rows)
    if sizes:
        width = max(len(n) for n, _c in sizes)
        out += ["    /** @name Sizes - what a body loops over",
                "     *  @{ */", ""]
        out += [f"    static const uint8_t {n.ljust(width)} = {c};" for n, c in sizes]
        out += ["", "    /** @} */", ""]

    if watched:
        out += [f"    Vars::VarRef _watch_rows[{len(watched)}];  ///< swept for"
                f" validity before each activation", ""]

    ctx_init = [v for v in p["vars"] if v["from_ctx"]]
    init = "".join(f", {v['name']}({v['init']})" for v in ctx_init)
    out += ["public:",
            f"    {cls}(const SetupContext& ctx, Vars::Bank& io)",
            f'        : Program(ctx, "{p["name"]}", Tasks::GROUP_{p["task"]},'
            f' io){init} {{}}',
            ""]

    out += ["protected:",
            "    /** @brief Resolve every name. An output is CLAIMED here. */",
            "    void bind(Vars::Bank& io) override;",
            "    void run(ProgramContext& c) override;"]
    if "observe" in p["hooks"]:
        out += ["    void observe(ProgramContext& c) override;  ///< reports in"
                " EVERY state"]
    if "on_bind" in p["hooks"]:
        out += ["    void on_bind();  ///< wire a block to a row it was handed"]
    out += [""]
    if p["fns"]:
        out += ["private:", ""]
        width = max(len(f["type"].split("(")[0].strip()) for f in p["fns"])
        for f in p["fns"]:
            ret, rest = f["type"].split("(", 1)
            if f["doc"]:
                out += _doc_block(f["doc"])
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
        out += ["", "    // What the base sweeps before each activation, so an"
                    " input that has",
                "    // stopped being believable is DEGRADED without the law"
                    " asking."]
        for n, (member, idx, _row) in enumerate(watched):
            ref = member + (f"[{idx}]" if _is_array(p, member) else "")
            out.append(f"    _watch_rows[{n}] = {ref}.r;")
        out.append(f"    set_inputs(_watch_rows, {len(watched)});")
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
           " * @brief How the site's programs reach each other: through the"
           " table,",
           " *        never directly. In EXECUTION order.",
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
    lines = ["// A row one program produces and another reads. Terminal rows -"
             " a",
             "// totalizer, a position, an alarm - go to the pages and the bus,"
             " not here."]
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
        out += ["// Every program, in EXECUTION order."]
        out += [f'#include "../programs/{p["stage"]}/{snake(p["class"])}/'
                f'{snake(p["class"])}.gen.h"' for p in programs]
        out += [""]
    out += _program_list(programs)
    out += ["#endif  // SITE_PROGRAMS_GEN_H"]
    return "\n".join(out) + "\n"
