#!/usr/bin/env python3
"""The table checks, against tables that are deliberately wrong.

A generator is load-bearing the moment the tables go through it: nobody reviews
331 rows of generated C++ line by line, so a check that silently passes is worse
than the hand-written table it replaced. Each case below breaks exactly one rule
and asserts the message names it.

    python3 scripts/test_vars_checks.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vars_checks
import vars_programs


def _int_ok(v):
    try:
        int(v, 0)
        return True
    except (ValueError, TypeError):
        return False


def row(kind, name, **cells):
    r = {"name": name, "_file": "t.csv", "_line": 1, "_set": "site"}
    r.update({k: str(v) for k, v in cells.items()})
    return (kind, r)


def run(rows):
    out = []
    vars_checks.check(rows, out.append)
    return out


def expect(label, rows, needle):
    got = run(rows)
    hit = [p for p in got if needle in p]
    if not hit:
        print(f"FAIL {label}\n     wanted a problem mentioning {needle!r}\n"
              f"     got: {got or 'no problems at all'}")
        return False
    print(f"ok   {label}")
    return True


def expect_clean(label, rows):
    got = [p for p in run(rows) if "not being read" not in p]
    if got:
        print(f"FAIL {label}\n     wanted no problems, got: {got}")
        return False
    print(f"ok   {label}")
    return True


# A minimal table that passes everything, for cases to break one field of.
def base():
    return [
        row("P", "cli_min", reg=100, unit="mV"),
        row("P", "cli_max", reg=101, unit="mV"),
        row("P", "eng_lo", reg=102, unit="V"),
        row("P", "eng_hi", reg=103, unit="V"),
        row("A", "alarm_x", code="X_FLT", reg=800, desc="a fault"),
        row("MI", "meter_V", reg=10, regs=2, dev=5, fc=4, wire="T_F32", timeout=15000),
    ]


# --- the configuration ------------------------------------------------------

def blk(name, type="Ton", count=0, include="", doc=""):
    return {"name": name, "type": type, "count": count, "include": include,
            "doc": doc}


def prog(name, cls="C", task="SLOW", stage="", blocks=(), **sections):
    p = {"name": name, "class": cls, "task": task, "conns": [], "arrays": set(),
         "stage": stage, "blocks": list(blocks)}
    for sec, members in sections.items():
        for member, rows in members.items():
            if isinstance(rows, str):
                p["conns"].append((member, sec, rows, None))
            else:
                for i, r in enumerate(rows):
                    p["conns"].append((member, sec, r, i))
    return p


TABLE = {
    "a_global":  ("SV", {"name": "a_global", "type": "T_F32", "_set": "site"}),
    "b_global":  ("SV", {"name": "b_global", "type": "T_F32", "_set": "site"}),
    "a_coil":    ("RO", {"name": "a_coil", "_set": "site"}),
    "a_input":   ("MI", {"name": "a_input", "type": "T_F32", "_set": "site"}),
    "a_param":   ("P",  {"name": "a_param", "type": "T_F32", "_set": "site"}),
    "a_text":    ("PS", {"name": "a_text", "_set": "site"}),
    "an_alarm":  ("A",  {"name": "an_alarm", "_set": "site"}),
}


def prun(programs, mentioned=(), features=None):
    out = []
    # a_text is READ by the fixture site's own C++: a text row has no typed
    # reference, so a program could not read it even if it wanted to.
    vars_programs.check(programs, TABLE, out.append, set(mentioned) | {"a_text"},
                        features=features)
    return out


def pexpect(label, programs, needle, mentioned=(), features=None):
    got = prun(programs, mentioned, features)
    if not [x for x in got if needle in x]:
        print(f"FAIL {label}\n     wanted {needle!r}\n     got: {got or 'nothing'}")
        return False
    print(f"ok   {label}")
    return True


def pclean(label, programs, mentioned=()):
    got = prun(programs, mentioned)
    if got:
        print(f"FAIL {label}\n     wanted no problems, got: {got}")
        return False
    print(f"ok   {label}")
    return True


# --- the enumerated vocabularies ---------------------------------------------

def member(enum, mem, code, label="L"):
    return {"enum": enum, "member": mem, "code": str(code), "label": label,
            "_set": "site", "_file": "enums.csv", "_line": 1}


def erun(members, rows=()):
    out = []
    vars_checks.enums(members, rows, out.append)
    return out


def eexpect(label, members, needle, rows=()):
    got = erun(members, rows)
    if not [x for x in got if needle in x]:
        print(f"FAIL {label}\n     wanted {needle!r}\n     got: {got or 'nothing'}")
        return False
    print(f"ok   {label}")
    return True


def eclean(label, members, rows=()):
    got = erun(members, rows)
    if got:
        print(f"FAIL {label}\n     wanted no problems, got: {got}")
        return False
    print(f"ok   {label}")
    return True


def enum_checks():
    ok = True
    base = [member("E", "E_A", 0, "a"), member("E", "E_B", 1, "b")]
    ok &= eclean("a well-formed vocabulary", base)

    ok &= eexpect("two members on one code",
                  base + [member("E", "E_C", 1, "c")], "both code 1")
    ok &= eexpect("two members with one label",
                  base + [member("E", "E_C", 2, "a")], "labels both")
    ok &= eexpect("a member name reused by another enum",
                  base + [member("F", "E_A", 0, "z")], "same scope")
    ok &= eexpect("a code that is not a number",
                  base + [member("E", "E_C", "two", "c")], "is not a number")
    ok &= eexpect("a member with no label",
                  base + [member("E", "E_C", 2, "")], "the label IS the vocabulary")

    row = ("SVE", {"name": "a_state", "str": "E", "def": "E_A",
                   "_set": "site", "_file": "status.csv", "_line": 1})
    ok &= eclean("...and a row over it, defaulting to one of its members",
                 base, [row])
    ok &= eexpect("a row naming an enum that is not declared", base,
                  "is not an enum in enums.csv",
                  [("SVE", {"name": "a_state", "str": "Nope", "def": "",
                            "_set": "site", "_file": "status.csv", "_line": 1})])
    ok &= eexpect("a row defaulting to a member of another enum", base,
                  "is not a member of E",
                  [("SVE", {"name": "a_state", "str": "E", "def": "F_Z",
                            "_set": "site", "_file": "status.csv", "_line": 1})])
    return ok


def program_checks():
    ok = True
    # Every row the fixture table declares has a producer: an unproduced one is
    # now an error of its own, and a case about ownership should not trip it.
    CONNS = {"in": {"x": "a_input", "k": "a_param"},
             "out": {"y": "a_global", "z": "b_global",
                     "c": "a_coil", "a": "an_alarm"}}

    def full(**kw):
        """The well-formed configuration, with one thing added to it."""
        return prog("p", cls="P1", **dict(CONNS, **kw))

    base = full()
    ok &= pclean("a well-formed configuration", [base])

    ok &= pexpect("a computed row nothing produces",
                  [prog("p", cls="P1", out={"y": "a_global", "c": "a_coil",
                                            "a": "an_alarm"})],
                  "publishes its declared default")

    ok &= pexpect("a coil nothing drives",
                  [prog("p", cls="P1", out={"y": "a_global", "z": "b_global",
                                            "a": "an_alarm"})],
                  "an output nothing drives")

    ok &= pexpect("a parameter nothing reads",
                  [prog("p", cls="P1", out={"y": "a_global", "z": "b_global",
                                            "c": "a_coil", "a": "an_alarm"})],
                  "a parameter nothing reads")

    ok &= pexpect("an alarm nothing raises",
                  [prog("p", cls="P1", out={"y": "a_global", "z": "b_global",
                                            "c": "a_coil"})],
                  "an alarm nothing raises")

    ok &= pclean("...and none fires for a row the site's own C++ writes",
                 [prog("p", cls="P1", out={"y": "a_global"})],
                 mentioned={"b_global", "a_coil", "an_alarm", "a_param"})

    ok &= pexpect("two programs producing one row",
                  [base, prog("q", cls="P2", out={"y": "a_global"})],
                  "already produced by")

    ok &= pclean("...unless both declare it shared",
                 [prog("p", cls="P1", inout={"y": "a_global"}),
                  prog("q", cls="P2", inout={"y": "a_global"}),
                  prog("r", cls="P3", **{"in": {"k": "a_param"},
                                         "out": {"c": "a_coil", "z": "b_global",
                                                 "a": "an_alarm"}})])

    ok &= pexpect("connecting a text row",
                  [prog("p", cls="P1", **{"in": {"x": "a_text"}})],
                  "no typed reference reaches")

    ok &= pexpect("producing an INPUT row",
                  [prog("p", cls="P1", out={"y": "a_input"})],
                  "the next scan overwrites")

    ok &= pexpect("producing a PARAMETER row",
                  [prog("p", cls="P1", out={"y": "a_param"})],
                  "never reaches flash")

    ok &= pexpect("naming a row that is not there",
                  [prog("p", cls="P1", out={"y": "nope"})],
                  "which is not a row")

    ok &= pexpect("two programs with one name",
                  [prog("p", cls="P1", out={"y": "a_global"}),
                   prog("p", cls="P2", out={"z": "b_global"})],
                  "two programs are called")

    ok &= pexpect("one class instantiated twice",
                  [prog("p", cls="P1", out={"y": "a_global"}),
                   prog("q", cls="P1", out={"z": "b_global"})],
                  "already instantiated")

    ok &= pexpect("an unknown rate class",
                  [prog("p", cls="P1", task="SOMETIMES", out={"y": "a_global"})],
                  "is not one of")

    ok &= pexpect("one member connected twice",
                  [prog("p", cls="P1", **{"in": {"x": "a_input"},
                                          "out": {"x": "a_global"}})],
                  "connected twice")

    # --- the per-program shape: names land in the program's own scope --------

    ok &= pexpect("a member named after Plc::Program's own",
                  [prog("p", cls="P1", **{"in": {"health": "a_input"}})],
                  "would shadow it")

    ok &= pexpect("a block named after Plc::Program's own",
                  [prog("p", cls="P1", blocks=[blk("group")])],
                  "would shadow it")

    ok &= pexpect("a member reaching into the hand-written half",
                  [prog("p", cls="P1", **{"in": {"_state": "a_input"}})],
                  "belongs to the hand-written half")

    ok &= pexpect("a block and a member of one name",
                  [prog("p", cls="P1", blocks=[blk("x")],
                        **{"in": {"x": "a_input"}})],
                  "both as a member and as a block")

    ok &= pexpect("one block declared twice",
                  [prog("p", cls="P1", blocks=[blk("t"), blk("t", "Tof")])],
                  "declared twice")

    ok &= pexpect("a block the library does not have",
                  [prog("p", cls="P1", blocks=[blk("v", "ValveController")])],
                  "needs its `include`")

    ok &= pclean("...which its include makes the site's own",
                 [full(blocks=[blk("v", "ValveController",
                                   include="blocks/v.h")])])

    ok &= pexpect("a block whose family this site builds without",
                  [prog("p", cls="P1", blocks=[blk("f", "Ewma")])],
                  "PLC_BLK_FILTERS off",
                  features={"BOTANIK_FEATURE_PLC_BLK_FILTERS": False})

    ok &= pclean("...and none when it is on",
                 [full(blocks=[blk("f", "Ewma")])])

    ok &= pexpect("a stage that is not a directory name",
                  [prog("p", cls="P1", stage="Sense/Climate")],
                  "becomes a directory name")
    return ok


def header_checks():
    """The per-program declaration part: what the emitted C++ has to say.

    Not a golden file - one would have to be regenerated for every wording
    change. These are the claims the hand-written half depends on.
    """
    p = prog("irr", cls="WaterMeters", task="SLOW", stage="irrigation",
             blocks=[blk("avg", "Ewma"),
                     blk("v", "ValveController", count=3,
                         include="blocks/valve_controller.h")],
             **{"in": {"x": "a_input"},
                "out": {"y": ["a_global", "b_global"], "c": "a_coil",
                        "a": "an_alarm"}})
    p["health"] = "a_global"
    text = vars_programs.program_header(p, TABLE, ["// banner", ""])
    wanted = [
        ("the class derives Plc::Program",
         "class WaterMeters : public Plc::Program {"),
        ("the name and rate class are baked in",
         'Program(ctx, "irr", Tasks::GROUP_SLOW, io) {}'),
        ("bind() cannot be overridden", "void bind(Vars::Bank& io) final;"),
        ("...only extended", "virtual void on_bind() {}"),
        ("...and it calls the hook", "    on_bind();"),
        ("a library block is reached through Plc::", "Plc::Ewma       avg;"),
        ("a site block is emitted as written", "ValveController v[3];"),
        ("...and its header included from the program's directory",
         '#include "../../../blocks/valve_controller.h"'),
        ("an array member carries its length", "static const uint8_t N_Y = 2;"),
        ("a block array too", "static const uint8_t N_V = 3;"),
        ("the watched rows are handed to the base", "set_inputs(_watched, 1);"),
        ("the health row is resolved into the base", "set_health_row("),
    ]
    ok = True
    for label, needle in wanted:
        if needle in text:
            print(f"ok   {label}")
        else:
            print(f"FAIL {label}\n     wanted {needle!r} in the generated header")
            ok = False
    return ok


def main():
    ok = True

    ok &= expect_clean("a well-formed table passes", base())

    t = base(); t[1][1]["reg"] = "100"
    ok &= expect("duplicate register", t, "already claimed by")

    t = base(); t[0][1]["reg"] = "0"
    ok &= expect("a mapped row with no register", t, "must carry a register")

    t = base(); t[4][1]["reg"] = "750"
    ok &= expect("alarm outside its block", t, "a site A row belongs in 800-899")

    t = base(); t[0][1]["reg"] = "850"
    ok &= expect("parameter in the alarm block", t, "a site P row belongs in 1-799")

    # The rule the runtime test could not express: a frozen bank does not know
    # which SET a row came from, so a site row at 912 used to pass.
    t = base(); t[0][1]["reg"] = "912"
    ok &= expect("site row inside the library's reserve", t,
                 "a site P row belongs in 1-799")

    t = base()
    for _, r in t:
        r["_set"] = "libbotanik"
    for _, r in t:
        r["reg"] = str(900 + int(r["reg"]) % 80) if _int_ok(r.get("reg")) else r.get("reg")
    t[4][1]["reg"] = "990"
    ok &= expect_clean("the same rows are fine in the library's own blocks", t)

    t = base(); t[4][1]["code"] = ""
    ok &= expect("alarm with no code", t, "needs a display code")

    t = base(); t[4][1]["desc"] = "x" * 41
    ok &= expect("alarm description wider than the log line", t,
                 "capped at 40")

    t = base(); t[4][1]["code"] = "WAY_TOO_LONG"
    ok &= expect("alarm code too wide for the page", t, "the ALARMS page shows 9")

    t = base(); t[5][1]["regs"] = "1"
    ok &= expect("span narrower than the wire type", t, "needs 2 register(s)")

    t = base(); t[5][1]["timeout"] = "0"
    ok &= expect("field-bus input that can never go stale", t, "needs a timeout")

    t = base(); t.append(row("DI", "local_pin", dev=0, fc=2, reg=0, timeout=0))
    ok &= expect_clean("a LOCAL input needs no timeout: no bus can fall silent", t)

    t = base(); t.append(row("MI", "meter_A", reg=11, regs=2, dev=5, fc=4,
                             wire="T_F32", timeout=15000))
    ok &= expect("two rows overlapping on one device", t, "overlaps")

    t = base(); t.append(row("MI", "other_dev", reg=11, regs=2, dev=6, fc=4,
                             wire="T_F32", timeout=15000))
    ok &= expect_clean("the same registers on a DIFFERENT device are fine", t)

    t = base(); t.append(row("RO", "coil_a", reg=10, dev=5, fc=5))
    ok &= expect_clean("a coil may reuse a register number: bits are their own space", t)

    cal = dict(cal_raw_min="cli_min@0", cal_raw_max="cli_max@1", dev=11, fc=4,
               reg=200, regs=1, wire="T_U16", timeout=5000,
               cal_eng_min="eng_lo@0", cal_eng_max="eng_hi@1",
               cal_min="eng_lo@0", cal_max="eng_hi@1")
    t = base(); t.append(row("AI", "an_in", **cal))
    ok &= expect_clean("a resolvable calibration passes", t)

    c = dict(cal); c["cal_raw_max"] = "nope@1"
    t = base(); t.append(row("AI", "an_in", **c))
    ok &= expect("calibration naming a parameter that is not there", t,
                 "which is not a parameter")

    c = dict(cal); c["cal_raw_max"] = "eng_hi@1"      # V against mV
    t = base(); t.append(row("AI", "an_in", **c))
    ok &= expect("raw endpoints in different units", t,
                 "raw endpoints disagree on their unit")

    c = dict(cal); c["cal_min"] = ""
    t = base(); t.append(row("AI", "an_in", **c))
    ok &= expect("an empty calibration field", t, "calibration field cal_min is empty")

    t = base(); t.append(row("P", "cli_min", reg=104, unit="mV"))
    ok &= expect("one name declared twice in a set", t, "is declared twice")

    # Redeclaration: a site row restating a library row. The library's is
    # shadowed out at freeze(), so it must NOT read as a duplicate register, and
    # the restatement is judged against the block its register came from.
    def restated(**over):
        lib = row("P", "rtu_timeout_ms", reg=980, unit="ms", type="T_U32")
        lib[1]["_set"] = "libbotanik"
        cells = dict(reg=980, unit="ms", type="T_U32")
        cells.update(over)
        return base() + [lib, row("P", "rtu_timeout_ms", **cells)]

    ok &= expect_clean("a site row may restate a library row", restated())
    ok &= expect("a restatement may not move the register",
                 restated(reg=105), "keep the register the library assigned")
    ok &= expect("a restatement may not change the type",
                 restated(type="T_F32"), "the library binds it by type")

    # Edge-counted local inputs, against what the OPTA's part can do.
    def counted(*chans):
        t = base()
        for i, c in enumerate(chans):
            t.append(row("DI", f"pulse{i}", dev=0, fc=2, reg=c, timeout=0,
                         invert="false", isr="rising"))
        return t

    ok &= expect_clean("two edge-counted inputs on free channels", counted(1, 2))
    ok &= expect("I1 and I4 counting at once", counted(0, 3),
                 "share an interrupt line")
    ok &= expect_clean("I1 alone is fine, and so is I4 alone", counted(0))
    ok &= expect("two rows on one channel", counted(2, 2), "is already counted by")
    ok &= expect("more counters than lines", counted(0, 1, 2, 4, 5, 6, 7, 2),
                 "at most 7")

    t = base(); t.append(row("DI", "p", dev=0, fc=2, reg=1, timeout=0,
                             invert="false", isr="sideways"))
    ok &= expect("an isr mode that is not an edge", t, "is not one of")

    t = base(); t.append(row("DI", "p", dev=9, fc=2, reg=1, timeout=5000,
                             invert="false", isr="rising"))
    ok &= expect("edge-counting a bus device", t, "only a LOCAL row")

    t = base()
    t[0][1]["access"] = "maybe"
    ok &= expect("an access level that is not one of the three", t,
                 "is not one of")

    t = base()
    t[4][1]["access"] = "RW"
    ok &= expect("an access level on a row nothing may write", t,
                 "only a parameter has an access level")

    t = base()
    t[0][1]["access"] = "RO"
    ok &= expect_clean("...and RO on a parameter is fine", t)

    ok &= enum_checks()
    ok &= program_checks()
    ok &= header_checks()

    print("\n" + ("all checks bite" if ok else "SOME CHECKS DO NOT BITE"))
    return 0 if ok else 1



if __name__ == "__main__":
    sys.exit(main())
