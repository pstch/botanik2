#!/usr/bin/env python3
"""The RS-485 segment, declared: what answers at each unit address.

Two tables, because a model is shared and a unit is not: twelve temperature
sensors are twelve `devices.csv` rows over ONE `device_models.csv` row.

Constraints, not signal maps. Where a reading lives is the row's business (the
variable tables); this is what a request must look like for the device to answer
at all - see modbus/rtu/device.h.
"""

import os

import vars_csv

MODELS_CSV = "device_models.csv"
DEVICES_CSV = "devices.csv"

MODEL_COLS = ["model", "shape", "size", "align", "multi", "gap_ms",
              "timeout_ms", "desc", "notes"]
DEVICE_COLS = ["unit", "model", "tag", "notes"]

# Shape -> the factory it is built through, and whether it takes an alignment.
SHAPES = {"meter": True, "io_board": False, "relay_board": False}

# The shape whose `size` is a COIL COUNT, so a coil past it is a row pointing at
# a terminal the board does not have.
COILS = "relay_board"

GATE = "BOTANIK_FEATURE_MODBUS_RTU_CLIENT"


def load(table_dir):
    """The two tables, or ([], []) where a site declares no field bus."""
    return (_read(os.path.join(table_dir, MODELS_CSV)),
            _read(os.path.join(table_dir, DEVICES_CSV)))


def _read(path):
    return vars_csv.read(path) if os.path.exists(path) else []


def _where(r):
    return f"{r.get('_file', '?')}:{r.get('_line', 0)}"


def _int(cell):
    try:
        return int(cell)
    except (TypeError, ValueError):
        return None


def check(models, devices, rows, err):
    """The two tables against each other, and against the rows addressing them.

    @p rows is every variable row as (kind, row), so a unit nobody declared and
    a device nobody reads are both reported: the first is a request the client
    would cut against the generic model, the second is dead weight.
    """
    by_model = {}
    for m in models:
        name = m["model"]
        if name in by_model:
            err(f"{_where(m)}: model '{name}' declared twice")
        by_model[name] = m
        if m["shape"] not in SHAPES:
            err(f"{_where(m)}: shape '{m['shape']}' is not one of "
                f"{', '.join(sorted(SHAPES))}")
        elif SHAPES[m["shape"]] and _int(m["align"]) is None:
            err(f"{_where(m)}: a {m['shape']} needs an alignment")
        elif not SHAPES[m["shape"]] and m["align"]:
            err(f"{_where(m)}: a {m['shape']} has no alignment rule")
        if _int(m["size"]) is None:
            err(f"{_where(m)}: size must be a number")
        if m["multi"] not in ("", "true", "false"):
            err(f"{_where(m)}: multi is true, false or empty")
        if not m["desc"]:
            err(f"{_where(m)}: desc is the name that reaches the logs")

    by_unit = {}
    for d in devices:
        unit = _int(d["unit"])
        if unit is None or not 1 <= unit <= 247:
            err(f"{_where(d)}: unit must be 1..247 (0 is the local terminal)")
            continue
        if unit in by_unit:
            err(f"{_where(d)}: unit {unit} declared twice")
        by_unit[unit] = d
        if d["model"] not in by_model:
            err(f"{_where(d)}: unit {unit} names model '{d['model']}', which "
                f"{MODELS_CSV} does not declare")
        if not d["tag"]:
            err(f"{_where(d)}: unit {unit} has no tag; a log line names the "
                f"cabinet rather than the unit number")

    if not devices:
        return
    used = set()
    for _, r in rows:
        unit = _int(r.get("dev", ""))
        if unit is None or unit == 0:
            continue
        used.add(unit)
        d = by_unit.get(unit)
        if d is None:
            err(f"{_where(r)}: {r['name']} addresses unit {unit}, which "
                f"{DEVICES_CSV} does not declare")
        else:
            _addressing(r, by_model.get(d["model"]), err)

    for unit in sorted(set(by_unit) - used):
        print(f"  note: unit {unit} ({by_unit[unit]['tag']}) is declared and no "
              f"row addresses it")


