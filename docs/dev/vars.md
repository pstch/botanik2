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

**`Value`** is the RAM slot, pinned at 32 bytes:

```cpp
struct Value {
    Scalar   v;
    Scalar   ovr;        // forced value, while ovr_on
    uint64_t stamp_ms;   // uptime ms of the last good update
    bool     valid;
    bool     ovr_on;
    bool     ovr_valid;  // what valid() reports while overridden
    uint8_t  _rsv[5];
};
static_assert(sizeof(Value) == 32, "Value must stay 32 bytes (RAM per variable)");
```

There is no `def` in the slot: the default is a typed literal already in flash, so
an invalid read falls back to it without a per-slot copy.

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

Reads funnel through one accessor:

```cpp
Scalar raw() const { return _v->ovr_on ? _v->ovr
                                       : (_v->valid ? _v->v : _d->def); }
bool valid() const { return _v->ovr_on ? _v->ovr_valid : _v->valid; }
```

Two decisions live in those two lines.

**An invalid variable reads as its declared default.** Control logic that does not
care about quality still gets a sane number; logic that does care asks `valid()`.

The slot records **why**, not just whether: `Value::qual` is a `Vars::Quality` -
`Q_GOOD`, `Q_NO_DATA`, `Q_STALE`, `Q_IMPLAUSIBLE`, `Q_SAFE` - written by whichever
path invalidated the row, so it is a diagnosis rather than something reconstructed
at display time. `valid()` is `qual == Q_GOOD`; there is no second bool to drift
from it, and it costs the byte the old bool did. `Q_NO_DATA` is 0 on purpose: a
zero-initialised `Value`, which is how every site's store starts, must read as
"nothing has written this" and never as good data.
Because a site picks defaults that are outside the plausibility band, a dead sensor
usually reads as a fault without any code testing for it.

**The override is applied here, and only here.** Every reader in the firmware goes
through `raw()`/`valid()`, so a forced value reaches control logic, the console,
telemetry and the alarm model alike. There is no path that sees around it - which is
exactly what makes an override useful for testing, and exactly why it must be
impossible to forget one is set.

Writes keep landing in `v` underneath while an override is in force, so the override
*shadows* rather than replaces: clearing it resumes the live value with no gap and
nothing to restore. `ovr_valid` is separate because "force this to 20 C" and "make
this read as a dead sensor" are both needed, and quality is what distinguishes them.
Set it from the console with `overrides set <name> [<value>] [valid|invalid]`. Note what it
does not do: the forced value is still what `raw()` returns, so a consumer gating on
`valid()` sees bad data while one that does not still reads the number. Forcing the
quality is not the same as simulating the timeout fallback, which substitutes `def`.

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
naming the row and by `overrides clear` clearing the lot, stored copies included.
Anyone arriving with PLC habits should be told which of the two they are looking at,
which is what the `(unsaved)` marker is for.

Every role, `ROLE_OUTPUT` included. An override means one thing whatever row it is on
- a person has taken this by hand - and carving an exception out for actuators made it
mean two, which is how an operator ends up wondering why the valve they forced came
back and the reading they forced did not.

@warning A restored output override is **driving the plant** from the moment
`load_overrides()` runs. Each restore is announced as a CRITICAL naming the row, and
an output's says so explicitly; that announcement is the safeguard. Every release path
(`override_clear()`, `clear_overrides()`, a health withdrawal) wipes the stored copy in
the same breath, so a released one cannot come back.

## `Bank`: the one writer

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
| `set_control_mode()` / `set_health_hold()` / `control_mode()` | control authority |
| `clear_overrides()` | release the lot |

The two writes are deliberately different. `set()` refuses anything that is not a
parameter with `SET_INVALID` - writing a sensor or an output would simply be
overwritten by the next scan. `override_set()` checks only the type, so a reading can
be pushed past its plausibility band to exercise the alarm watching for it.

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
`tests/test_site_names.cpp` fails on it.

Both `find()` overloads share the one mapping. The length-bounded form (a shell word
has no NUL of its own) orders identically: with the first *len* characters equal, a
longer row name sorts after the key, because the key ends there.

### Control authority

Three modes, and two INDEPENDENT sources that decide which is in force. The
**operator** picks a standing mode at the console; the **system** raises a hold
for bring-up, a health fault or a pending reset. `control_mode()` is the
STRICTER of the two, so a recovered firmware never talks over the operator and
an operator's choice survives a fault coming and going.

| Mode | Outputs | An output's override |
|---|---|---|
| `CONTROL_ENABLED` | driven by the programs | drives |
| `CONTROL_INHIBITED` | dropped to `def`, scan stopped | still drives |
| `CONTROL_DISABLED` | dropped to `def`, scan stopped | muted |

The middle mode is how an operator takes the plant by hand: the programs stop
deciding, and an override is then the only thing that *can* move an actuator.
The strictest stops even that.

**Muted is not erased.** `CONTROL_DISABLED` sets `VF_OVR_MUTED` on the row, which
`Var::raw()` and `valid()` read, so the field sees the safe state while the
forced value waits; leaving the mode restores it with nothing to re-enter.
Crossing that edge in either direction marks the row `VF_FIELD_DIRTY`, because
both directions change what the field is owed.

The one destructive path is the SYSTEM's hold (`set_health_hold(true)`), which
**erases** every output override, stored copy included. The two differ because
the operator is present and a fault is not: an actuator held energised by hand
through a fault, or resurrected by the next boot, is what the safe state exists
to prevent.

Outputs only, in every mode. Muting a forced sensor would erase the diagnosis it
was forced to get, and sweeping a parameter would revert the network settings on
a scan overrun.

`override_set()` therefore never refuses an output on control grounds. It is live
immediately rather than staged, and unless it is saved there is no session it can
outlive.

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

Config-driven components read by name (`Vars::get_int` / `get_f32` / `get_str`) and
re-apply their whole group when its commit serial changes - `group_serial(io, "net")`
is just `get_int(io, "net")`, the group's own same-named parameter.

@note Nothing bumps that serial automatically. `set()` applies the value and stops
there; the operator commits the group by writing its serial. That is deliberate - a
group like the static IP block is incoherent until every field is in, so applying per
edit would restart the interface through states that were never meant to exist. See
@ref user_variables.

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
