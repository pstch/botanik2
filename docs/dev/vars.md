# The variable model {#dev_vars}

`vars/` is the one model every value the firmware exposes goes through - a meter
reading, a relay coil, a computed status value, a setting. The *shape* of that
model (the descriptor fields, the kinds, what a site declares) is in
@ref int_variables; this page is how it works inside.

The whole table is **generated**. `scripts/tables.py` merges the library's rows
into the site's, applies the site's overrides, orders the rows, assigns every
index and every generated register, resolves every name, and checks. Nothing it
checks is checked again at runtime, and there is no registration pass, no
freeze, no name resolution and no counter reporting that one of those went
wrong.

## Split: const descriptor, RAM slot

Two parallel arrays, indexed together.

**`Descriptor`** is the flash half: name, unit, description, band, type, kind,
binding. It is `const` and `constexpr`-constructed so the whole table
constant-initialises into **flash**, not `.data`. That is why `Scalar` has
`constexpr` factories and why `SiteDescriptor` has no NSDMIs - one function call
in those initialisers would drag several kilobytes into RAM. 72 bytes on the M7.

**`Slot`** is the RAM half, pinned at 24 bytes:

```cpp
struct Slot {
    Scalar   v;
    Scalar   ovr;        // forced value, while S_OVR_ON
    uint32_t stamp_ms;   // low 32 bits of uptime at the last good update
    uint16_t str_off;    // T_STR only: this row's pool slot
    uint8_t  flags;      // SlotFlags
    uint8_t  wire_off;   // an analog row's index into the wire array
};
static_assert(sizeof(Slot) == 24, "Slot must stay 24 bytes (RAM per variable)");
```

There is no `def`: the default is a typed literal already in flash, so an
unusable read falls back to it without a per-slot copy. There is no QUALITY
either - that is a row of its own. And there is no companion INDEX, because the
companion of row `i` is row `i + 1`: the generator emits the pair together and
`F_QLT` on the first is the whole link.

The stamp is 32 bits and ages by unsigned subtraction, so the 49-day wrap costs
nothing. What the zero cannot say is "never written", which is `S_WRITTEN`.

@warning The size assert is not decoration. Every field here costs its width
**per variable, on every site, whether it is used or not** - the override alone
is 8 bytes across the whole table. Adding a field means spending that
everywhere; say so out loud when you do.

## One Kind, and what is derived from it

`Kind` is what a row IS, declared, in one field. Everything a table used to
filter on by scope, role or transport is either the kind or one comparison over
a field the row carries anyway.

| Kind | Payload | Binding | Companion |
|---|---|---|---|
| `K_PARAM` | band, labels, or text slot, by type | none | never |
| `K_ALARM` | alarm spec | none | never |
| `K_STATUS` | none | none | when its program opts in |
| `K_INPUT` | band, or a calibration pointer | device, fc, reg, regs, wire | always |
| `K_OUTPUT` | none, or a calibration pointer | device, fc, reg, regs, wire | never |
| `K_QUALITY` | none | none | never |

| Derived | Rule |
|---|---|
| `published(d)` | not a parameter, an alarm or a quality |
| `mapped(d)` | a register, and not a field row - an INPUT's `reg` addresses a DEVICE |
| `local(d)` | an INPUT or OUTPUT on device 0: one of the controller's own terminals |
| `analog(d)` | the binding says `B_ANALOG`: the wire carries a count, the value is derived |
| `pulse(d)` | the binding says `B_PULSE`: the row IS a running edge count |
| `writable(d)` | a parameter that is not `F_RDONLY` |

## `Var`: the one handle

`Var` is a `(bank, index)` pair - four bytes on the M7 - made on the fly and
passed by value. Every row-level operation hangs off it: reading, publishing,
editing it as a parameter, forcing it.

```cpp
float t = io.var(Row::cli_temp_ext).as_f32();
io.var(Row::irr_input_ro1).set(true);
```