def _addressing(r, m, err):
    """One row against the model it addresses: the coil count, or alignment."""
    if m is None:
        return
    if m["shape"] == COILS:
        coil, size = _int(r.get("coil", "")), _int(m["size"])
        if coil is not None and size is not None and coil >= size:
            err(f"{_where(r)}: {r['name']} is coil {coil} on a {size}-coil "
                f"{m['model']}")
        return
    align = _int(m["align"]) or 1
    if align < 2:
        return
    reg, regs = _int(r.get("reg", "")), _int(r.get("regs", ""))
    if reg is not None and reg % align != 0:
        err(f"{_where(r)}: {r['name']} starts at register {reg}, and "
            f"{m['model']} answers only on multiples of {align}")
    if regs is not None and regs % align != 0:
        err(f"{_where(r)}: {r['name']} spans {regs} registers, and {m['model']} "
            f"answers only in multiples of {align}")


def emit(models, devices):
    """The device table as C++, gated: without the client nothing reads it.

    @return (header lines, .cpp lines). The objects are DEFINED in the .cpp
    because a `const` definition at namespace scope in a header has internal
    linkage, giving every including TU its own copy - and the bank matches an
    analog block by POINTER, so copies are not interchangeable.
    """
    if not devices:
        return [], []
    hdr = [f"#if {GATE}", "",
           "/** @name Field devices - what each unit on the segment answers",
           " *  @{ */", ""]
    src = [f"#if {GATE}", ""]
    for m in models:
        hdr += _doc(m["notes"] or m["desc"])
        hdr.append(f"extern const Modbus::DeviceModel MDL_{m['model']};")
        src += [f"const Modbus::DeviceModel MDL_{m['model']} =",
                f"    Modbus::{m['shape']}({_args(m)});", ""]
    hdr += ["",
            "/** @brief Every unit on the segment, and what the plant calls it:",
            " *  the tag is data, so a log line can name the cabinet. */",
            "extern const Modbus::Device g_devices[];",
            "extern const Modbus::DeviceTable g_device_table;",
            "", "/** @} */", "", f"#endif  // {GATE}", ""]

    src.append("const Modbus::Device g_devices[] = {")
    wu = max(len(d["unit"]) for d in devices)
    ref = ["&MDL_" + d["model"] + "," for d in devices]
    wm = max(len(m) for m in ref)
    for d, m in zip(devices, ref):
        src.append(f'    {{ {d["unit"]:>{wu}}, {m:{wm}} "{d["tag"]}" }},')
    src += ["};", "",
            "const Modbus::DeviceTable g_device_table = {",
            "    g_devices, (uint8_t)(sizeof(g_devices) / sizeof(g_devices[0]))",
            "};", "", f"#endif  // {GATE}", ""]
    return hdr, src


def _args(m):
    """Only as many arguments as the model actually constrains."""
    args = ['"' + m["desc"] + '"', m["size"]]
    if SHAPES[m["shape"]]:
        args.append(m["align"])
    tail = [m["multi"], m["gap_ms"], m["timeout_ms"]]
    while tail and not tail[-1]:
        tail.pop()
    return ", ".join(args + [c or _omitted(m["shape"], i)
                             for i, c in enumerate(tail)])


def _omitted(shape, i):
    """What the factory would have defaulted an argument to, once a later one
    forces it to be spelled."""
    return ("false" if shape == "meter" else "true") if i == 0 else "0"


def _doc(text, width=70):
    """A note as a doc block, wrapped at @p width."""
    lines, line = [], ""
    for w in text.split():
        if line and len(line) + 1 + len(w) > width:
            lines.append(line)
            line = w
        else:
            line = f"{line} {w}".strip()
    lines.append(line)
    if len(lines) == 1:
        return ["/** @brief " + lines[0] + " */"]
    return ["/** @brief " + lines[0]] + [" *  " + l for l in lines[1:-1]] \
        + [" *  " + lines[-1] + " */"]
