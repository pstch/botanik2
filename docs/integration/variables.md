# The variable table {#int_variables}

Everything the firmware exposes is a **variable**: a Modbus meter reading, a relay
coil, a value a control program computes, a setting. There is one model and one
table. Nothing carries a byte offset, so reordering a table can never silently
re-point an alarm or a display row at its neighbour.

Variables differ along three independent **fields**, never types:

| Field | Says | Values |
|---|---|---|
| `VarType` | what the value *is* | `T_BOOL`, `T_U8`…`T_U64`, `T_I8`…`T_I64`, `T_F32`, `T_ENUM`, `T_STR` |
| `Transport` | how it reaches the field | `XP_NONE`, `XP_LOCAL_DIGITAL`, `XP_LOCAL_ANALOG`, `XP_MODBUS` |
| `VarRole` | what it is **for** | `ROLE_INPUT`, `ROLE_OUTPUT`, `ROLE_INTERNAL`, `ROLE_PARAM` |

The role is what drives behaviour:

| Role | Behaviour |
|---|---|
| `ROLE_INPUT` | goes invalid once `timeout_ms` passes with no refresh |
| `ROLE_OUTPUT` | driven to `def` when control authority is withdrawn |
| `ROLE_INTERNAL` | never expires, never swept |
| `ROLE_PARAM` | persisted, editable, always valid |

## Declaring one

`Vars::Variable` is a flash-resident aggregate, ordered widest-first so a row carries
no implicit padding:

| # | Field | Type | Meaning |
|---|---|---|---|
| 1-3 | `min`, `max`, `def` | `Scalar` | edit bounds (parameter) or plausibility band (input); see below for `def` |
| 4 | `name` | `const char*` | symbolic name - also the telemetry leaf and the storage key |
| 5 | `unit` | `const char*` | `"C"`, `"%"`, `""` |
| 6 | `desc` | `const char*` | shown in the detail panels |
| 7 | `str` | `const char* (*)(uint64_t)` | `T_ENUM` renderer, null for a plain number |
| 8 | `bind` | `Binding` | where the value comes from |
| 9 | `timeout_ms` | `uint32_t` | `ROLE_INPUT` staleness; 0 = never |
| 10-11 | `str_off`, `str_cap` | `uint16_t` | `T_STR` slot in the string pool |
| 12 | `type` | `VarType` | |
| 13 | `role` | `VarRole` | |
| 14 | `digits` | `uint8_t` | `T_F32` display decimals |
| 15 | `kind` | `StrKind` | `T_STR` edit validation: `STR_NONE`, `STR_IPV4`, `STR_IPV4_OPT`, `STR_HOST`, `STR_HOST_OPT` |
| 16 | `flags` | `uint8_t` | `F_INVERT`, `F_BOUNDED`, `F_RDONLY` |
| 17 | `_rsv[3]` | `uint8_t` | |

`Binding` is `{ transport, wire, device, fc, reg, regs, scale, offset }`. `wire` is
the type **on the wire**, deliberately distinct from the variable's own `type`: the
driver computes `value = wire * scale + offset`, so control logic never sees raw
counts.

`Scalar` is the 8-byte union every value, bound and default lives in
(`{ uint64_t; int64_t; float; const char*; }`), with `constexpr` factories `U`, `I`,
`F`, `S`, `B`. Use them - the `Scalar(int)` constructor is deleted so a bare literal
names itself in the error rather than converting silently.

@note **`def` does three jobs**, which is why it is worth getting right: an input's
substitute when the reading is invalid, an output's **safe state** when control is
withdrawn, and a parameter's factory value. A dead sensor reads as `def`, so choosing
a `def` that is out of the plausibility band means the control programs treat a dead
sensor as a fault without testing `valid()` at all.

### In practice: declare a macro per row shape

Spelling seventeen fields per row does not scale. The reference site defines one
macro per kind of variable and the table is a list of one-line calls:

