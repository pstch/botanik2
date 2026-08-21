#!/usr/bin/env python3
"""What a variable table has to satisfy, checked before the C++ exists.

These used to be doctest cases over a frozen bank. They are DECLARATION rules -
a duplicate register, a span that does not match its wire type, a calibration
naming a parameter that is not there - so they belong where the declaration is,
reported against a CSV line rather than a row index.

The runtime rules stay in tests/test_site.cpp, because they are about the FROZEN
BANK and no schema can reach them: redeclaration, string-pool layout, name-index
construction, and whether the names the FIRMWARE reaches for resolve.

Every check runs over BOTH table sets at once. That is the point: the register
space is shared - a site's alarms at 800-899, the library's rows at 900-999 -
so half the table cannot check it.
"""

# Bytes a value occupies on the wire, mirroring Vars::type_width().
TYPE_WIDTH = {
    "T_BOOL": 1, "T_U8": 1, "T_I8": 1, "T_ENUM": 1,
    "T_U16": 2, "T_I16": 2,
    "T_U32": 4, "T_I32": 4, "T_F32": 4,
    "T_U64": 8, "T_I64": 8,
    "T_STR": 0,
}
# Function codes addressing bits rather than 16-bit registers (Modbus::is_bit).
BIT_FCS = {1, 2, 5, 15}

# Which scopes carry a Modbus TCP slave register (Vars::var_mapped).
MAPPED = {"P", "PE", "PS", "SV", "SVR", "A"}

ALARM_CODE_MAX = 9      # AlarmsPage::CODE_W
# An alarm's description IS its ALARM_* log line: one line, and the house
# rule in logs/format.h is 40 columns.
ALARM_DESC_MAX = 40


class Problem(Exception):
    pass


def _int(cell):
    """A cell as an int, or None when it is a symbol or an expression."""
    try:
        return int(cell, 0)
    except (ValueError, TypeError):
        return None


def _where(row):
    """Set-qualified, because both table sets have a params.csv."""
    return f"{row['_set']}/{row['_file']}:{row['_line']}"


# Which registers each table set may claim, per scope. Stricter than the runtime
# test this replaces, which could only say "one block or the other" - a frozen
# bank does not know which SET a row came from, and a site row sitting at 912
# passed. Reading both sets is what makes the real rule checkable.
BLOCKS = {
    True:  {"A": (800, 899), "*": (1, 799)},      # a site
    False: {"A": (986, 999), "*": (900, 985)},    # the library
}


def _reg_block_ok(scope, reg, is_site):
    lo, hi = BLOCKS[is_site].get(scope, BLOCKS[is_site]["*"])
    return lo <= reg <= hi


def _block_text(scope, is_site):
    lo, hi = BLOCKS[is_site].get(scope, BLOCKS[is_site]["*"])
    return f"{'a site' if is_site else 'the library'} {scope} row belongs in {lo}-{hi}"


def _span(row):
    """[first, last) register span of a field-bus row."""
    reg = _int(row.get("reg", ""))
    regs = _int(row.get("regs", "")) or 1
    return (reg, reg + max(regs, 1)) if reg is not None else None


LIBRARY = "libbotanik"


def _shadows(rows, err):
    """Split @p rows into (effective, redeclared).

    A site row RESTATES a library row by declaring the same name: Bank::freeze()
    keeps the highest-numbered declaration and tombstones the earlier one, and
    segments register library-then-site, so the site's wins. The shadowed row
    does not exist at run time, and checking it would report a duplicate
    register against a row nothing can reach.

    @return (rows the firmware will actually have, {name: (library, site)})
    """
    lib = {r["name"]: (k, r) for k, r in rows if r["_set"] == LIBRARY}
    site = {r["name"]: (k, r) for k, r in rows if r["_set"] != LIBRARY}
    pairs = {}
    for name in sorted(set(lib) & set(site)):
        lk, lr = lib[name]
        sk, sr = site[name]
        pairs[name] = (lr, sr)

        # The register is an external contract - a master's map is written
        # against it - so a restatement inherits it rather than moving it.
        if lk in MAPPED and sk in MAPPED and lr.get("reg") != sr.get("reg"):
            err(f"{_where(sr)}: {name} redeclares a library row but moves its "
                f"register from {lr.get('reg')} to {sr.get('reg')}; keep the "
                f"register the library assigned")
        # Library code binds this row by name AND type; changing the type under
        # it resolves to the sink at bring-up rather than failing here.
        if lr.get("type", "") != sr.get("type", ""):
            err(f"{_where(sr)}: {name} redeclares a library row with type "
                f"'{sr.get('type')}' instead of '{lr.get('type')}'; the library "
                f"binds it by type")

    shadowed = {id(lr) for lr, _ in pairs.values()}
    return [(k, r) for k, r in rows if id(r) not in shadowed], pairs


