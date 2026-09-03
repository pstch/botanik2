# The variable model {#dev_vars}

`vars/` is the one model every value the firmware exposes goes through - a meter
reading, a relay coil, a computed global, a setting. The *shape* of that model (the
descriptor fields, the enums, what a site declares) is in @ref int_variables; this
page is how it works inside.

## Split: const descriptor, RAM slot

Two parallel arrays, indexed together.

**`Variable`** is the descriptor: name, unit, description, bounds, type, role,
binding. It is `const` and `constexpr`-constructed so the whole table
constant-initialises into **flash**, not `.data`. That is why `Scalar` has
`constexpr` factories and why `SiteDescriptor` has no NSDMIs - one function call in
those initialisers would drag several kilobytes into RAM.

**`Slot`** is the RAM slot, pinned at 32 bytes:

```cpp
struct Slot {
    Scalar   v;
    Scalar   ovr;        // forced value, while ovr_on
    uint64_t stamp_ms;   // uptime ms of the last good update
    uint16_t str_off;    // T_STR only: this row's pool slot
    uint16_t qlt_row;    // this row's `_qlt` companion, or VAR_NONE
    bool     ovr_on;
    uint8_t  vflags;     // ValueFlags
    uint8_t  wire_off;   // AI/AO only: index into the wire array
};
static_assert(sizeof(Slot) == 32, "Slot must stay 32 bytes (RAM per variable)");
```

There is no `def` in the slot: the default is a typed literal already in flash, so
an invalid read falls back to it without a per-slot copy. There is no QUALITY
either - that is a row of its own, and `qlt_row` is how this one finds it.

@warning The size assert is not decoration. Every field here costs its width **per
variable, on every site, whether it is used or not** - the override alone is 8 bytes
across the whole table. Adding a field means spending that everywhere; say so out
loud when you do.

## `Var`: the one handle

`Var` is a non-owning `(descriptor, slot)` pair, made on the fly and passed by
value. It costs nothing to construct and gives control logic type safety with no
offset arithmetic:

```cpp
float t = io.var(V_EXT_TEMP).value();
io.var(V_DRAIN_VALVE).set(true);
```

Reads funnel through one accessor, and the value in force is separate from the
belief in it:

```cpp
Value value() const { return _v->ovr_on ? override() : live(); }
bool  valid() const { return quality() == Q_VALID; }
```

**A quality is the VALUE of a `_qlt` row.** `x_qlt` is a real row - descriptor,
register, name index - whose value is a `Vars::Quality`: `Q_NO_DATA`, `Q_VALID`,
`Q_STALE`, `Q_IMPLAUSIBLE`, `Q_REFUSED`. `Bank::freeze()` pairs it with `x` by
name into `Slot::qlt_row`, so the suffix is the whole link and there is no table
to add a companion to. `quality()` is one indirection; a row with no companion is
valid, always, and so is every companion, which is what stops the regress.

There is no second store. A driver's `publish()` writes that row, `expire_stale()`
writes it, an ST law assigns it, an operator forces it - all the same byte, so
nothing can lag or disagree. `Q_NO_DATA` is 0 on purpose: it is a companion's
declared default, so a row nothing has spoken for reads as "nothing has written
this" and never as good data.

**An unbelievable row reads its declared default TO A LAW.** `Ref::get()` is where
the substitution happens, once. `Var::value()` does not substitute: the monitor
page and telemetry show the number that was implausible, with the quality beside
it, which is what an operator chasing a dead probe needs. `Ref::held()` is the
same read for a law that must have the raw one - a totaliser difference.

Which rows get a companion is generated, not declared: one per INPUT, because a
reading can always stop being one, plus one per row a program that opted in
(`quality` in `tables/programs.csv`) writes. Opting in obliges the law to write
every one of them, which `tables.py plant` checks off matiec's own write list.

**The override is applied here, and only here.** Every reader in the firmware goes
through `raw()`/`valid()`, so a forced value reaches control logic, the console,
telemetry and the alarm model alike. There is no path that sees around it - which is
exactly what makes an override useful for testing, and exactly why it must be
impossible to forget one is set.

Writes keep landing in `v` underneath while an override is in force, so the override
*shadows* rather than replaces: clearing it resumes the live value with no gap and
nothing to restore. The override carries no validity of its own: "make this read as
a dead sensor" is a force on the row's `_qlt` companion, which is an ordinary force
of an ordinary row - saved, released and listed the same way. Both spellings are
`overrides set <name> [<value>] [valid|invalid]` from the console; the second word
lands on the companion. A row with no companion has no quality to force, and says
so.

