# The variable table {#int_variables}

Everything the firmware exposes is a **variable**: a Modbus meter reading, a relay
coil, a value a control program computes, a setting. There is one model and one
table. Nothing carries a byte offset, so reordering a table can never silently
re-point an alarm or a display row at its neighbour.

Variables differ along independent **fields**, never types:

| Field | Says | Values |
|---|---|---|
| `VarType` | what the value *is* | `T_BOOL`, `T_U8`…`T_U64`, `T_I8`…`T_I64`, `T_F32`, `T_ENUM`, `T_STR` |
| `Transport` | how it reaches the field | `XP_NONE`, `XP_LOCAL`, `XP_MODBUS` |
| `VarRole` | what it is **for** | `ROLE_INPUT`, `ROLE_OUTPUT`, `ROLE_COMPUTED`, `ROLE_PARAM` |
| `VarScope` | which **kind of row** it is | `A`, `P`, `G`, `SV`, `MI`, `AI`, `DI`, `MO`, `AO`, `DO`, `RO` |

The role is what drives behaviour:

| Role | Behaviour |
|---|---|
| `ROLE_INPUT` | goes invalid once `timeout_ms` passes with no refresh |
| `ROLE_OUTPUT` | driven to `def` when control authority is withdrawn |
| `ROLE_COMPUTED` | never expires, never swept |
| `ROLE_PARAM` | editable and persistable, always valid |

The scope drives nothing. It is what a table **filters and iterates on** ("every
alarm", "every relay"), and it is declared rather than reconstructed from the other
three - which is what it used to be, at every display site, with three fields having
to agree.

| Scope | Row |
|---|---|
| `A` | alarm: the boolean an `AlarmCondition` reads |
| `P` | parameter: editable, saved on demand. Written `P` (numeric, banded), `PS` (text) or `PE` (enumerated - its labels are its bound). One carrying `retain` records what happened rather than how the plant is set, and `params reset` leaves it alone |
| `G` | computed value, HMI only - rename it freely. `GR` is the same thing RETAINED: its value survives a reboot |
| `SV` | status value, **published** over MQTT and the Modbus TCP slave, so its name is an external contract. `SVE` is one rendered through an enum converter; pass `retained` for a value that survives a reboot - a water totalizer, hours run |
| `MI` / `MO` | direct input / output: the register maps 1:1, no conversion |
| `AI` / `AO` | analog input / output: a raw count via `scale`/`offset` |
| `DI` / `DO` | digital input / output |
| `RO` | relay output (a coil) |

@note Local I/O is **not** a separate scope. A local row is one whose Modbus device
address is 0, and the factories derive `transport` from that, so the address and the
transport can never disagree.

## Declaring one

**In C++, one `constexpr` factory call per row**, in `vars_table.cpp`. The
factory is the scope: its arguments are exactly what a row of that scope has, so
a row that cannot be spelled has nowhere to go - a relay with a plausibility
band, or an alarm with a type.

```cpp
const Descriptor params[] = {
    P ("ntp_poll_sec", T_U32, 3600, 10, 86400, 0, "sec", 922, "NTP poll interval", "", false),
    PE("sys_log_level", Logs::LOG_DEBUG, &Logs::level_label, 973,
                     "lowest level the logger forwards", ""),
    ...
};
```

`vars/declare.h` is the one definition of what a scope MEANS, and the whole
array is `const` at namespace scope, so it constant-initialises into flash.

@note Every factory ends with `desc` and `notes`, and `notes` is never optional:
`""` says "nothing more to add" in the same shape as a real note. `SU`/`SI`/`SF`/
`SB` build the typed `Scalar` - a bare literal cannot, since the union takes
`uint64`/`int64`/`float`/`const char*` equally well, and `Scalar(int)` is deleted
so the error names itself.

@note **`PE()` has no band**, and that is the point. Its converter is the row's
whole vocabulary: it renders a code as a word, parses a word back (`params set
sys_log_level warning`), and a code it cannot name is REFUSED - by `set`, by the
typed overload, and by `override`, which skips a band but not this. A set of
codes is not an interval, so a `min`/`max` would admit every gap between them.
A site declares its vocabularies in `vars_table.h`, the enum beside its
converter; the library's own (`Logs::level_label`,
`Plc::health_label`) stay in their subsystems and a row names them there.
The codes are what reach the store and an external master, so a vocabulary may
gain labels but must never renumber them.