def capacity(rows, var_max, err):
    """Whether the declared rows fit the bank the site sized.

    Not a declaration rule but a CAPACITY one, and it has to be checked here
    because the way it fails at run time is unreadable: register_vars() adds
    NONE of a segment that does not fit, so one row past the limit silently
    costs a whole table - an empty field-bus plan, a failing POST, and 205 rows
    where 353 were declared.
    """
    if var_max is None:
        return
    n = len(rows)
    if n > var_max:
        err(f"{n} rows declared, but the site sizes its bank at "
            f"BOTANIK_VAR_MAX {var_max}: registration would refuse a whole "
            f"segment. Raise it in the site's config.h (about 36 bytes of RAM "
            f"per row).")
    elif n > var_max - 8:
        print(f"  note: {n} of {var_max} rows used - {var_max - n} left before "
              f"a segment is refused")


def check(rows, err):
    """Run every declaration rule over @p rows; report through @p err."""
    rows, redeclared = _shadows(rows, err)
    for name, (lr, sr) in redeclared.items():
        print(f"  note: {_where(sr)}: '{name}' restates the library row at "
              f"{_where(lr)} (the site's wins)")
    _registers(rows, redeclared, err)
    _alarms(rows, err)
    _field_bus(rows, err)
    _pulses(rows, err)
    _access(rows, err)
    _retain(rows, err)
    _calibrations(rows, err)
    _names(rows, err)


def _registers(rows, redeclared, err):
    seen = {}
    for kind, r in rows:
        if kind not in MAPPED:
            continue
        reg = _int(r.get("reg", ""))
        if reg is None:
            err(f"{_where(r)}: {r['name']}: register '{r.get('reg')}' is not a number")
            continue
        if reg == 0:
            err(f"{_where(r)}: {r['name']}: a {kind} row must carry a register")
            continue
        # A restatement keeps the LIBRARY's register, so it is judged against
        # the library's block rather than the site's.
        is_site = r["_set"] != LIBRARY and r["name"] not in redeclared
        if not _reg_block_ok(kind, reg, is_site):
            err(f"{_where(r)}: {r['name']}: register {reg} is outside its block "
                f"({_block_text(kind, is_site)})")
        if reg in seen:
            other = seen[reg]
            err(f"{_where(r)}: {r['name']}: register {reg} is already claimed by "
                f"{other['name']} at {_where(other)}")
        else:
            seen[reg] = r
    if not seen:
        err("no row carries a Modbus register: the tables are not being read")


def _alarms(rows, err):
    for kind, r in rows:
        if kind != "A":
            continue
        code = r.get("code", "")
        if not code:
            err(f"{_where(r)}: {r['name']}: an alarm needs a display code")
        elif len(code) > ALARM_CODE_MAX:
            err(f"{_where(r)}: {r['name']}: code '{code}' is {len(code)} chars, "
                f"the ALARMS page shows {ALARM_CODE_MAX}")
        desc = r.get("desc", "")
        if len(desc) > ALARM_DESC_MAX:
            err(f"{_where(r)}: {r['name']}: description is {len(desc)} chars; it "
                f"is also the log line, capped at {ALARM_DESC_MAX}")