**A forced VALUE also forces the belief.** Forcing a dead probe's reading so the
plant runs on it is what an override is for, and inheriting the doubt underneath
would make that impossible - so `override_set()` writes `Q_VALID` into the
companion, and releasing the value releases that too. Written into the store
rather than applied at read time on purpose: ST reads the companion ROW, so a
rule living in `Var` would be an answer only C++ could see. `... <value> invalid`
forces the companion afterwards and so wins.

Overrides **can be made durable, and are not by default**. `override_set()` applies
the force and marks the slot `VF_OVR_UNSAVED`; `save_override()` writes it under an
`ovr.<name>` key of its own, so the override and the parameter setting beneath it stay
separate facts - clearing one leaves the other untouched. `Bank::load_overrides()`
restores what was saved, after `load_params()`, and a restored override is announced
as a CRITICAL at boot.

The default is the safe half of the trade: forcing a signal to chase a fault is the
common case and dies with the session, so a forgotten force cannot survive the reboot
someone reaches for when a machine misbehaves. Committing is the deliberate act, for
the override meant to stay - a failed sensor bypassed until the spare arrives. Two
things keep the saved ones honest: the boot CRITICAL, and `overrides list` reporting
how many are unsaved.

@note The three mutators live on `Bank`, not on the `Var` handle - the handle's
are private to it. Each has a storage side, so a caller that reached past the bank
got RAM-only behaviour that looked right until the next boot.

@note **Unsaved is the IEC 61131 behaviour, and it is the default.** Forcing there is
volatile by design: a power cycle clears every force, so power-cycling is a reliable
way back to normal operation. That holds here for any override nobody committed. A
SAVED one is the deviation, taken deliberately for the case IEC has no answer to - a
bypass that must hold across the power cut - and it is mitigated by the boot CRITICAL
naming the row and by `overrides clear all` clearing the lot, stored copies included.
Anyone arriving with PLC habits should be told which of the two they are looking at,
which is what the `(unsaved)` marker is for.

Every role, `ROLE_OUTPUT` included. An override means one thing whatever row it is on
- a person has taken this by hand - and carving an exception out for actuators made it
mean two, which is how an operator ends up wondering why the valve they forced came
back and the reading they forced did not.

@warning A restored output override is **driving the plant** from the moment
`load_overrides()` runs. Each restore is announced as a CRITICAL naming the row, and
an output's says so explicitly; that announcement is the safeguard. Every release path
(`override_clear()`, `override_clear_all()`, a health withdrawal) wipes the stored copy in
the same breath, so a released one cannot come back.

## `Bank`: the one writer {#dev_vars_writers}

`Bank` owns the tables and is what `SystemContext::vars` exposes. Consumers read it
from the context; nothing gets its own pointer.

| | |
|---|---|
| `init()` | seed every variable with its declared default; clear overrides in RAM (the stored ones survive, for `load_overrides()`) |
| `load_overrides()` | restore the overrides in force at the last shutdown; skips `ROLE_OUTPUT` |
| `expire_stale(now_ms)` | invalidate `ROLE_INPUT` rows past their `timeout_ms` |
| `var(ref)` | make a handle |
| `find(name[, len])` | resolve a name to a `VarRef`, through the name index |
| `set(...)` | edit a **parameter**: validate against its band, apply, mark unsaved |
| `override_set(...)` | force **any** variable: range-checked against the type only, marked unsaved |
| `save(...)` / `save_override(...)` | write one row's value / force to the store, clearing its mark |
| `save_params()` / `save_overrides()` | the same for every row still marked |
| `safe(ref)` | drop one row to its declared safe state (on `Var`), which is how a program leaving the scan lets go of what it drove |
| `override_clear_all()` | release the lot |

The bank does not decide control authority and no longer enacts it either.
Who may drive the field is `Plc::Runtime::Plant::running()` - the operator's
standing choice (`sys_control`, and `programs disable` per program) against
the system's hold (`Scheduler::RunState`), stricter wins. A program that is
not running drops what it drove to its declared safe state, one row at a
time, through `Var::safe()`.