```cpp
#define MTR(nm, unit, dgt, dev, obj, lo, hi, desc) \
    { Scalar::F(lo), Scalar::F(hi), Scalar::F(0.0f), nm, unit, desc, nullptr, \
      { XP_MODBUS, T_F32, dev, 3, obj, 2, 1.0f, 0.0f }, \
      5000, 0, 0, T_F32, ROLE_INPUT, dgt, STR_NONE, F_BOUNDED, {} }

const Variable g_vars[] = {
    MTR("pwr_L1_V", "V", 1, 5, 0, 0.0f, 500.0f, "L1 phase voltage"),
    ...
};
```

`constexpr` throughout keeps the whole table constant-initialised into flash rather
than `.data`.

## Referring to one

`VarRef` is a bare index - `struct VarRef { uint16_t index; }`, a struct rather than
a typedef so a stray `int` cannot become a reference. `VAR_NONE` is `0xFFFF`,
deliberately not 0, so a zero-initialised `VarRef` does not alias row 0.

Generate a parallel enum for readability:

```cpp
enum VarId : uint16_t { V_PWR_L1_V, V_PWR_L2_V, /* ... */ V_COUNT };
```

@note A bad index resolves to an inert **sink**, not to a clamped row. Clamping a
typo onto the last row of the table would drive a real relay.

## Binding the table

Three arrays and one macro:

```cpp
// vars.cpp - defined exactly once, or a second TU including the site breaks the link
Value    g_var_values[V_COUNT];
char     g_str_pool[STR_POOL_BYTES];
TaskStats g_task_stats[g_task_count];
```

```cpp
#define SITE_VAR_TABLES { g_vars, g_var_values, V_COUNT, \
                          g_str_pool, STR_POOL_BYTES,    \
                          V_NET, (uint16_t)(V_COUNT - V_NET) }
```

`VarTables` is `{ var, val, count, pool, pool_bytes, param_first, param_count }`.

@warning `SITE_VAR_TABLES` is a **macro, not a function**. Any function call in that
initialiser would make the `SiteDescriptor` dynamically initialised and drag the
whole thing out of flash into `.data`.

@warning **`ROLE_PARAM` rows must be a contiguous block at the end of the table.**
The parameter pages address them with a dense 0-based index, and keeping them last
means adding a setting never moves a field-IO index.

## Reading and writing at runtime

`Vars::Var` is the one non-owning handle over a `(descriptor, slot)` pair. It is
made on the fly and passed by value.

| | |
|---|---|
| Read | `raw()`, `value()`, `b()`, `i()`, `valid()`, `stamp()` |
| Write | `set(bool)`, `set(float)`, `set_i(int64_t)` |
| Publish | `publish(Scalar, now_ms)` - value, stamp and validity in one step; what a driver calls |
| Safe state | `safe()` |
| Override | `overridden()`, `override_set()`, `override_freeze()`, `override_clear()` |

An invalid variable reads as its declared `def`.

**`Vars::Bank` is the one object that gets, sets and overrides variables**, and it is
what consumers reach through `SystemContext::vars` rather than being handed their own
pointer. Two distinct writes:

- `set()` edits a **parameter**: validated against its declared band and persisted.
  Anything else returns `SET_INVALID` - writing a sensor or an output would just be
  overwritten by the next scan.
- `override_set()` **forces any variable**: volatile, range-checked against the type
  only, so a reading can be pushed past its plausibility band to exercise the alarm
  watching for it.

Overrides are applied inside `Var::raw()` / `valid()` - the single point every reader
goes through - so a forced value reaches control logic, console, telemetry and alarms
alike. See @ref dev_vars for how that is implemented, and @ref user_variables for
what it means operationally.

## Static asserts to carry

Each of these guards a **silent** truncation, so a site should keep all of them:

```cpp
static_assert(sizeof(g_vars) / sizeof(g_vars[0]) == (size_t)V_COUNT,
              "g_vars and VarId disagree: an index would address the wrong variable");
static_assert(STR_POOL_BYTES >= 8 * STR_CAP, "the string pool is too small");
```

Two more belong with the tables they guard - see @ref int_tables.
