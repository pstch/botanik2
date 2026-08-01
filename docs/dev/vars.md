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

Overrides are RAM-only and never persisted, deliberately: one left on across a reboot
is a fault waiting to happen, and a reboot is exactly what an operator reaches for
when a machine misbehaves.

## `Bank`: the one writer

`Bank` owns the tables and is what `SystemContext::vars` exposes. Consumers read it
from the context; nothing gets its own pointer.

| | |
|---|---|
| `init()` | seed every variable with its declared default; clear overrides |
| `expire_stale(now_ms)` | invalidate `ROLE_INPUT` rows past their `timeout_ms` |
| `var(ref)` | make a handle |
| `find(name, len)` | resolve a name to a `VarRef` |
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

### Control authority

One latch. `disable_control()` drops every `ROLE_OUTPUT` to its declared `def`,
**releases** any override holding an output, and (through the loop) inhibits the
scan. Releasing rather than ignoring is the important part: a staged override would
fire the moment control came back. While control is off, `override_set()` refuses an
output outright.

Control authority outranks an override, never the other way round.

## Parameters

A parameter is just a `ROLE_PARAM` variable. `Params` is the editable view over that
contiguous block: dense 0-based indexing for the PARAMS page and the shell, the boot
load, and a `set()` returning `SET_OK` / `SET_INVALID` / `SET_NOT_PERSISTED`.

That third result matters: a refused *persist* is not a rejected *edit*. The value is
live; it just will not survive a reboot, and the caller must say so rather than
pretend either success or failure.

`KvParamStore` is one `HAL::Storage` key per parameter, named after the parameter.
There is no layout to version, so adding, removing or reordering settings cannot
invalidate the others.

Config-driven components read by name (`Vars::get_int` / `get_f32` / `get_str`) and
re-apply their whole group when its commit serial changes - `group_serial(io, "net")`
is just `get_int(io, "net")`, the group's own same-named parameter.

@note Nothing bumps that serial automatically. `set()` persists the value and stops
there; the operator commits the group by writing its serial. That is deliberate - a
group like the static IP block is incoherent until every field is in, so applying per
edit would restart the interface through states that were never meant to exist. See
@ref user_settings.

## Access tables

`vars/access.h` holds the display-side types: `VarGroup` (a sub-topic plus the
`VarRef`s under it - the unit shared by the MONITOR page, the `get` command and MQTT
telemetry), `HomeVar` (a home-screen label and ref), and `var_format()`.