The two writes are deliberately different. `set()` refuses anything that is not a
parameter with `SET_INVALID` - writing a sensor or an output would simply be
overwritten by the next scan. `override_set()` checks only the type, so a reading can
be pushed past its plausibility band to exercise the alarm watching for it.

### How its methods are named {#dev_vars_naming}

Three shapes, and which one a new method takes is not a matter of taste.

| Shape | For | Examples |
|---|---|---|
| `<noun>_<verb>` | a FAMILY of operations on one concept, so the family sorts together and reads the way the shell says it | `override_set` / `override_clear` / `override_clear_all`, matching `overrides set` / `overrides clear` |
| `set_<property>` | a single property with no family behind it. Turning these around would give `run_state_set`, which is why they are not the shape above | `set_store`, `set_run_state`, `set_variables` |
| `<thing>_count()` | a tally, always, and never `num_` or `n_` | `param_count`, `unsaved_count`, `refused_count`, `unresolved_count`, `param_stray_count` |

`set()` alone means EDIT A PARAMETER. Anything that writes a row by another
route says so in its name (`override_set`, `publish` on `Var`), so a caller
cannot reach for `set` and get a different rule.

### The sink

An out-of-range index - including `VAR_NONE` - resolves to an inert **sink** row, not
to a clamped real row. Clamping a typo onto the last row of the table would drive a
real relay. The sink reads as its own zeroes and swallows writes.

### The name index

`find()` is the one way anything addresses a row, and it was a `strcmp` down the whole
table: 259 of them per miss at the reference site, several times a beat once the
simulated field and the config readers are counted.

It is now a **boot-time mapping**: `VarTables::index` is `count` row numbers, which the
`Bank(VarTables)` constructor sorts by name, and `find()` binary-searches. ~8
comparisons instead of 259, and nothing to maintain afterwards - the descriptor table
is `const` and never grows, so the mapping built at construction is the mapping for the
life of the bank.

The array is **borrowed and exactly sized**, like the value store and the string pool:
a fixed array inside `Bank` would need a maximum row count declared somewhere, and that
number is already written down as `VarTables::count`. A site's is
`SiteStore::name_index[VAR_TOTAL]`.

A null one leaves `find()` walking the table - which is what a two-row test fixture
wants and what a real site must not ship, so `name_index_built()` reports it and
`tests/test_site.cpp` fails on it.

Both `find()` overloads share the one mapping. The length-bounded form (a shell word
has no NUL of its own) orders identically: with the first *len* characters equal, a
longer row name sorts after the key, because the key ends there.

### Control authority

Two INDEPENDENT sources decide whether a law may drive the field, and the
STRICTER wins. The **operator** picks a standing choice - `sys_control` for all
of them, `programs disable <pat|all>` for one - and the **system** holds control
outside `Scheduler::RunState::RUN`, which is bring-up (`sys_startup_sec`) and
the way out (`sys_shutdown_sec`). `Plc::Runtime::Plant::running()` is where the
two meet, so a recovered firmware never talks over the operator and an
operator's choice survives a hold coming and going.

A program that is not running does not FREEZE what it drove. Its rows are WRITTEN
to their declared value - which is what `Descriptor::def` means for an output, and
what `vars info` calls "safe state, driven when control is withdrawn" - and the
write marks them owed to the field, so the coil actually moves. A quality companion
in the same roster drops to `Q_NO_DATA` with them. A coil left energised by a law
nobody is running is the thing this exists to prevent.

Which rows those are cannot be asked of the bank: an ST row resolves
VAR_IN_OUT, so no exclusive writer is recorded. The roster is generated per
program from what matiec says its body writes (`__SET_EXTERNAL`), and the same
pass fails the build on a row two programs write, or none.

Overrides are untouched by any of this. An override is the operator present at
the console, and muting one would take the plant out of their hands at exactly
the moment they reached for it; `override clear` is how a force comes off.
`override_set()` is live immediately rather than staged, and unless it is saved
there is no session it can outlive.

## Parameters

A parameter is just a `ROLE_PARAM` variable. `Params` is the editable view over that
contiguous block: dense 0-based indexing for the PARAMS page and the shell, the boot
load, and the two-step edit: `set()` returning `SET_OK` / `SET_INVALID`, and `save()`
returning `SET_OK` / `SET_NOT_PERSISTED`.

