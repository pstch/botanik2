# The variable table {#int_variables}

Everything the firmware exposes is a **variable**: a Modbus meter reading, a relay
coil, a value a control program computes, a setting. There is one model and one
table. Nothing carries a byte offset, so reordering a table can never silently
re-point an alarm or a display row at its neighbour.

The table is **generated**. `scripts/tables.py` merges the library's rows into the
site's, applies the site's overrides, orders the rows, assigns every index and
every generated register, resolves every name, and refuses a table that breaks a
rule. There is no registration pass and nothing is resolved at bring-up.

A row is declared along two fields:

| Field | Says | Values |
|---|---|---|
| `VarType` | what the value *is* | `T_BOOL`, `T_U8`…`T_U64`, `T_I8`…`T_I64`, `T_F32`, `T_ENUM`, `T_STR` |
| `Kind` | what the row IS | `K_PARAM`, `K_ALARM`, `K_STATUS`, `K_INPUT`, `K_OUTPUT`, `K_QUALITY` |

The kind is what drives behaviour, and it is what a table **filters and iterates
on** ("every alarm", "every input"):

| Kind | Behaviour |
|---|---|
| `K_PARAM` | editable and persistable, always good |
| `K_ALARM` | the boolean an `AlarmCondition` reads, carrying its own dwell policy |
| `K_STATUS` | computed by a law; **published** over MQTT and, with a register, over the Modbus TCP slave |
| `K_INPUT` | read from the field; drops to `Q_NONE` once `timeout_ms` passes with no refresh; always has a quality companion |
| `K_OUTPUT` | driven to `def` when nothing is driving it |
| `K_QUALITY` | the `_qlt` companion of the row before it |

Everything else a consumer used to ask a declared field for is DERIVED, from
fields the row carries anyway:

| Derived | Rule |
|---|---|
| `published(d)` | not a parameter, an alarm or a quality |
| `mapped(d)` | a register, and not a field row - an INPUT's `reg` addresses a DEVICE |
| `local(d)` | an INPUT or OUTPUT on device 0: one of the controller's own terminals |
| `analog(d)` | the wire carries a raw count and the value is derived through a calibration |
| `pulse(d)` | the row IS a running edge count on a local terminal |
| `writable(d)` | a parameter that is not `F_RDONLY` |

@note Local I/O is **not** a separate kind. A local row is an INPUT or OUTPUT
whose Modbus device address is 0, so the address and the behaviour can never
disagree.

## Declaring one

**In a CSV**, one row per line, one file per spelling: `params.csv`,
`params_enum.csv`, `params_text.csv`, `alarms.csv`, `status.csv`,
`status_enum.csv`, `modbus_in.csv`, `modbus_out.csv`, `analog_in.csv`,
`analog_out.csv`, `digital_in.csv`, `digital_out.csv`, `relay_out.csv`,
`pulse_in.csv`. The generator emits one `constexpr` factory call per row, and
`vars/declare.h` is the one definition of what each spelling MEANS: its arguments
are exactly what a row of that shape has, so a row that cannot be spelled has
nowhere to go - a relay with a plausibility band, an alarm with a timeout, a
parameter with a device address.

```cpp
// generated/vars.gen.cpp
P ("ntp_poll_sec", T_U32, 3600, 10, 86400, VAR_NONE, VAR_NONE, 0, "sec", 922,
   "NTP poll interval, in seconds", ""),
PE("mqtt_level", Logs::LOG_DEBUG, &Logs::level_label, 937,
   "lowest level published to the broker", ""),
```

The whole array is `const` at namespace scope, so it constant-initialises into
flash.

@note Every factory ends with `desc` and `notes`, and `notes` is never optional:
`""` says "nothing more to add" in the same shape as a real note. `SU`/`SI`/`SF`/
`SB` build the typed `Scalar` - a bare literal cannot, since the union takes
`uint64`/`int64`/`float`/`const char*` equally well, and `Scalar(int)` is deleted
so the error names itself.

@note **`PE()` has no band**, and that is the point. Its converter is the row's
whole vocabulary: it renders a code as a word, parses a word back (`params set
mqtt_level warning`), and a code it cannot name is REFUSED - by `param_set`, by
the typed overload, and by `override_set`, which skips a band but not this. A set
of codes is not an interval, so a `min`/`max` would admit every gap between them.
A site declares its vocabularies in `tables/enums.csv`; the library's own
(`Logs::level_label`, `Plc::health_label`) stay in their subsystems and a row
names them there. The codes are what reach the store and an external master, so a
vocabulary may gain labels but must never renumber them.

@note **`def` does three jobs**, which is why every factory takes one: an input's
substitute when nothing usable is behind the row, an output's **safe state** when
nothing is driving it, and a parameter's factory value. A dead sensor reads as
`def`, so choosing a `def` outside the plausibility band means the control
programs treat a dead sensor as a fault without testing `valid()` at all.