def _field_bus(rows, err):
    bus = []
    for kind, r in rows:
        dev = _int(r.get("dev", ""))
        fc = _int(r.get("fc", ""))
        if dev is None or fc is None:
            continue                       # not a bound row
        bus.append((kind, r, dev, fc))

        # A row's register span must match the width its wire type needs, or a
        # 32-bit value reads half of itself and half of its neighbour.
        regs = _int(r.get("regs", "")) or 1
        if fc in BIT_FCS:
            if regs > 1:
                err(f"{_where(r)}: {r['name']}: a bit row spans {regs} registers")
        else:
            wire = r.get("wire") or r.get("type") or ""
            width = TYPE_WIDTH.get(wire)
            if width is None:
                err(f"{_where(r)}: {r['name']}: unknown wire type '{wire}'")
            else:
                want = (width + 1) // 2
                if regs != want:
                    err(f"{_where(r)}: {r['name']}: wire type {wire} needs {want} "
                        f"register(s), the row declares {regs}")

        # expire_stale() skips timeout 0, so an input on a device that goes quiet
        # would hold Q_GOOD for the life of the firmware. Device 0 is a LOCAL
        # channel (xp_of()), read every scan off a pin the firmware owns: there
        # is no bus to fall silent, so it needs no timeout.
        if kind in ("MI", "AI", "DI") and dev != 0:
            if not _int(r.get("timeout", "")):
                err(f"{_where(r)}: {r['name']}: a field-bus input needs a timeout, "
                    f"or it can never go stale")

    # Two rows on one device must not address the same registers. A read and a
    # write of one coil ARE the same coil; bit and register spaces are separate.
    for i, (_, a, da, fa) in enumerate(bus):
        sa = _span(a)
        if sa is None:
            continue
        for _, b, db, fb in bus[i + 1:]:
            if da != db or (fa in BIT_FCS) != (fb in BIT_FCS):
                continue
            sb = _span(b)
            if sb and sa[0] < sb[1] and sb[0] < sa[1]:
                err(f"{_where(b)}: {b['name']} [{sb[0]},{sb[1]}) overlaps "
                    f"{a['name']} [{sa[0]},{sa[1]}) on device {da}")


def enums(members, rows, err):
    """The enumerated vocabularies, and the rows written over them.

    @p members is enums.csv as read; @p rows the effective table. A vocabulary
    is a CONTRACT - the codes go out on the wire - so the rules here are about
    keeping one row's meaning from quietly becoming another's.
    """
    by_enum, seen_member = {}, {}
    for r in members:
        name, mem = r.get("enum", ""), r.get("member", "")
        if not name or not mem:
            err(f"{_where(r)}: an enum member needs both an enum and a member name")
            continue
        e = by_enum.setdefault(name, {"codes": {}, "labels": {}})

        # A member name lands in the ENCLOSING scope: these are unscoped enums,
        # because the tables and the site's code both spell them bare.
        if mem in seen_member and seen_member[mem] != name:
            err(f"{_where(r)}: member '{mem}' is already {seen_member[mem]}'s; "
                f"an unscoped enum puts it in the same scope")
        seen_member[mem] = name

        code = _int(r.get("code", ""))
        if code is None:
            err(f"{_where(r)}: {name}.{mem}: code '{r.get('code')}' is not a number")
        elif code in e["codes"]:
            err(f"{_where(r)}: {name}.{mem} and {e['codes'][code]} are both "
                f"code {code}; a code is what goes on the wire")
        else:
            e["codes"][code] = mem

        label = r.get("label", "")
        if not label:
            err(f"{_where(r)}: {name}.{mem} has no label; the label IS the "
                f"vocabulary an operator types")
        elif label in e["labels"]:
            err(f"{_where(r)}: {name}: '{label}' labels both {mem} and "
                f"{e['labels'][label]}")
        else:
            e["labels"][label] = mem

    for kind, r in rows:
        cell = r.get("str", "")
        if not cell or "::" in cell:
            continue          # a library vocabulary, named the library's way
        if cell not in by_enum:
            err(f"{_where(r)}: {r['name']}: '{cell}' is not an enum in "
                f"enums.csv")
            continue
        # A default names a MEMBER of that row's own vocabulary. Anything else
        # is a row defaulting to a code from another enum, or to a typo.
        d = r.get("def", "")
        if d and d not in by_enum[cell]["codes"].values():
            err(f"{_where(r)}: {r['name']}: default '{d}' is not a member of "
                f"{cell}")


# What an `access` cell may say. Empty is RW, which is the common case, so the
# column stays blank rather than repeating itself two hundred times.
ACCESS_WORDS = ("", "RW", "RO")


def _access(rows, err):
    """The access column: only a parameter has one, and only three words."""
    for kind, r in rows:
        cell = r.get("access", "")
        if not cell:
            continue
        if kind not in ("P", "PE", "PS"):
            err(f"{_where(r)}: {r['name']}: only a parameter has an access "
                f"level; nothing may write a reading")
        elif cell not in ACCESS_WORDS:
            err(f"{_where(r)}: {r['name']}: access '{cell}' is not one of "
                + ", ".join(w or "(empty)" for w in ACCESS_WORDS))


# The one word a `retain` cell may say. Empty is the common case: a row whose
# value is a setting or a reading, rather than a record of what happened.
RETAIN_WORDS = ("", "true")