Reads funnel through one accessor, and the value in force is separate from the
belief in it:

```cpp
Value value() const {
    return _flag(S_OVR_ON) ? Value{ _s().ovr, &meta(), type() } : live();
}
bool  valid() const { return measured(); }
```

A `Value` is a number, the type that number is IN, and the descriptor: enough to
convert it, saturating, and to name an enumerated code. It carries no quality,
and there is no accessor for the forced number on its own. Ask `override_on()`
first and `value()` IS the force, so an unforced row's stale `ovr` field cannot
be reached at all.

A default-built `Var` has no bank and is the sink: it reads zeros with quality
`Q_NONE` and drives nothing.

**A quality is the VALUE of a `_qlt` row.** `x_qlt` is a real row - descriptor,
register, name index - whose value is a `Vars::Quality`. It sits immediately
after `x`, and `x` carries `F_QLT`, so `quality()` is one indexed load and there
is no pairing pass and nothing to keep in step. A row with no companion is good,
always, and so is every companion, which is what stops the regress.

Three codes, and each is a DECISION a reader makes differently:

| Code | Meaning | `usable` | `measured` | Written by |
|---|---|---|---|---|
| `Q_NONE` | nothing usable behind the row | no | no | the companion's default, a safe-state drop, the expiry sweep, a bus failure, a short payload, `QUAL` unseeded |
| `Q_GOOD` | measured, restored, or forced | yes | yes | `publish` in band, a retained restore, a value override, `QUAL` ok |
| `Q_HELD` | a law holds a number it stands behind | yes | no | ST only, `QUAL` seeded |

There were six. The four that went were DIAGNOSES rather than decisions, and
each keeps a home: stale is the stamp's age on the monitor page, implausible is
the value shown outside the band beside it, never-written is `S_WRITTEN`, and a
refused register is one log line from the RTU master at the transition, naming
the row and the device.

There is no second store. A driver's `publish()` writes that row, `expire_stale()`
writes it, an ST law assigns it, an operator forces it - all the same byte, so
nothing can lag or disagree. `Q_NONE` is 0 on purpose: it is a companion's
declared default, so a row nothing has spoken for reads as "nothing has written
this" and never as good data.

**An unusable row reads its declared value TO A LAW.** `Ref::get()` is where the
substitution happens, once, and it branches on `usable()` - so a law computes on
`Q_HELD` too, which is the whole point of that code. `Var::value()` does not
substitute: the monitor page and telemetry show the number that was implausible,
with the quality beside it, which is what an operator chasing a dead probe needs.

Which rows get a companion is generated, not declared: one per INPUT, because a
reading can always stop being one, plus one per row a program that opted in
(`quality` in `tables/programs.csv`) writes. Opting in obliges the law to write
every one of them, which `tables.py plant` checks off matiec's own write list.

**The override is applied here, and only here.** Every reader in the firmware
goes through `value()`/`valid()`, so a forced value reaches control logic, the
console, telemetry and the alarm model alike. There is no path that sees around
it - which is exactly what makes an override useful for testing, and exactly why
it must be impossible to forget one is set.

Writes keep landing in `v` underneath while an override is in force, so the
override *shadows* rather than replaces: clearing it resumes the live value with
no gap and nothing to restore. The override carries no validity of its own:
"make this read as a dead sensor" is a force on the row's `_qlt` companion,
reached as `h.quality_var()`, which is an ordinary force of an ordinary row -
saved, released and listed the same way. Both spellings are
`overrides set <name> [<value>] [valid|invalid]` from the console; the second
word lands on the companion. A row with no companion has no quality to force,
and says so.

**A forced VALUE also forces the belief.** Forcing a dead probe's reading so the
plant runs on it is what an override is for, and inheriting the doubt underneath
would make that impossible - so `override_set()` writes `Q_GOOD` into the
companion, and releasing the value releases that too. Written into the store
rather than applied at read time on purpose: ST reads the companion ROW, so a
rule living in `Var` would be an answer only C++ could see. `... <value> invalid`
forces the companion afterwards and so wins.

