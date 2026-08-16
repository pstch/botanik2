# Reworking the variable accessors {#dev_vars_accessors}

A staged plan. The end state is one vocabulary for reading a variable - four
layers, one return type - and a `Bank` whose method names say which concept they
belong to. @ref dev_vars is how the model works today.

## Why

Reading a variable currently takes four names that do not describe a common
axis, and two of them say the wrong thing:

| Today | Means | Problem |
|---|---|---|
| `raw()` | the value with the override APPLIED | "raw" is the opposite of what it returns |
| `value()` | the same, coerced to `float` | a type coercion wearing the name of the concept |
| `valid()` | the resolved validity | does not say which layer, while `live_valid()` does |
| `live()` / `live_valid()` / `live_quality()` | under the override | three names for one layer |

There is no name at all for the value before analog scaling, and nothing
expresses "this layer does not exist" - so `override()` cannot be asked for
without first asking `overridden()`.

## The end state

Four layers. Three are sources, one is the resolution:

| Accessor | Layer |
|---|---|
| `wire()` | the count on the wire, before analog scaling |
| `live()` | the engineering value a driver or program produced |
| `override()` | the forced value |
| `value()` | the resolution: `override()` if forced, else `live()` |

All four return the same type, and value and validity resolve **together** so the
two cannot drift:

```cpp
/** @brief A value from one layer, with what it is and whether it is any good.
 *  Q_NONE means the layer does not exist - the override of an unforced row, the
 *  wire of a row with no bus binding. */
struct Value {
    Scalar            v;
    const Variable*   d;   ///< the type, the enum converter, digits, unit
    Quality           q;   ///< Q_NONE: no such layer
};
```

`Q_NONE` rather than a `present` flag, because a caller must check quality before
trusting any reading anyway: absence rides a check that is already mandatory,
instead of adding a second one that can be skipped.

The descriptor pointer rather than a bare `VarType`, because `T_ENUM`'s label
converter lives on the descriptor - carrying only the type would send every enum
read back for a second call. It costs 8 bytes on a type that is returned in
registers and never stored.

### Conversions

`as_bool`, `as_u8/u16/u32/u64`, `as_i8/i16/i32/i64`, `as_f32`. All **saturate**;
`as_u8()` of 300 is 255, not 44, which is what `Map::wire_of` already does. A
wrapped value is indistinguishable from a real reading, a pinned one is obviously
at the rail.

Cross-type conversion is intended, not a bug to catch: a digital reads 0/1 so a
display or a register never branches on the type. The only forbidden conversion
is **`T_STR`**, whose live text is in the bank's pool and not in the `Scalar` at
all. No scalar `Value` is built for a text row - `Scalar::s` there holds the
*declared default*, so one would carry a plausible `const char*` that is not the
live value.

| Type | Read as a number | Read as text |
|---|---|---|
| numeric | `as_*()` | - |
| `T_ENUM` | `as_u32()` - the code | `label()`, null if the code has none |
| `T_STR` | refused | `Bank::text()` |

Setting by name already works and needs nothing: `parse_scalar()` tries the enum
label before the number, so `params set sys_log_level warning` and `... 3` are
both accepted, and a code the converter does not recognise is refused.

## Bank's accessors

The rule is already in `Bank`'s class doc: `<noun>_<verb>` for a family,
`set_<property>` for a lone property, `<thing>_count()` for a tally. Applying it
to the two families that predate it:

| Today | Becomes | Sites | Why |
|---|---|---|---|
| `set(Var, ...)` | `param_set` | ~45 | `set` means four things today; only this one edits a parameter |
| `check(Var, ...)` | `param_check` | 5 | same family |
| `save(Var)` | `param_save` | 9 | same family; `save_params` already exists beside it |
| `text_at(Var, cap*)` | `text(Var)` | 18 | it is not an "at", and it is the only route to a row's text |

Not renamed: `var()` (346 sites), `find()` (76), `desc()` (11), `param()` (9).
Nobody is confused by these, and churn on `var()` alone would swamp every stage
below.

## Stages

Each stage builds and passes the suite on its own. The ordering is not
cosmetic - it is chosen so that **nothing changes meaning silently**.

### 0. Independent fixes

`MqttSender` becomes a config-aware `Component` reading the `mqtt_` commit
serial, so `mqtt_tlm_sec` and `mqtt_retain` stop being resolved by name ~9x per
beat. `SunPositionTracker` moves its `geo_` reads into the `on_param_changed()`
hook it already declares.

Independent of everything below; land first.

### 1. `Vars::Value` -> `Vars::Slot`

Frees the name. The 32-byte per-row RAM struct is called "the slot" everywhere in
the prose already (`_editable_slot()`, "one slot per row", "a slot cannot hold a
hostname").

68 references across 13 files, and the only four outside `vars/` are test
fixtures. Mechanical, compiler-checked.

### 2. The new `Vars::Value`

Add the struct, the `as_*` family, `label()`, and the `T_STR` refusal. Nothing
calls it yet, so this stage adds and breaks nothing.

### 3. `raw()` -> `value()`, `value()` -> `as_f32()`

**The stage that needs the care.** Today's `raw()` returns the resolved value and
the new `raw()` would return the wire value: same signature, different layer, and
no diagnostic. So `raw()` must stop existing before it comes back meaning
something else.

Doing the rename here means the name is absent between stage 3 and stage 5, so a
missed site is a compile error rather than a behaviour change. 15 sites for
`raw()`, 70 for `value()` - the latter mostly caught by the `float` -> `Value`
type change.

`valid()` -> folded into `value().q`. `live_valid()` / `live_quality()` -> folded
into `live().q`.

### 4. Bank's family renames

`set`/`check`/`save` -> `param_*`, `text_at` -> `text`. Compiler-checked, ~77
sites, no behaviour change.

### 5. `wire()`

Only now is the name free. Computed rather than stored: for `AI`/`AO` it is
`analog_values(cal).to_raw(value())` - exactly what `rtu/image.cpp` already does
on the way out - for `MI`/`DI`/`RO` the wire *is* the value, and for `P`/`G`/`SV`
there is no wire, so `Q_NONE`.

No RAM cost: `Slot` stays 32 bytes and its `static_assert` stands.

## What is deliberately not changing

**Overrides stay legal on parameters.** `override_set` skips the band on purpose,
and that is the only deliberate way past a validator - a parameter is exactly
where that is sometimes wanted. `test_health`'s "an override on the threshold row
reaches it like any other reader" stays.

**Writer conflicts stay PLC-scoped.** `claim_writer()` records which control
*program* wrote a row; only `plc/io.h` calls it and `report_writer_conflicts()`
names the program through the task registry. It catches two programs driving one
actuator, not a program racing the RTU image. The name reads wider than the
scope, which is worth knowing, not worth changing.

**`Scalar` stays an untagged 8-byte union.** The type lives on the descriptor.
Giving `Scalar` a tag would break `sizeof(Scalar) == 8` and cascade: `NumBand` is
two of them, which is exactly the 16 bytes the `Payload` union budgets.
