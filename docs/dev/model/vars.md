# The variable table {#dev_vars}

<!-- source of truth: vars/descriptor.h, vars/model.h, vars/scalar.h, vars/declare.h, vars/persist.h -->

Every value the firmware publishes, tunes, persists or puts on a wire is a ROW
in one array. There is one array per unit, generated whole, and one index space
over it. Nothing registers a variable at run time, and there is no second table
beside the table: a counter only C++ can read is one no ST program can meter
with.

A row is two halves. The CONST half is a `Descriptor`, which lives in flash and
never changes. The MUTABLE half is a `Slot`, 24 bytes of RAM, parallel to it by
index. `Bank` owns both arrays; `Var` is a (bank, index) pair and is where every
row-level operation lives.

## The generated table

`scripts/sitegen` merges the library's own rows (`vars/tables/library.csv`) into
the site's, orders them, and emits one `Descriptor[]` plus a `Vars::Row::<name>`
constant per row. The library's parameters come first, so the contiguous
`K_PARAM` run holds; registers 900-999 are the library's.

Because an index is a bare `uint16_t` and zero is a real row, `VAR_NONE` is
`0xFFFF` rather than 0, and a row index must never be read back out of a `const`
table it was declared in.

A generic component addresses a row by its generated constant, so a name no
table declares is a compile error rather than a lookup that fails at bring-up.
`Bank::find()` remains for the two callers that take a name from a human: the
console and a log sink naming its own level row.

## Bank

`Bank` lays the RAM out over the generated table at construction: a `Slot` per
row, a `char` pool for the text rows, and a small wire array for the analog ones.
Sizes are computed by the generator (`VAR_MAX`, `STR_POOL_BYTES`, the analog
counts), so a site never states its own.

A bad index does not clamp onto a real row. It resolves to an inert sink
descriptor, so a mis-wired caller reads zero and writes nothing rather than
quietly corrupting its neighbour.

## Var

`Var` is the whole row-level API: read live or declared or forced, publish with
a quality, edit a parameter, force and release an override, ask for the band or
the calibration. It carries no state of its own - it is two words - so passing
one by value is free and holding one across a beat is safe.

Quality is not a field on a value. It is a ROW: the companion immediately after
its subject, marked by `F_QLT`, which is why `quality_row()` is `_i + 1` and why
ST reads a quality with the same `__GET_EXTERNAL` as anything else. Adjacency IS
the pointer, so there is no pairing pass and no second map to keep in step.

`Ref<T>::drive()` resolves a constraint ST creates: the language has one
assignment operator, so the ROW'S KIND decides whether a write is this scan's
answer or an edit owed to the retain cadence.

## Scalar

One value slot, 8 bytes, whatever the type - `uint64_t`, `int64_t`, `float`, or
a `const char*` holding a T_STR's DEFAULT text. A live T_STR is not in the slot;
it is in the bank's char pool, because a slot cannot hold a hostname.

### Widths {#scalar_width}

Nine integer widths, a float, a bool, an enum and a string, because IEC 61131-3
has them and a law that declares a `UINT` must get a `UINT`. The widths cost
nothing at run time: `Value::as<T>()` derives both rails from `sizeof(T)`, so a
narrowing read saturates at ITS own rail without `<limits>` or `<type_traits>`.

Every conversion out of a `Value` SATURATES. 300 read as a `u8` is 255, not 44: a
wrapped value is indistinguishable from a real reading, while a pinned one is
obviously at the rail. That is also why there is no free `as_i64(VarType,
Scalar)` - one that did not clamp would be the same name with quieter semantics,
and casting a float to an integer is undefined for a NaN or an out-of-range
magnitude.

### One tag {#scalar_one_tag}

`Scalar` is a TAGLESS union. It does not know which arm is live, and it is not
supposed to: the tag is the row's `VarType`, stated once in the descriptor, and
a second copy inside the value would be a second thing to keep in step.

The cost is that a bare literal cannot build one, since `0` suits `uint64_t`,
`int64_t` and `float` equally well. So `Scalar(int)` is deleted - a stray
`Scalar(0)` then names itself in the error instead of listing four equally good
candidates - and the table is written through `SU()`, `SI()`, `SF()`, `SB()`,
which say the type in two characters.

A `Value` is the pair: a `Scalar` and the type to read it as. It is not always
the row's own type - `Var::wire()` answers in the WIRE type, a `u16` count
feeding an `f32` row - which is exactly why the two travel together instead of
every call site carrying the `VarType` alongside by hand.

### Flash-backed values {#scalar_flash}