Overrides **can be made durable, and are not by default**. `Var::override_set()`
applies the force and marks the slot `S_OVR_UNSAVED`; `Persist::save_override()`
writes it under an `ovr.<name>` key of its own, so the override and the parameter
setting beneath it stay separate facts - clearing one leaves the other untouched.
`Persist` restores what was saved, after the parameters, and a restored override
is announced as a CRITICAL at boot.

The default is the safe half of the trade: forcing a signal to chase a fault is
the common case and dies with the session, so a forgotten force cannot survive
the reboot someone reaches for when a machine misbehaves. Committing is the
deliberate act, for the override meant to stay - a failed sensor bypassed until
the spare arrives. Two things keep the saved ones honest: the boot CRITICAL, and
`overrides list` reporting how many are unsaved.

@note **Unsaved is the IEC 61131 behaviour, and it is the default.** Forcing
there is volatile by design: a power cycle clears every force, so power-cycling
is a reliable way back to normal operation. That holds here for any override
nobody committed. A SAVED one is the deviation, taken deliberately for the case
IEC has no answer to - a bypass that must hold across the power cut - and it is
mitigated by the boot CRITICAL naming the row and by `overrides clear all`
clearing the lot, stored copies included.

Every kind, `K_OUTPUT` included. An override means one thing whatever row it is
on - a person has taken this by hand - and carving an exception out for actuators
made it mean two, which is how an operator ends up wondering why the valve they
forced came back and the reading they forced did not.

@warning A restored output override is **driving the plant** from the moment
Persist runs. Each restore is announced as a CRITICAL naming the row; that
announcement is the safeguard. Every release path (`Persist::override_clear()`,
`override_clear_all()`, a health withdrawal) wipes the stored copy in the same
breath, so a released one cannot come back.

## Bands, and an end that is a row

A band end is a number **or another row**, so a pair with a required
relationship - a target's floor and ceiling, a ramp's start and end - cannot be
edited into an impossible one. Each value alone is legal; only the pair is
wrong, and a fixed band cannot see that.

`Band` is two ends flattened into 24 bytes: `lo`, `hi`, and a row index for
each, `VAR_NONE` where the end is fixed. The number beside a named end is the
FALLBACK, and what the generator checks the default against.

`Var::band()` is the only way to READ one: it resolves both ends against what
those rows hold NOW, and for an analog row it answers the calibration's
plausibility band instead. Reading `declared_band()` directly shows the
firmware fallback rather than the parameter an operator just edited.

A band whose two ends are equal and both fixed is NO BAND. That is what a row
with no meaningful bounds carries, and why there is no flag saying so.

`AnalogCal` is the same idea six times over: two calibration points, an
engineering pair and a plausibility band, each a number or a row. `scale` and
`offset` are never stored - they derive from the endpoints, so a recalibration
lands the moment the parameter commits.

## `Bank`: the table, and nothing durable

`Bank` owns the generated table and the RAM beside it, and is what
`SystemContext::vars` exposes. Consumers read it from the context; nothing gets
its own pointer.

| | |
|---|---|
| `init(rows, order, count, param_first, param_count)` | take the generated arrays and lay the RAM out over them |
| `var(i)` | make a handle |
| `find(name[, len])` | resolve a name, case-insensitively, through the generated order |
| `param_at(k)` / `param_count()` | the K_PARAM block, 0-based |
| `expire_stale(now_ms)` | drop `K_INPUT` rows past their `timeout_ms` to `Q_NONE` |
| `unsaved_count()` / `owed_count()` / `unsaved_override_count()` | the tallies the pages show |

It holds no store. Every verb with a storage side is a `Vars::Persist` call, and
`Persist` does its RAM half through the `Var` inside it - so a caller cannot
reach past it and get RAM-only behaviour that looks right until the next boot.

