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
| `ROLE_PARAM` | persisted, editable, always valid |

The scope drives nothing. It is what a table **filters and iterates on** ("every
alarm", "every relay"), and it is declared rather than reconstructed from the other
three - which is what it used to be, at every display site, with three fields having
to agree.

| Scope | Row |
|---|---|
| `A` | alarm: the boolean an `AlarmCondition` reads |
| `P` | parameter: persisted, editable |
| `G` | computed value, HMI only - rename it freely |
| `SV` | status value, **published** over MQTT and the Modbus TCP slave, so its name is an external contract |
| `MI` / `MO` | direct input / output: the register maps 1:1, no conversion |
| `AI` / `AO` | analog input / output: a raw count via `scale`/`offset` |
| `DI` / `DO` | digital input / output |
| `RO` | relay output (a coil) |

@note Local I/O is **not** a separate scope. A local row is one whose Modbus device
address is 0, and the factories derive `transport` from that, so the address and the
transport can never disagree.

## Declaring one

Not by spelling the struct. Each scope has one `constexpr` factory in
`vars/declare.h`, named after it, and it takes **only** the arguments that scope can
use - which is what makes a relay with a plausibility band, or an alarm with a
register, fail to compile:

```cpp
const Variable g_vars[] = {
    P ("ntp_poll_sec", T_U32, 3600, 10, 86400, 0, "sec", 10,
                     "NTP poll interval, in seconds", ""),
    PS("ntp_srv1",   DEF_NTP_SRV1, STR_CAP, STR_HOST, 11, "NTP server 1", ""),
    MI("grid_L1_V",  T_F32, 5, 3, 0, 2, SF(0.0f), SF(500.0f), SF(0.0f), 5000, 1,
                     "V", "L1 phase voltage", ""),
    A ("alarm_too_hot", "TOO_HOT", 2, 2, 0, 0, 10,
                     "Inside temperature too high", "check vents and shade"),
    SV("sys_supply_hz", T_F32, 0, 2, "Hz", 501, "deadbanded supply frequency", ""),
    RO("cli_window1_opn", 31, 0, false, false, "window 1 open", ""),
};
```

Every factory ends with `desc` and `notes`, and `notes` is never optional: `""` says
"nothing more to add" in the same shape as a real note. `SU`/`SI`/`SF`/`SB` build the
typed `Scalar` - a bare literal cannot, since the union takes `uint64`/`int64`/`float`/
`const char*` equally well, and `Scalar(int)` is deleted so the error names itself.

@note **`def` does three jobs**, which is why every factory takes one: an input's
substitute when the reading is invalid, an output's **safe state** when control is
withdrawn, and a parameter's factory value. A dead sensor reads as `def`, so choosing
a `def` outside the plausibility band means the control programs treat a dead sensor
as a fault without testing `valid()` at all.

### The row

`Vars::Variable` is a flash-resident aggregate, **72 bytes** on the M7. Field order is
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
| `flags` | `uint8_t` | `F_INVERT`, `F_BOUNDED`, `F_RDONLY` |

`Binding::wire` is the type **on the wire**, deliberately distinct from the variable's
own `type`: for an `AI`/`AO` row the driver computes `value = wire * scale + offset`,
so control logic never sees raw counts.

@note `F_BOUNDED` on an input is what `Bank::reject_implausible()` acts on, and it is
the second half of input validity. `timeout_ms` catches a reading nothing refreshed;
`F_BOUNDED` catches one that is perfectly fresh and still nonsense - a dead 4-20 mA
loop reads full scale forever, and no timeout will ever notice.

### Analog calibration

An `AI` / `AO` row carries no numbers of its own. It points at a shared
`const AnalogCal` of six **tunables**, each independently a firmware constant or a
parameter an operator can edit:

```cpp
constexpr Tunable K(float v);                            // fixed in firmware
constexpr Tunable R(const char* name, float fallback);   // read from a parameter

const AnalogCal CAL_PWR_DC1 = {
    R("pwr_dc1_rawMin_mV", 4000.0f), R("pwr_dc1_rawMax_mV", 6000.0f),  // wire units
    R("pwr_dc1_engMin_V",    20.0f), R("pwr_dc1_engMax_V",    30.0f),  // engineering units
    R("pwr_dc1_min",         20.0f), R("pwr_dc1_max",         30.0f),  // plausibility band
};
AI("pwr_dc1", &CAL_PWR_DC1, 11, 3, chan, 1, T_U16, SF(0.0f), 5000, 1, "V", "PS1 bus", ""),
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

@note A block may be **shared** by several rows. Sharing the block shares its
parameters, so the rows can never be trimmed apart - right when two readings must
agree by construction, wrong when they merely use the same part number.

@warning Anything that DISPLAYS a band must use `var_band(bank, d)`, not
`num_band(d)`. An analog row's band lives in its calibration, so `num_band()` would
show the firmware defaults while the operator is looking at the numbers they just
changed.

@warning `pay` is a union discriminated by `type == T_STR`: a numeric row holds
`{min, max}`, a text row holds `{off, cap, kind}`. **Never touch `pay` directly.**
Read it through `num_band(d)` or `text_slot(d)`, which check the discriminant and
return an unset band or a slot of capacity 0 off-variant. This matters more than it
looks: `off` is *written through* (`copy_bounded(pool + off, ...)`), so a numeric row
read as text would hand the pool an arbitrary offset. `tests/test_site_names.cpp`
asserts no row's inactive arm is reachable, and that no two text slots overlap.

Every factory body is a **single return statement**, which C++11 `constexpr` requires
and which is what keeps the whole table constant-initialised into flash rather than
`.data` behind a startup loop. Verify it after a change: `.data` should not grow.

## Referring to one

`VarRef` is a bare index - `struct VarRef { uint16_t index; }`, a struct rather than
a typedef so a stray `int` cannot become a reference. `VAR_NONE` is `0xFFFF`,
deliberately not 0, so a zero-initialised `VarRef` does not alias row 0.

Nothing outside the bank writes an index down. The status groups, the home fields,
the alarm conditions and the control programs all spell the variable's **name**, and
`Bank::find()` resolves it - once, at bring-up for the declarative tables. There used
to be a parallel `enum VarId` here, generated from the row order; keeping 249
enumerators in step with 249 rows by hand bought nothing that a name does not, and
its `static_assert` caught a changed *count* but never a re-ordering.

@note A name that matches no row, and any bad index, resolves to an inert **sink** -
not to a clamped row, which would drive a real relay. The bank counts every failed
lookup (`unresolved_count()`) and logs the name, so a typo shows up in the test suite
instead of reading as a dead variable forever.

@note `find()` binary-searches a mapping the bank sorts once at construction, out of
the `index` array a site hands it in its `VarTables` - `uint16_t name_index[VAR_TOTAL]`
beside the value store. Forget it and every lookup walks the whole table;
`tests/test_site_names.cpp` fails so nobody finds out the slow way.

@warning A `VarRef` sitting in a `const` table is zero, and **zero is a real row**.
Resolve names into a mutable array at bring-up and read them there; never read a
resolved index back out of the flash table it was declared in.

## Binding the table

Two arrays and one macro:

```cpp
// vars.cpp - defined exactly once, or a second TU including the site breaks the link
Value g_var_values[VAR_TOTAL];
char  g_str_pool[STR_POOL_BYTES];
```

```cpp
#define SITE_VAR_TABLES { \
    g_vars, Site::Store::values, VAR_TOTAL, \
    Site::Store::pool, STR_POOL_BYTES, Site::Store::name_index }