@note **`def` does three jobs**, which is why every factory takes one: an input's
substitute when the reading is invalid, an output's **safe state** when control is
withdrawn, and a parameter's factory value. A dead sensor reads as `def`, so choosing
a `def` outside the plausibility band means the control programs treat a dead sensor
as a fault without testing `valid()` at all.

@note A parameter also declares its **access**: `RW`, or `RO` for one an external
master may read and never write. Empty means `RW`. It is a Modbus rule - the
console is not levelled - and it is the only thing that sets `F_RDONLY`.

### The row

`Vars::Descriptor` is a flash-resident aggregate, **72 bytes** on the M7. Field order is
load-bearing: `pay` is 8-aligned, so the 4-byte `str` pointer sits after it and lands
in the tail hole rather than buying four bytes of padding.

| Field | Type | Meaning |
|---|---|---|
| `def` | `Scalar` | invalid-read fallback / safe state / factory value |
| `name` | `const char*` | symbolic name - also the telemetry leaf and the storage key |
| `unit`, `desc`, `notes` | `const char*` | display text |
| `pay` | `Payload` | **union**: a numeric band, a `T_STR` slot SIZE (never its offset - `Bank::init()` lays those out into the `Value`), an alarm's policy, or an analog row's `const AnalogCal*` |
| `bind` | `Binding` | `{ transport, wire, device, fc, reg, regs, scale, offset }` |
| `timeout_ms` | `uint32_t` | `ROLE_INPUT` staleness; 0 = never |
| `str` | `const char* (*)(uint64_t)` | `T_ENUM` renderer, null for a plain number |
| `type`, `role`, `scope` | enums | |
| `digits` | `uint8_t` | `T_F32` display decimals |
| `flags` | `uint8_t` | `F_INVERT`, `F_BOUNDED`, `F_RDONLY`, `F_RETAIN` |

`Binding::wire` is the type **on the wire**, deliberately distinct from the variable's
own `type`: for an `AI`/`AO` row the driver computes `value = wire * scale + offset`,
so control logic never sees raw counts.

@note `F_BOUNDED` on an input is what `publish()` judges a reading against, and it is
the second half of input validity. `timeout_ms` catches a reading nothing refreshed;
`F_BOUNDED` catches one that is perfectly fresh and still nonsense - a dead 4-20 mA
loop reads full scale forever, and no timeout will ever notice.

### Analog calibration

An `AI` / `AO` row carries no numbers of its own. It points at a shared
`const AnalogCal` of six **tunables**, each independently a firmware constant or a
parameter an operator can edit:

Two builders declare each cell, and the block is named after the row:

```cpp
constexpr Tunable K(float v);                            // fixed in firmware
constexpr Tunable R(const char* name, float fallback);   // read from a parameter

const AnalogCal CAL_pwr_dc1_V = {
    R("pwr_dc1_rawMin_mV", 4000.0f), R("pwr_dc1_rawMax_mV", 6000.0f),  // wire units
    R("pwr_dc1_engMin_V",    20.0f), R("pwr_dc1_engMax_V",    30.0f),  // engineering units
    R("pwr_dc1_min",         20.0f), R("pwr_dc1_max",         30.0f),  // plausibility band
};
AI("pwr_dc1_V", &CAL_pwr_dc1_V, 11, 4, 2, 1, T_U16, SF(0.0f), 5000, 1, "V", "DC PS1 bus voltage", ""),
```