The bank does not decide control authority and does not enact it. Who may drive
the field is `Plc::Runtime::Plant::running()` - the operator's standing choice
(`sys_control`, and `programs disable` per program) against the system's hold
(`Scheduler::RunState`), stricter wins. A program that is not running drops what
it drove to its declared safe state, one row at a time, through `Var::safe()`.

The two writes are deliberately different. `param_set()` refuses anything that is
not a writable parameter with `SET_INVALID` - writing a sensor or an output would
simply be overwritten by the next scan. `override_set()` checks only the type, so
a reading can be pushed past its plausibility band to exercise the alarm watching
for it.

### How its methods are named {#dev_vars_naming}

Three shapes, and which one a new method takes is not a matter of taste.

| Shape | For | Examples |
|---|---|---|
| `<noun>_<verb>` | a FAMILY of operations on one concept, so the family sorts together and reads the way the shell says it | `override_set` / `override_clear`, matching `overrides set` / `overrides clear` |
| `set_<property>` | a single property with no family behind it. Turning these around would give `run_state_set`, which is why they are not the shape above | `set_store`, `set_run_state`, `set_quality` |
| `<thing>_count()` | a tally, always, and never `num_` or `n_` | `param_count`, `unsaved_count`, `owed_count` |

`param_set()` means EDIT A PARAMETER. Anything that writes a row by another
route says so in its name (`override_set`, `publish`, `set`), so a caller cannot
reach for one and get a different rule.

### Addressing a row

Everything but the console indexes through a **generated constant**:
`Vars::Row::cli_temp_hi`, from `generated/rows.gen.h`, which the build points at
with `BOTANIK_SITE_ROWS` exactly as `BOTANIK_SITE_CONFIG` carries the config. A
name no table declares is a compile error, not a reference that resolves to the
sink for the life of the run.

@warning A generated index is a bare `uint16_t`, and **0 is a real row**. Never
read an index back out of a `const` table it was declared in: a zero-initialised
field is row 0, not "none". `VAR_NONE` is 0xFFFF for that reason.

### The sink

An out-of-range index - including `VAR_NONE` - resolves to an inert **sink** row,
not to a clamped real row. Clamping a typo onto the last row of the table would
drive a real relay. The sink reads zeros with quality `Q_NONE`, so a law reading
one gets the declared substitute rather than a zero it cannot tell from a
reading, and it swallows writes.

### The name order

`find()` is the CONSOLE's route - an operator's typed word, a log sink naming the
parameter its floor follows - and it binary-searches a **generated** array of row
numbers in case-folded name order. Nothing is built at boot and there is nothing
to maintain: the table is `const` and never grows.