def _retain(rows, err):
    """The retain column: parameters and computed globals, and one word."""
    for kind, r in rows:
        cell = r.get("retain", "")
        if not cell:
            continue
        if kind not in ("P", "PE", "PS", "G", "GR"):
            err(f"{_where(r)}: {r['name']}: only a parameter or a computed "
                f"global may be retained")
        elif cell not in RETAIN_WORDS:
            err(f"{_where(r)}: {r['name']}: retain '{cell}' is not one of "
                + ", ".join(w or "(empty)" for w in RETAIN_WORDS))


# What an `isr` cell may say, and the HAL::Pulse::Edge it becomes.
EDGES = {"rising": "EDGE_RISING", "falling": "EDGE_FALLING", "both": "EDGE_BOTH"}
# I1 (A0) and I4 (A3) share an EXTI line on the OPTA's part: at most one of the
# pair may carry an interrupt, so seven of the eight inputs can count at once.
EXTI_PAIR = (0, 3)
MAX_PULSE_CHANNELS = 7


def _pulses(rows, err):
    """Edge-counted local inputs, against what the part can actually do."""
    counted = []
    for kind, r in rows:
        mode = r.get("isr", "")
        if not mode:
            continue
        if kind != "DI":
            err(f"{_where(r)}: {r['name']}: only a DI row can be edge-counted")
            continue
        if _int(r.get("dev", "")) != 0:
            err(f"{_where(r)}: {r['name']}: only a LOCAL row (device 0) can be "
                f"edge-counted; a bus device has no interrupt line here")
            continue
        if mode not in EDGES:
            err(f"{_where(r)}: {r['name']}: isr '{mode}' is not one of "
                + ", ".join(sorted(EDGES)))
            continue
        ch = _int(r.get("reg", ""))
        if ch is None or not (0 <= ch <= 7):
            err(f"{_where(r)}: {r['name']}: channel {r.get('reg')} is not an "
                f"OPTA input (0=I1 .. 7=I8)")
            continue
        counted.append((ch, r))

    seen = {}
    for ch, r in counted:
        if ch in seen:
            err(f"{_where(r)}: {r['name']}: channel {ch} is already counted by "
                f"{seen[ch]['name']}")
        seen[ch] = r
    if all(c in seen for c in EXTI_PAIR):
        a, b = (seen[c] for c in EXTI_PAIR)
        err(f"{_where(b)}: {a['name']} (I1) and {b['name']} (I4) cannot both "
            f"count: the two share an interrupt line on this part")
    if len(counted) > MAX_PULSE_CHANNELS:
        err(f"{len(counted)} edge-counted inputs, but at most "
            f"{MAX_PULSE_CHANNELS} can be attached at once")


def _calibrations(rows, err):
    from vars_extract import CAL_FIELDS

    params = {r["name"]: r for kind, r in rows if kind in ("P", "PE", "PS")}
    for kind, r in rows:
        if kind not in ("AI", "AO"):
            continue
        named = {}
        for f in CAL_FIELDS:
            cell = r.get(f, "")
            if not cell:
                err(f"{_where(r)}: {r['name']}: calibration field {f} is empty")
                continue
            if cell.startswith("="):
                continue                   # a fixed K(), nothing to resolve
            pname = cell.split("@", 1)[0]
            if pname not in params:
                err(f"{_where(r)}: {r['name']}: {f} names '{pname}', "
                    f"which is not a parameter")
            else:
                named[f] = params[pname]

        # Both ends of a span must share a unit, or the scale is wrong by their
        # ratio - and mV against uA both look plausible.
        for lo, hi, what in (("cal_raw_min", "cal_raw_max", "raw"),
                             ("cal_eng_min", "cal_eng_max", "engineering")):
            a, b = named.get(lo), named.get(hi)
            if a and b and a.get("unit", "") != b.get("unit", ""):
                err(f"{_where(r)}: {r['name']}: the {what} endpoints disagree on "
                    f"their unit ('{a.get('unit')}' against '{b.get('unit')}')")


def _names(rows, err):
    """A name repeated within ONE table set is a mistake. Across the two it is
    redeclaration, which is a feature, so it is reported and not failed."""
    per_set = {}
    for kind, r in rows:
        per_set.setdefault(r["_set"], {}).setdefault(r["name"], []).append(r)
    for setname, names in per_set.items():
        for name, rs in names.items():
            if len(rs) > 1:
                err(f"{_where(rs[1])}: '{name}' is declared twice in {setname} "
                    f"(also {_where(rs[0])})")
    # A name shared ACROSS the two sets is redeclaration, which is a feature;
    # _shadows() has already reported it and checked what it may not change.