The endpoints and the band are separate pairs on purpose: the endpoints are where the
sensor was calibrated, the band is what counts as a believable reading, and the band
is usually wider. A value just past the top endpoint is still a measurement.

`scale` and `offset` are **not stored anywhere**. `Vars::analog_values(bank, cal)`
resolves the six and derives them, so re-trimming a sensor takes effect the moment the
parameter commits and there is no second copy to update. It also gives `to_eng()`,
`to_raw()` (for an output) and `plausible()`, and floors a degenerate span at scale 0
rather than dividing by zero.

`R()`'s second argument is the **fallback**: a parameter that is not in the table
reads as that constant, so a mistyped name degrades to a sane number instead of zero
(and the bank counts the miss, which the site test asserts never happens).

@note A calibration is **shared by being named**, not by being pointed at. One
block is generated per row, and two rows naming the same six parameters resolve
to the same numbers - so there is no third object to keep in step with them, and
the sharing is visible in the table rather than in a pointer. The generated site
report derives the groups from exactly that and lists each one's six roles.

@warning Anything that DISPLAYS a band must use `var_band(bank, d)`, not
`num_band(d)`. An analog row's band lives in its calibration, so `num_band()` would
show the firmware defaults while the operator is looking at the numbers they just
changed.

@warning `pay` is a union discriminated by `type == T_STR`: a numeric row holds
`{min, max}`, a text row holds `{off, cap, kind}`. **Never touch `pay` directly.**
Read it through `num_band(d)` or `text_slot(d)`, which check the discriminant and
return an unset band or a slot of capacity 0 off-variant. This matters more than it
looks: `off` is *written through* (`copy_bounded(pool + off, ...)`), so a numeric row
read as text would hand the pool an arbitrary offset. `tests/test_site.cpp`
asserts no row's inactive arm is reachable, and that no two text slots overlap.

Every factory body is a **single return statement**, which C++11 `constexpr` requires
and which is what keeps the whole table constant-initialised into flash rather than
`.data` behind a startup loop. Verify it after a change: `.data` should not grow.

## Referring to one

`VarRef` is a bare index - `struct VarRef { uint16_t index; }`, a struct rather than
a typedef so a stray `int` cannot become a reference. `VAR_NONE` is `0xFFFF`,
deliberately not 0, so a zero-initialised `VarRef` does not alias row 0.

Nothing outside the bank writes an index down. The home fields, the alarm rows and
the control programs all spell the variable's **name**, and `Bank::find()`
resolves it - once, at bring-up. A control program names each row once, in its
`bind()`, through `_bind_input` / `_bind_output` / `_bind_inout` - which
resolve, check and claim in one call.

Those return a `Ref<T, Dir>` rather than a bare `VarRef` - the row's C++ TYPE and
its IEC 61131-3 declaration section, both compile-time. Driving a `VAR_INPUT`, or
reading a `T_U16` row as a float, does not compile. The INDEX still resolves at
bind time, because `freeze()` may have compacted a redeclared row out.

@note A name that matches no row, and any bad index, resolves to an inert **sink** -
not to a clamped row, which would drive a real relay. The bank counts every failed
lookup (`unresolved_count()`) and logs the name, so a typo shows up in the test suite
instead of reading as a dead variable forever.

@note `find()` binary-searches a name index the bank builds in `freeze()`, once,
after every set of rows has been registered. Before that it is a full scan, which
is correct and is why `Bank::name_index_built()` exists: bring-up resolves
hundreds of names, and doing it before the freeze would be the cost the index
exists to avoid.

@warning A `VarRef` sitting in a `const` table is zero, and **zero is a real row**.
Resolve names into a mutable array at bring-up and read them there; never read a
resolved index back out of the flash table it was declared in.

## Binding the table

The generator emits it. `register_all()` hands the bank the site's rows in the
one order that matters, and the site's `register_vars()` is the one call:

```cpp
// generated/vars.gen.h
inline void register_all(Vars::Bank& io) {
    io.register_vars(params, PARAMS_COUNT);   // PARAMETERS FIRST
    io.register_vars(rows,   ROWS_COUNT);
}
```