@note A parameter also declares its **access**: `RW`, or `RO` for one an external
master may read and never write. Empty means `RW`. It is a Modbus rule - the
console is not levelled - and it is the only thing that sets `F_RDONLY`.

### The row

`Vars::Descriptor` is a flash-resident aggregate, **72 bytes** on the M7. Field
order is load-bearing: `pay` is 8-aligned, so the four 4-byte string pointers sit
after it and land in the tail hole rather than buying padding.

| Field | Type | Meaning |
|---|---|---|
| `def` | `Scalar` | invalid-read substitute / safe state / factory value |
| `pay` | `Payload` | **union**: a `Band`, a `T_STR` slot SIZE (never its offset - the bank lays those out), an alarm's policy, or an analog row's `const AnalogCal*` |
| `name` | `const char*` | symbolic name - also the telemetry leaf and the storage key |
| `unit`, `desc`, `notes` | `const char*` | display text |
| `bind` | `Binding` | `{ reg, device, fc, regs, wire, flags }` - 8 bytes |
| `timeout_ms` | `uint32_t` | `K_INPUT` staleness; 0 = never |
| `poll_ms` | `uint16_t` | how often to ASK; 0 derives it from the timeout |
| `kind`, `type` | enums | |
| `labels` | `const char* (*)(uint64_t)` | `T_ENUM` renderer, null for a plain number |
| `digits` | `uint8_t` | `T_F32` display decimals |
| `flags` | `uint8_t` | `F_INVERT`, `F_RDONLY`, `F_RETAIN`, `F_QLT` |

`Binding::wire` is the type **on the wire**, deliberately distinct from the
variable's own `type`: an analog row's driver converts a raw count into
engineering units, so control logic never sees the count. `Binding::flags` says
how the driver reaches the row - `B_WORD_SWAP`, `B_PULSE`, `B_ANALOG` - and it is
`B_ANALOG` that makes a row analog and its payload a calibration pointer.

@note An input's band is what `publish()` judges a reading against, and it is the
second half of input validity. `timeout_ms` catches a reading nothing refreshed;
the band catches one that is perfectly fresh and still nonsense - a dead 4-20 mA
loop reads full scale forever, and no timeout will ever notice.

### A band whose end is another row

`params.csv` and `modbus_in.csv` carry a `lo_row` / `hi_row` beside the numbers.
Name a row there and that end reads the row's value NOW, so a pair with a
required relationship - a target's floor and ceiling, a ramp's start and end -
cannot be edited into an impossible one. The number beside a named end is the
FALLBACK, and what the generator checks the default against.

Constrain BOTH ends against each other. The pair stays reachable as long as it
starts valid - widen by moving the ceiling first, narrow by moving the floor
first - and the generator refuses to emit a table whose default sits outside its
own band.

A band whose two ends are equal and both fixed is NO band; that is what a row
with no meaningful bounds carries.

### Analog calibration

An analog row carries no numbers of its own. It points at a shared
`const AnalogCal` of six cells, each independently a firmware constant or a
parameter an operator can edit. `analog_cal.csv` declares them, a `*_row` column
beside each number:

```
cal             ,raw_min_row         ,raw_min,raw_max_row         ,raw_max ,...
CAL_pwr_dc1_V   ,pwr_dc1_rawMin_mV   ,4000.0f,pwr_dc1_rawMax_mV   ,6000.0f ,...
```

The endpoints and the band are separate pairs on purpose: the endpoints are where
the sensor was calibrated, the band is what counts as a believable reading, and
the band is usually wider. A value just past the top endpoint is still a
measurement.

`scale` and `offset` are **not stored anywhere**. `Var::cal()` resolves the six
and derives them, so re-trimming a sensor takes effect the moment the parameter
commits and there is no second copy to update. It also gives `to_eng()`,
`to_raw()` (for an output) and `plausible()`, and floors a degenerate span at
scale 0 rather than dividing by zero.

The number beside a named cell is the **fallback**, used where the cell is fixed.

@note A calibration is **shared by being named**, not by being pointed at. One
block is generated per row, and two rows naming the same six parameters resolve
to the same numbers - so there is no third object to keep in step with them, and
the sharing is visible in the table rather than in a pointer.

@warning Anything that DISPLAYS or CHECKS a band must use `Var::band()`. An
analog row's band lives in its calibration and a parameter's end may be another
row, so `declared_band()` would show the firmware fallback while the operator is
looking at the numbers they just changed.

@warning `pay` is a union discriminated by the kind, the type and `B_ANALOG`.
**Never touch it directly.** Read it through `declared_band(d)`, `text_slot(d)`,
`analog_cal(d)` or `alarm_spec(d)`, which check the discriminant and return an
inert answer off-variant. This matters more than it looks: a text row's slot is
*written through*, so a numeric row read as text would hand the pool an arbitrary
offset.