An ad-hoc table (a unit case's handful of rows) has no order beside it and
`find()` walks it instead. A site's is generated, always.

Both `find()` overloads share the one comparison. The length-bounded form (a
shell word has no NUL of its own) orders identically: with the first *len*
characters equal, a longer row name sorts after the key, because the key ends
there.

### Control authority

Two INDEPENDENT sources decide whether a law may drive the field, and the
STRICTER wins. The **operator** picks a standing choice - `sys_control` for all
of them, `programs disable <pat|all>` for one - and the **system** holds control
outside `Scheduler::RunState::RUN`, which is bring-up (`sys_startup_sec`) and
the way out (`sys_shutdown_sec`). `Plc::Runtime::Plant::running()` is where the
two meet, so a recovered firmware never talks over the operator and an
operator's choice survives a hold coming and going.

A program that is not running does not FREEZE what it drove. Its rows are WRITTEN
to their declared value - which is what `Descriptor::def` means for an output,
and what `vars info` calls "safe state, driven when nothing drives it" - and the
write marks them owed to the field, so the coil actually moves. A quality
companion in the same roster drops to `Q_NONE` with them. A coil left energised
by a law nobody is running is the thing this exists to prevent.

Which rows those are cannot be asked of the bank: an ST row resolves VAR_IN_OUT,
so no exclusive writer is recorded. The roster is generated per program from what
matiec says its body writes (`__SET_EXTERNAL`), and the same pass fails the build
on a row two programs write, or none.

Overrides are untouched by any of this. An override is the operator present at
the console, and muting one would take the plant out of their hands at exactly
the moment they reached for it; `overrides clear` is how a force comes off.

## `Persist`: the durable half

A `Component` of its own. The bank is RAM-only; every verb with a storage side is
here, and each does its RAM half through the `Var` inside it.

| | |
|---|---|
| `on_setup` | parameters, then the overrides that shadow them (with the boot CRITICAL), then the retained values |
| `on_config` | the retain cadence from `sys_retain_sec`: retained rows and owed parameters |
| `save(h)` / `save_all()` | commit what is unsaved and owed, clearing the marks |
| `save_override(h)` / `save_overrides()` | the same for a force |
| `override_clear(h)` / `override_clear_all()` | release in RAM and drop the stored copy in one call |
| `reset(h)` | factory default, and drop the key. Refused for a retained parameter |
| `revert(h)` | the stored copy back over an unsaved edit; the STORED one is left alone |
| `flush(now_ms)` | retained and owed, now - what health calls on the way down |

The store interface is three verbs over one key with a width: `load`, `save`,
`remove`. Narrow, not the 8-byte Scalar, because the width is part of the
encoding - storing eight bytes for a `u8` changes every key and `HAL::Storage`
refuses the oversized read, reverting the whole configuration to factory.

Three key namespaces, told apart by a prefix no identifier can spell: the bare
name is the setting, `ovr.` the override, `ret.` the retained copy. One row may
carry all three, and clearing one leaves the others untouched.

A refused *save* is not a rejected *edit*: the value is live, it just will not
survive a reboot, so `SET_NOT_PERSISTED` is a third answer and the row keeps its
mark - the next `save_all()` tries it again.

### A retained parameter

`F_RETAIN` on a `K_PARAM` row says the value is a RECORD of what happened -
`irr_last_trayN`, the day each tray was last watered - and `Persist::reset()`
refuses it. The factory default of such a row is a lie about the plant: zero
reads as "never watered", so restoring it makes every tray due at once.

It reuses the flag instead of adding one because the question is the same either
way, "is this value history?"; only the mechanism differs by kind, and
`Persist::retainable()` / `Persist::param_retained()` are the two halves. A
retained parameter never enters the retained store - it already has a key of its
own, and two keys for one row could disagree.

On the row it is a trailing `retained` flag rather than a factory of its own:
retention is orthogonal to the P / PE / PS split, which is about the shape of the
value, so a factory per combination would multiply out.

`Persist::revert()` still moves a retained row, deliberately. It restores what a
reboot would give it - the saved copy, or the factory default when nothing was
ever saved - so it can undo only an edit that is not durable yet, and takes away
nothing a power cut would not.

### The two marks

`S_PARAM_UNSAVED` is an OPERATOR's edit and `S_PARAM_OWED` is a PROGRAM's. They
survive each other on purpose: a law stamping a watering date must not read as an
unsaved SETTING on the params page, and the retain cadence must not commit a
setting the operator has not confirmed. `save_all()` writes both; `save_owed()`
writes only the second, and that is what the cadence calls.

Config-driven components take their settings through `ConfigBinder`, which reads
the rows by generated index and re-applies a component's whole group when that
group's commit serial changes.

@note Nothing bumps that serial automatically. `param_set()` applies the value and
stops there; the operator commits the group by writing its serial. That is
deliberate - a group like the static IP block is incoherent until every field is
in, so applying per edit would restart the interface through states that were
never meant to exist. See @ref user_variables.

## Scalar, and the declaration factories

### One tag, four uses {#scalar_one_tag}

A variable carries a single VarType. That tag governs its live value AND its
declared band and default: the same union, read the same way, so they cannot
disagree about what the variable is (a real-valued parameter's band of 0.5..2.5
stays fractional).

### Why constexpr constructors {#scalar_flash}

A site's variable table is `const` and must be constant-initialised straight
into flash: no `.data` copy, no startup initialiser. A union with constexpr
constructors keeps that property (a *member* having constructors does not
stop the enclosing descriptor being an aggregate), while brace-initialisation
could only ever reach the union's FIRST member, C++11 having no designated
initialisers. Two load-bearing constraints fall out:

 - a C++11 constexpr function body must be a SINGLE return statement, so the
   factories below construct rather than assign
 - a bare integer literal converts equally well to uint64_t, int64_t and
   float, so Scalar(int) is deleted and the named factories are the way in

### Persisted width {#scalar_width}

@c type_width() is the NARROW width (1/2/4/8), not sizeof(Scalar): what a
value occupies in storage. It must stay narrow, or already-stored records
stop being readable.

### constexpr and the single return {#declare_flash}

The factories build the site's `const Descriptor rows[]`, which must be
constant-initialised straight into flash: a runtime call in that initialiser
would drag ~46 KB of table into `.data` behind a startup loop. A C++11 constexpr
body must be a SINGLE return statement, so each one is exactly that - an
aggregate initialiser, no locals and no branches.

Sixteen factories over six Kinds. Each takes ONLY the arguments its spelling can
use, so a meaningless row is unspellable: a relay with a plausibility band, an
alarm with a timeout, a parameter with a device address.

### Local I/O {#declare_local}

There is no separate kind for a local channel. A local row is an INPUT or OUTPUT
whose Modbus device address is 0, so the two can never disagree. `AI`, `DI`,
`AO`, `DO` and `RO` all take a device: 0 is local, anything else is a unit on the
bus. `PI` is local by construction - a pulse counter is an interrupt on one of
the controller's own terminals - and carries its channel in `reg` and its edge in
`fc`, the two fields a local row has no other use for.

### Order {#lib_order}

The `K_PARAM` rows are **one contiguous run** - that is what lets the PARAMS page
and the shell address them by a dense 0-based index - and the bounds are
generated constants beside the table. The generator emits the library's
parameters first, then the site's, so the run is contiguous by construction; the
firmware is handed `param_first` and `param_count` and scans for nothing.

### Register space {#lib_regs}

A parameter, status value or alarm carries a hand-assigned Modbus TCP slave
register (`Binding::reg`), and an external master's map is a contract. The
library reserves **900-999**: parameters and status values grow upward from 900,
its alarms take 990-999, and a site's own alarms live in 800-899. The GENERATED
rows have blocks of their own above all of it - qualities from `quality_base`,
the parking pairs from `park_base`, the per-program health rows from `prog_base` -
so adding a reading or a program cannot renumber a hand-assigned register.

The generator refuses a duplicate, a mapped kind with no register, and a
hand-assigned one that lands inside a generated block.

### A site OVERRIDES a library row

By redeclaring the name. The generator replaces the library's row IN PLACE and
keeps the library's register, so the whole row becomes the site's - the band and
the unit included, not just the value - and the map does not move. `geo_lat_deg`
is the worked example.

## Access tables

`vars/access.h` holds the display-side rules. There used to be a `VarGroup[]`
table per site listing which variables belonged to which sub-topic; two
properties of the row replaced it:

| | |
|---|---|
| `var_group(d)` | the group: the variable's name up to the first `_`, interned |
| `published(d)` | on the MONITOR page and in telemetry unless it is a parameter, an alarm or a quality |

Naming a variable is what files it, so there is no table to add it to and none to
forget. What is left declarative is `HomeVar` (a home-screen label and the row it
shows - no rule derives a caption), and `var_format()` renders any row's value.
