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
Set it from the console with `override <name> [<value>] [valid|invalid]`. Note what it
does not do: the forced value is still what `raw()` returns, so a consumer gating on
`valid()` sees bad data while one that does not still reads the number. Forcing the
quality is not the same as simulating the timeout fallback, which substitutes `def`.

Overrides **are persisted**, under an `ovr.<name>` key of their own so the override and
the parameter setting beneath it stay separate facts - clearing one leaves the other
untouched. `Bank::load_overrides()` restores them after `load_params()`, and a restored
override is announced as a CRITICAL at boot.

That is a deliberate trade, and it cuts the other way too: a maintainer who forced a
signal to work on the plant should not lose it to a power cut, but a forgotten one now
survives the reboot someone reaches for when a machine misbehaves. Three things keep
that honest - the boot CRITICAL, `override list` saying plainly that they survive, and
`override clear` clearing the lot in one command.

@note The three mutators live on `Bank`, not on the `Var` handle - the handle's
are private to it. Each has a storage side, so a caller that reached past the bank
got RAM-only behaviour that looked right until the next boot.

@note **This is a deliberate deviation from IEC 61131.** Forcing there is volatile by
design: a power cycle clears every force, so power-cycling is a reliable way back to
normal operation. Here it is not. The trade was made knowingly - a maintainer who forced
a signal to work on the plant should not lose it to a power cut - and it is mitigated by
the boot CRITICAL, by `override list` saying so in as many words, and by
`override clear` clearing the lot in one command. Anyone arriving with PLC habits
should be told.

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
| `set(...)` | edit a **parameter**: validate against its band, then persist |
| `override_set(...)` | force **any** variable: range-checked against the type only |
| `enable_control()` / `disable_control()` / `control_enabled()` | control authority |
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

One latch, two independent inhibits. `disable_control()` drops every `ROLE_OUTPUT` to
its declared `def` and (through the loop) inhibits the scan, so nothing re-drives them
on the next beat.

What happens to an **override** depends on which inhibit:

| Source | Overrides on outputs |
|---|---|
| `INHIBIT_OPERATOR` (`control disable`) | left alone |
| `INHIBIT_HEALTH` (fault, bring-up, pending reset) | released, stored copy included |

That asymmetry is the whole design. `control disable` is how an operator takes the
plant by hand: the programs stop deciding, and an override is then the only thing that
*can* move an actuator - wiping them would leave nothing able to. The health inhibit
means something else entirely - the firmware can no longer reason about what it is
driving, or is about to stop running, and nobody is necessarily watching - so a
hand-held output is exactly what the safe state exists to release.

`override_set()` therefore never refuses an output on control grounds. It is live
immediately rather than staged, and it is never persisted, so there is no session it
can outlive.

## Parameters

A parameter is just a `ROLE_PARAM` variable. `Params` is the editable view over that
contiguous block: dense 0-based indexing for the PARAMS page and the shell, the boot
load, and a `set()` returning `SET_OK` / `SET_INVALID` / `SET_NOT_PERSISTED`.

That third result matters: a refused *persist* is not a rejected *edit*. The value is
live; it just will not survive a reboot, and the caller must say so rather than
pretend either success or failure.

`KvParamStore` is one `HAL::Storage` key per parameter, named after the parameter, plus
one `ovr.<name>` key per persisted override. There is no layout to version, so adding,
removing or reordering parameters cannot invalidate the others. The `ovr.` prefix cannot
collide with a parameter name, because no identifier carries a dot.

Config-driven components read by name (`Vars::get_int` / `get_f32` / `get_str`) and
re-apply their whole group when its commit serial changes - `group_serial(io, "net")`
is just `get_int(io, "net")`, the group's own same-named parameter.

@note Nothing bumps that serial automatically. `set()` persists the value and stops
there; the operator commits the group by writing its serial. That is deliberate - a
group like the static IP block is incoherent until every field is in, so applying per
edit would restart the interface through states that were never meant to exist. See
@ref user_params.

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