Every factory body is a **single return statement**, which C++11 `constexpr`
requires and which is what keeps the whole table constant-initialised into flash
rather than `.data` behind a startup loop. Verify it after a change: `.data`
should not grow.

## Referring to one

By INDEX, through a generated constant:

```cpp
io.var(Vars::Row::cli_temp_hi).as_f32();
```

`generated/rows.gen.h` gives one `const uint16_t` per row, library and site
alike, in `namespace Botanik::Vars::Row`. The build points the library at it with
`BOTANIK_SITE_ROWS`, exactly as `BOTANIK_SITE_CONFIG` carries the config. **A name
no table declares is a compile error**, which is the whole point: it used to be a
reference that resolved to an inert sink and read zero for the life of the run.

An ST program names its rows the same way without writing anything: the generated
`plant.cpp` defines `ROW_<NAME>` for every `VAR_EXTERNAL`, and `__INIT_EXTERNAL`
binds `Ref<type>{ ROW_<NAME> }`.

`Bank::find(name)` stays for the two callers that have a STRING rather than a
name known at compile time: the console, and a log sink naming the parameter its
floor follows. It binary-searches a generated, case-folded order.

@warning An index is a bare `uint16_t` and **zero is a real row**. Never read one
back out of a `const` table it was declared in - a zero-initialised field is row
0, not "none". `VAR_NONE` is `0xFFFF` for that reason, and it resolves to an inert
**sink** that reads zeros with quality `Q_NONE`.

## Reading and writing at runtime

`Vars::Var` is the one handle: a `(bank, index)` pair, four bytes on the M7, made
on the fly and passed by value. Every row-level operation is on it.

| | |
|---|---|
| Read | `value()`, `live()`, `declared()`, `wire()`, `text()`, `as_f32()`, `as<T>()`, `quality()`, `usable()`, `valid()`, `stamp()`, `written()`, `age_ms()` |
| Bounds | `band()`, `cal()`, `implausible()` |
| Driver writes | `publish(Scalar, now_ms)` for a 1:1 row, `publish_wire(raw, now_ms)` for a converted one, `set_quality()`, `safe()` |
| Law writes | `set(bool)`, `set(float)`, `set_i(int64_t)` |
| Parameter edit | `param_set(text)`, `param_set(value)`, `param_check(...)` - RAM only |
| Override | `override_on()`, `override_set(...)`, `override_clear()` - RAM only; `value()` is the forced number when `override_on()` |
| Quality | `quality_var()`, the companion's own handle |

An unusable variable reads as its declared `def` **to a law** - `Ref::get()` is
where that substitution happens, once. `Var::value()` does not substitute.

Two distinct writes:

- `param_set()` edits a **parameter**: validated against its live band and
  applied, but NOT written to storage. Anything else returns `SET_INVALID`;
  writing a sensor or an output would just be overwritten by the next scan.
- `override_set()` **forces any variable**: type-checked only, so a reading can be
  pushed past its plausibility band to exercise the alarm watching for it.

**`Vars::Persist` is the durable half**, and every verb with a storage side is
there: `save`, `save_all`, `save_override`, `override_clear`, `reset`, `revert`,
`flush`. Consumers reach it through `SystemContext::persist`, the way they reach
the bank through `SystemContext::vars`.

@warning Release an override through **`Persist::override_clear()`**, not through
the handle. The handle cannot reach the store, and a release that leaves the
stored copy behind comes back at the next boot.

Overrides are applied inside `Var::value()` - the single point every reader goes
through - so a forced value reaches control logic, console, telemetry and alarms
alike. See @ref dev_vars for how that is implemented, and @ref user_variables for
what it means operationally.

## What checks the table

**The generator**, at generate time, against the whole table - which is the one
place it is all visible at once:

- row names unique under case folding, across the library and the site
- registers unique, present on every kind an external master addresses, and out
  of the blocks the generated rows are numbered from
- local terminals unique per direction
- every band end and every calibration cell names a row that exists and shares
  the referring row's type
- every default inside its own band, a named end contributing that row's default
- every home field names a row
- every alarm code short enough for the ALARMS page
- an enumeration ST can spell: contiguous from zero, since matiec numbers one by
  declaration order

`tables.py plant` adds what only matiec's own answer can give, off the
`__SET_EXTERNAL` list its compile produces: a row declared writable that the body
never writes, an output/alarm/status row no program writes at all, a quality an
opted-in program owes and does not write, and a parking program that never says
it parked.

**The suite** is left with BEHAVIOUR: `tests/test_site.cpp` runs the whole site -
every program once, plus the simulated field - and checks that nothing moved a
parameter it does not own.

On hardware there is no suite to run, and there is nothing left for one to catch:
a name no table declares does not compile, and a table that breaks a rule is not
emitted.