Splitting them is what makes that third result sayable at all. A refused *save* is not
a rejected *edit*: the value is live, it just will not survive a reboot, and the caller
must say so rather than pretend either success or failure. The row keeps its mark, so
the next `save_params()` tries it again rather than leaving the failure to be
remembered.

`KvParamStore` is one `HAL::Storage` key per parameter, named after the parameter, plus
one `ovr.<name>` key per saved override. There is no layout to version, so adding,
removing or reordering parameters cannot invalidate the others. The `ovr.` prefix cannot
collide with a parameter name, because no identifier carries a dot.

### A retained parameter

`F_RETAIN` on a `ROLE_PARAM` row says the value is a RECORD of what happened -
`irr_last_trayN`, the day each tray was last watered - and `param_reset()` refuses it.
The factory default of such a row is a lie about the plant: zero reads as "never
watered", so restoring it makes every tray due at once.

It reuses the flag instead of adding one because the question is the same either way,
"is this value history?"; only the mechanism differs by role, and `Bank::retainable()`
/ `Bank::param_retained()` are the two halves. A retained parameter never enters the
retained store - it already has a key of its own, and two keys for one row could
disagree.

On the row it is a trailing `retain` flag rather than a `PR` factory beside `P`:
retention is orthogonal to the P / PE / PS split, which is about the shape of
the value, so a factory per combination would multiply out.

`param_clear()` still moves a retained row, deliberately. It restores what a reboot
would give it - the saved copy, or the factory default when nothing was ever saved -
so it can undo only an edit that is not durable yet, and takes away nothing a power
cut would not.

Config-driven components read by name (`Vars::get_int` / `get_f32` / `get_str`) and
re-apply their whole group when its commit serial changes - `group_serial(io, "net")`
is just `get_int(io, "net")`, the group's own same-named parameter.

@note Nothing bumps that serial automatically. `set()` applies the value and stops
there; the operator commits the group by writing its serial. That is deliberate - a
group like the static IP block is incoherent until every field is in, so applying per
edit would restart the interface through states that were never meant to exist. See
@ref user_variables.


## Scalar, and the declaration factories

### One tag, four uses {#scalar_one_tag}

A variable carries a single VarType. That tag governs its live value AND its
declared @c min, @c max and @c def: the same union, read the same way, so
they cannot disagree about what the variable is (a real-valued parameter's
band of 0.5..2.5 stays fractional).

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


### Order {#lib_order}

`Bank` requires the `ROLE_PARAM` rows to be **one contiguous run** - that is
what lets the PARAMS page and the shell address them by a dense 0-based index.
The library registers first and its parameters come last in its array, so a
site's parameter segment abuts them. `Bank::freeze()` checks it and names the
row that broke it.

### Register space {#lib_regs}

A parameter, status value or alarm carries a hand-assigned Modbus TCP slave
register (`Binding::reg`), and an external master's map is a contract. The
library reserves **900-999**: parameters and status values grow upward from
900, its alarms take 990-999, and a site's own alarms live in 800-899.
`tests/test_site_names.cpp` fails on a collision and on a row outside the
block its scope belongs to.


### constexpr and the single return {#declare_flash}

These build the site's `const Descriptor g_vars[]`, which must be
constant-initialised straight into flash: a runtime call in that initialiser
would drag ~20 KB of table into `.data` behind a startup loop. A C++11
constexpr body must be a SINGLE return statement, so each one below is
exactly that - an aggregate initialiser, no locals and no branches.

### Local I/O {#declare_local}

There is no separate scope for a local channel. A local row is one whose
Modbus device address is 0, and the factories derive `transport` from that,
so the two can never disagree. `AI`, `DI`, `AO`, `DO` and `RO` all take a
device: 0 is local, anything else is a unit on the bus.

The names are the scope enum's own abbreviations, so a row and the value a
table filters on are spelled the same way, and rows stay one line wide.

## Access tables

`vars/access.h` holds the display-side rules. There used to be a `VarGroup[]` table
per site listing which variables belonged to which sub-topic; two properties of the
row replaced it:

| | |
|---|---|
| `var_group(d)` | the group: the variable's name up to the first `_`, interned |
| `var_published(d)` | on the MONITOR page and in telemetry unless its scope is `P` or `A` |

Naming a variable is what files it, so there is no table to add it to and none to
forget. What is left declarative is `HomeVar` (a home-screen label and a name - no
rule derives a caption), and `var_format()` renders any row's value.
