# Plan: one variable model for programs and blocks

Status: **agreed, not started.** Written 2026-08-30.

The target is a `Plc::Block` that is a structured node like a `Program`: it
declares its inputs and outputs as variables, keeps its own state, and has
exactly three entry points - construction, `bind()`, `operator()`.

---

## 1. Both questions are answered

**Q1. A block's INPUTS are rows**, like its outputs. Both spellings are legal
ST - `TON(IN := a AND b, PT := t)` assigns at the call, `TON.IN := x; TON();`
assigns first - and the second is chosen because there is then no line to
remember: every block, primitive or composite, declares its whole interface and
is invoked with none of it.

**Q2. `Ref` loses its `Bank*`.** Every read and write goes through the store:
`io.get(ref)`, `io.set(ref, v, current)`. That recovers 4 bytes per declared
reference, about **1 KB on the OPTA** across the 272 in the site's programs
today, and more once every block instance declares one per input and output.

### What Q2 changes about the order

Removing the pointer was going to be last, on the grounds that anonymous rows
put every variable in ONE store, so `Ref` needed no polymorphism and the pointer
was only a memory question. That reasoning still holds - there is no `VarStore`
interface in this plan, and no type-erasure through `Scalar` - but the ORDER
inverts:

> Stages that follow ADD references and call sites. Removing the pointer
> afterwards would mean writing new code in the old spelling and converting it
> too.

So it goes first. Convert the ~450 existing sites once, and every block written
after it is written in the new spelling.

### What it changes about a block

With no pointer in a `Ref`, a block cannot reach the store on its own, so the
store is an argument:

```cpp
void operator()(Vars::Bank& io, uint32_t dt_ms);
```

which is what makes a block genuinely storage-agnostic: it holds indices, and
whoever calls it says which table they index.

## 2. Stages

Each stage builds, passes `make check`, `check-bare`, `clang-tidy` and the OPTA
cross-build, and re-pins `libbotanik_sizes.h` on both targets.

### Stage 1 - `Ref` loses its `Bank*`

- `Ref<T,D>` becomes `{ VarRef r; }`, 2 bytes.
- `get` / `set` / `valid` / `hold` / `held` / `invalidate` / `edit` move onto
  the store: `io.get(ref)`, `io.set(ref, v, current)`.
- ~450 call sites, mechanical. The laws read `io.get(strat)` rather than
  `strat.get()`, which is the price.
- `Program` already holds `_io`, and every `run()` already receives the bank
  through its `ProgramContext`.
- Wide and shallow, so it lands in ONE commit: a bisect should come down either
  side of it, never inside.

### Stage 2 - anonymous rows in the bank

- `SCOPE_LOCAL` in `VarScope`. It must fall outside `var_published()` and
  `var_mapped()` **deliberately**: local rows currently dodge telemetry only by
  the null-name check in `telemetry.cpp`, which is luck rather than intent.
- A pool after the declared rows, sized by `BOTANIK_VAR_LOCAL_MAX`.
  `Bank::alloc_local(VarType, Scalar def) -> VarRef`, callable after `freeze()`.
- Overflow is counted and reported at bring-up, like `rows_unscanned()`.
- Tests: allocation returns distinct rows; a local is absent from the TCP map,
  from telemetry and from the parameter block; the pool reports overflow rather
  than aliasing.

Nothing uses it yet. Landing it alone keeps the diff readable.

### Stage 3 - composite blocks take their interface as rows

The four site blocks already hold a `Refs` struct (`AcSupplyRefs`,
`ZoneProbeRefs`, `WeatherProbeRefs`, `ValveRefs`), so this is small:

- `operator()` returns `void`; what it returned becomes a `RefOut` in the struct.
- `ZoneProbes::temperature()`, `humidity()`, `valid()` go: a caller reads the
  output row it already bound.
- `AcSupply::operator()` already drives `ok` and `fault` as rows; its return
  (whether every quantity answered) becomes one.
- `ValveController::set()` and `park()` become inputs. `set(open, current)`
  maps onto `io.set(ref, v, current)` exactly, which is neat.

### Stage 4 - library primitives take their interface as rows

The big mechanical one: **56 block instances** across the site.

- `Tof::q()` / `elapsed_ms()` become `RefOut<bool> q` / `RefOut<uint32_t> et`,
  which is what `TON.Q` and `TON.ET` are in ST.
- Same for `Ton`, `Ewma`, `Hysteresis`, `Deadband`, `Staircase3`, `Pid`,
  `Integral`, `Derivative`, `Actuator`, `MovingAverage`, `WindowedMax`.
- Inputs move with them: a primitive declares a `RefIn` for each and is invoked
  as `ton(io, dt_ms)`.
- An output that feeds only another block gets an anonymous row from stage 2;
  one that feeds a published variable binds that row directly, with no
  intermediate.

`tests/test_plc.cpp` is written against the current call shape and moves with it.

### Stage 5 - the laws

Intermediates that cross a block boundary become anonymous rows. A readability
local (`const float t_in = io.get(inside);`) stays a local: a row costs ~39
bytes and buys nothing for a value read once, two lines down.

## 3. Costs, measured rather than guessed

| | |
|---|---|
| `Vars::Slot` | 32 B RAM |
| `Vars::Descriptor` | 104 B host flash |
| a bank row, all in | ~39 B RAM (`Bank` is 19784 B for `VAR_MAX` 512) |
| a scalar member today | ~4 B |
| block instances held by the site | 56 |
| scalar state members in blocks + programs | 79 |
| intermediates in the site's laws | ~100 (upper bound) |
| declared `Vars::Ref` members in programs | 272 |
| call sites of the `Ref` surface | ~450 |

`VAR_MAX` goes 512 to about 650. The pool costs roughly 5-6 KB of RAM; stage 1
gives about 1 KB of it back. Net around +5 KB on the bank's 19.7.

## 4. Risks

- **Rows are allocated, not declared.** Today the table is a frozen `constexpr`
  array and `tests/test_site.cpp` leans on that. A bind-time pool is a new
  mechanism, and `unresolved_count()` cannot vouch for a row with no name: the
  equivalent guarantee is "the pool did not overflow".
- **Scope leakage.** Every filter that walks the table (telemetry, TCP map,
  MONITOR, params, alarms) must exclude `SCOPE_LOCAL` explicitly. 39 sites
  reference a scope or `ROLE_PARAM`; each wants checking once.
- **Instance count multiplies.** A block class is instantiated many times
  (`Plc::Ewma` x12, `ValveController` x8, `ZoneProbes` x4), so a row per
  instance is what is being paid, not a row per class.
- **Primitives get more verbose.** `rain_for(io.get(rain_mmph) > 0.0f, ...)`
  becomes a row, a write and a call. That is the accepted price of Q1: no rule
  to remember about which blocks bind and which take arguments.
- **Stage 1 has no behaviour change to test.** ~450 mechanical edits with only
  the existing suite standing behind them.
