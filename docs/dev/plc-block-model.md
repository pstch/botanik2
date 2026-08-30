# Plan: one variable model for programs and blocks

Status: **agreed in outline, not started.** Written 2026-08-30.

The target is a `Plc::Block` that is a structured node like a `Program`: it
declares its inputs and outputs as variables, keeps its own state, and has
exactly three entry points — construction, `bind()`, `operator()`.

---

## 1. What changed my recommendation

I had proposed dropping `Bank*` from `Ref` first, so a reference could address
either a bank row or block-local storage. **That is not needed**, and the reason
is the decision to put block variables in the variable store as ANONYMOUS ROWS:

> if a block's inputs and outputs are rows in the same store the programs use,
> there is only ever ONE store, and `Ref` needs no polymorphism at all.

That removes the whole `VarStore` interface, the type-erasure through `Scalar`
it would have needed (`get<T>` is a template and templates cannot be virtual),
and ~450 call sites of churn. `x.get()` stays `x.get()`.

The pointer in `Ref` becomes an independent memory question — 4 bytes × 272
declared references ≈ **1 KB on the OPTA** — worth its own decision, at the end,
on its own merits. It is no longer on the critical path.

## 2. The two open questions

**Q1. Are a block's INPUTS rows too, or arguments to `operator()`?**

Both are legal ST: `TON(IN := a AND b, PT := t)` assigns at the call,
`TON.IN := x; TON();` assigns first.

| | rows | arguments |
|---|---|---|
| anonymous rows needed | ~100 | ~30 |
| RAM | ~3.9 KB | ~1.2 KB |
| `bands(strat.get(), lo, mid, hi, hys)` | a row, a write, a call | one line |
| rule to remember | none | "composites bind, primitives take arguments" |

The case for rows is that there is no line to remember. The case for arguments
is that the cost lands on the ~40 primitive instances where the benefit is
smallest. **Undecided.**

**Q2. Does `Ref` keep its `Bank*`?** ~1 KB against ~450 mechanical edits and
laws reading `io.get(strat)` rather than `strat.get()`. **Undecided, and last.**

## 3. Stages

Each stage builds, passes `make check`, `check-bare`, `clang-tidy` and the OPTA
cross-build, and re-pins `libbotanik_sizes.h` on both targets.

### Stage 1 — anonymous rows in the bank

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

### Stage 2 — composite blocks take their outputs as rows

The four site blocks already hold a `Refs` struct (`AcSupplyRefs`,
`ZoneProbeRefs`, `WeatherProbeRefs`, `ValveRefs`), so this is small:

- `operator()` returns `void`; what it returned becomes a `RefOut` in the struct.
- `ZoneProbes::temperature()`, `humidity()`, `valid()` go — a caller reads the
  output row it already bound.
- `AcSupply::operator()` already drives `ok` and `fault` as rows; its return
  (whether every quantity answered) becomes one.

### Stage 3 — library primitives take their outputs as rows

The big mechanical one: **56 block instances** across the site.

- `Tof::q()` / `elapsed_ms()` become `RefOut<bool> q` / `RefOut<uint32_t> et`,
  which is what `TON.Q` and `TON.ET` are in ST.
- Same for `Ton`, `Ewma`, `Hysteresis`, `Deadband`, `Staircase3`, `Pid`,
  `Integral`, `Derivative`, `Actuator`, `MovingAverage`, `WindowedMax`.
- Q1 decides whether inputs move with them.
- An output that feeds only another block gets an anonymous row from stage 1;
  one that feeds a published variable binds that row directly, with no
  intermediate.

`tests/test_plc.cpp` is written against the current call shape and moves with it.

### Stage 4 — the laws

Intermediates that cross a block boundary become anonymous rows. A readability
local (`const float t_in = inside.get();`) stays a local: a row costs ~39 bytes
and buys nothing for a value read once, two lines down.

### Stage 5 — `Ref` loses `Bank*` (optional, decided last)

Only if Q2 says the kilobyte is worth it. Mechanical but wide.

## 4. Costs, measured rather than guessed

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

`VAR_MAX` goes 512 -> ~600-700 depending on Q1; the bank grows by the pool.

## 5. Risks

- **Rows are allocated, not declared.** Today the table is a frozen `constexpr`
  array and `tests/test_site.cpp` leans on that. A bind-time pool is a new
  mechanism, and `unresolved_count()` cannot vouch for a row with no name -
  the equivalent guarantee is "the pool did not overflow".
- **Scope leakage.** Every filter that walks the table (telemetry, TCP map,
  MONITOR, params, alarms) must exclude `SCOPE_LOCAL` explicitly. 39 sites
  reference a scope or `ROLE_PARAM`; each wants checking once.
- **Instance count multiplies.** A block class is instantiated many times
  (`Plc::Ewma` x12, `ValveController` x8, `ZoneProbes` x4), so a row per
  instance is what is being paid, not a row per class.
- **Primitives are already close to ST.** `ton(in, pt, dt)` with `ton.q()` maps
  onto `TON(IN:=..., PT:=...)` / `TON.Q`. Stage 3 is a real improvement on the
  output side and a real cost on the input side; Q1 is where that is decided.