The whole `constexpr` discipline exists to keep a site's `Descriptor[]` in
flash. A reference site's table is tens of kilobytes; the moment one factory
stops being a constant expression, that moves to RAM and gains a boot-time
initialiser loop.

Nothing enforces this at the language level, so `scalar.h` pins it with
`static_assert`s that evaluate the factories in a constant expression. They are
not testing arithmetic - they are testing that the arithmetic still HAPPENS at
compile time.

## Declaring a row

Sixteen `constexpr` factories over six kinds. Each takes ONLY the arguments its
spelling can use, so a meaningless row is unspellable: a relay with a
plausibility band, an alarm with a timeout, a parameter with a device address.
The fields a row does not carry are absent from the call and the factory
supplies them.

This is why the factories are worth their lines. The counterfactual - writing
`Descriptor{...}` with its full positional field list at every row of every site
- is larger, not smaller, and loses the property that the compiler refuses a row
that means nothing.

### Local terminals {#declare_local}

A local terminal is the controller's own input or output, not a device on the
bus. It carries its channel in `reg`, which a local row has no other use for,
and `LocalInputs` finds every one of them by walking the table at bring-up
rather than from a list somebody maintains. `F_INVERT` is folded in at publish
time, so a law reads `true = asserted` whatever the wiring does.

A pulse terminal is the running EDGE COUNT, as a row - not a level, because the
level of a pulse line is a moment of a train rather than a reading. A meter's law
differences it and carries the 32-bit wrap itself.

### Flash storage {#declare_flash}

See @ref scalar_flash: the factories are `constexpr` so their output is a
load-time constant. The field ORDER in `Descriptor` is load-bearing for the same
reason - it is what keeps the struct at its pinned size, and a reordering that
adds padding costs that padding on every row of every site.

## Analog calibration

An `AI`/`AO` row points at a shared `const AnalogCal`: six ends - two raw, two
engineering, two plausibility - each of which may be a fixed number OR the name
of another row. `scale` and `offset` are derived rather than stored, so a
recalibration lands the moment the parameter commits.

The six ends are indexed by `CalEnd`, with the values and the rows in parallel
arrays. Named pairs would read better and cost half again in flash: a
`{Scalar, uint16_t}` pair pads to 16 bytes, so six of them are 96 where the two
arrays are 64.

## Bands, and why one may name a row

A pair with a required relationship - a target's floor and ceiling, a ramp's
start and end - is only wrong AS A PAIR, and a fixed band cannot see that. So a
band's ends carry `lo_row` / `hi_row` beside the numbers, the generator resolves
them to indices, and `param_check()` judges an edit against `Var::band()` - which
is whatever the other row holds NOW.

Constrain both ends against each other and the pair stays reachable as long as it
starts valid, which the generator refuses to emit without. A band whose two ends
are equal and fixed is no band, which is why there is no flag saying so.

## Persist

Three namespaces - parameters, overrides, retained rows - over one key-value
store, because they have different lifetimes and different owners and a single
namespace would make a forced value indistinguishable from a tuned one.

Two dirty bits, not one. `S_PARAM_UNSAVED` is an operator's edit waiting for
`params save`; `S_PARAM_OWED` is a PROGRAM's durable write waiting on the retain
cadence. They are distinct because each must survive the other: a law writing a
totalizer must not clear an operator's pending edit, and vice versa.

Flash wears, so writes are paced rather than immediate, and `param_set()` refuses
a no-op write outright - without that, a Modbus master polling a parameter at its
own rate would erase a sector per poll.

`save_retained()` writes the LIVE value, never the override. A forced totalizer
is a diagnostic, and it must not enter plant history.

An override is logged BEFORE it is applied, so the displaced value is what the
log records. Forcing a row also sets its `_qlt` companion to `Q_GOOD`, which is
the correct semantic - a forced value IS known - and the one most
implementations get wrong.

## What this model does not do {#vars_blind_spots}

Stated here because an absent check reads like a forgotten one.

- **No per-point access control.** Anyone who reaches the console can force any
  row, including a relay. The act is logged and the override is visible on the
  PARAMS page and over Modbus, but nothing gates it by operator. The unit's
  security boundary is the console port and the plant network, not the row.
- **Linear calibration only.** Two points and a straight line. No square-root
  extraction for a differential-pressure flow, no thermocouple or RTD
  linearisation. A law that needs one does it itself.
- **No change-of-value history.** A row remembers its current value, its
  override and when it was last written. The audit line for an operator edit is
  in the log; there is no per-row trend buffer, which is the historian's job and
  not a controller's.