```

`VarTables` is `{ var, val, count, pool, pool_bytes, index }`, and `VAR_TOTAL` is
`sizeof(g_vars) / sizeof(g_vars[0])` - derived, so there is no second count to keep
in step.

@warning **Do not omit `index`.** It is the scratch `Bank`'s constructor sorts into
a name -> row mapping, and it is what makes `find()` a binary search. A null one is
legal and leaves every lookup walking the whole table - correct, but the cost the
index exists to avoid, on the call every site table and every control program makes
at bring-up. `Bank::name_index_built()` reports it and `tests/test_site.cpp` fails
on it, so this is caught - but only if the site is in the suite.

@warning `SITE_VAR_TABLES` is a **macro, not a function**. Any function call in that
initialiser would make the `SiteDescriptor` dynamically initialised and drag the
whole thing out of flash into `.data`.

@warning **`ROLE_PARAM` rows must be one contiguous block.** The parameter pages
address them with a dense 0-based index and one add. Where the block sits does not
matter - the bank locates it by scanning the roles - but a parameter outside the run
is unreachable: shown nowhere, editable nowhere, silently not persisted.

## Reading and writing at runtime

`Vars::Var` is the one non-owning handle over a `(descriptor, slot)` pair. It is
made on the fly and passed by value.

| | |
|---|---|
| Read | `raw()`, `value()`, `b()`, `i()`, `valid()`, `stamp()` |
| Write | `set(bool)`, `set(float)`, `set_i(int64_t)` |
| Publish | `publish(Scalar, now_ms)` - value, stamp and validity in one step; what a driver calls |
| Safe state | `safe()` |
| Override | `overridden()` to read it. The **mutators are private to `Bank`** |

An invalid variable reads as its declared `def`.

**`Vars::Bank` is the one object that gets, sets and overrides variables**, and it is
what consumers reach through `SystemContext::vars` rather than being handed their own
pointer. Two distinct writes:

- `set()` edits a **parameter**: validated against its declared band and persisted.
  Anything else returns `SET_INVALID` - writing a sensor or an output would just be
  overwritten by the next scan.
- `override_set(h, ...)` / `override_freeze(h)` / `override_clear(h)` **force any
  variable**: range-checked against the type only, so a reading can be pushed past its
  plausibility band to exercise the alarm watching for it.

@warning Go through the **bank**, never `Var`. Every override mutator has a storage
side - each override is persisted under its own `ovr.<name>` key - that only the bank
can reach, and reaching past it produced exactly the two bugs you would expect: a
forced quality that did not persist, and a released override that came back at the
next boot. That is why `Var`'s copies are private with `Bank` as the only friend.

Overrides are applied inside `Var::raw()` / `valid()` - the single point every reader
goes through - so a forced value reaches control logic, console, telemetry and alarms
alike. See @ref dev_vars for how that is implemented, and @ref user_variables for
what it means operationally (including why a restored override on an **output** is
announced as a critical, and why `control disable` deliberately leaves overrides
alone).

## Static asserts to carry

Each of these guards a **silent** truncation, so a site should keep all of them:

```cpp
static_assert(STR_POOL_BYTES >= 8 * STR_CAP, "the string pool is too small");
```

Names are checked at test time rather than build time, in `tests/test_site_names.cpp`:
every name in every table resolves, the parameters are one contiguous run, no two rows
share a name, and running the control programs and the simulated field leaves
`unresolved_count()` at zero and every parameter at its default.

Two more belong with the tables they guard - see @ref int_tables.