```cpp
// site.h
void register_vars() override { Site::register_all(io()); }
```

The value store, the string pool and the name index are the **bank's**, sized once
by `Config::VAR_MAX` rather than by every site counting its own rows. A site
declares no arrays for them.

@warning **`ROLE_PARAM` rows must be one contiguous block**, and the two-array
split above is what guarantees it: the library's parameters come last within its
own set and the site's come first within the site's, so the two runs abut. The
parameter pages address them with a dense 0-based index and one add - a parameter
outside the run is unreachable: shown nowhere, editable nowhere, silently not
persisted. `tests/test_site.cpp` walks the built bank for it, rather than a
comment asking for it.

@note The site's rows are registered AFTER the library's and share one flat index
space, so the two read as one table. A site may **restate** a library row by
declaring the same name: `Bank::freeze()` keeps the last declaration and compacts
the earlier one out, so the whole row is the site's. Keep the register the library
assigned and keep the type - the library binds these by both.

## Reading and writing at runtime

`Vars::Var` is the one non-owning handle over a `(descriptor, slot)` pair. It is
made on the fly and passed by value.

| | |
|---|---|
| Read | `raw()`, `value()`, `b()`, `i()`, `valid()`, `stamp()` |
| Write | `set(bool)`, `set(float)`, `set_i(int64_t)` |
| Publish | `publish(Scalar, now_ms)` for a 1:1 row; `Bank::publish_wire(ref, raw, now_ms)` for a scaled one, which the model converts and judges |
| Safe state | `safe()` |
| Override | `override()` to read it - `Q_NONE` when there is none. The **mutators are private to `Bank`** |

An invalid variable reads as its declared `def`.

**`Vars::Bank` is the one object that gets, sets and overrides variables**, and it is
what consumers reach through `SystemContext::vars` rather than being handed their own
pointer. Two distinct writes:

- `set()` edits a **parameter**: validated against its declared band and applied,
  but NOT written - `persist()` / `save_params()` do that. Anything else returns
  `SET_INVALID`; writing a sensor or an output would just be overwritten by the
  next scan.
- `override_set(h, ...)` / `override_freeze(h)` / `override_clear(h)` **force any
  variable**: range-checked against the type only, so a reading can be pushed past its
  plausibility band to exercise the alarm watching for it.

@warning Go through the **bank**, never `Var`. Every override mutator has a storage
side - `save_overrides()` writes each under its own `ovr.<name>` key, and releasing
one erases that key immediately - which only the bank can reach. Reaching past it
produced exactly the bug you would expect: a released override that came back at the
next boot. That is why `Var`'s copies are private with `Bank` as the only friend.

Overrides are applied inside `Var::raw()` / `valid()` - the single point every reader
goes through - so a forced value reaches control logic, console, telemetry and alarms
alike. See @ref dev_vars for how that is implemented, and @ref user_variables for
what it means operationally (including why a restored override on an **output** is
announced as a critical, and why `control disable` deliberately leaves overrides
alone).

## What checks the table

Everything a table can get wrong is checked at TEST time, in
`tests/test_site.cpp`, against the bank the site actually built: no two rows
share a register, every register sits in its scope's block, no two local rows
claim the same terminal, a default sits inside its own band, no two text rows
share pool bytes, the parameters are one contiguous run, and every alarm carries
a code short enough for the page.

What only a running bank can answer is checked there too: that every name every
table and every control program reaches for resolves, that `unresolved_count()`
is zero after the programs bind and the simulated field has run, and that no
parameter was written by accident.

On hardware there is no suite to run, so the same failure is reported instead of
checked: a program whose `bind()` reached for a name no row declares is
**FAULTED** for the life of the run, which the POST sees and the TASKS page
shows. A `VarRef` that resolves to nothing is the inert sink, which reads zero
for ever, and zero is a plausible number - the fault is the only thing that says
so.
