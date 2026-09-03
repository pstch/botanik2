# Removing Plc::Program and Plc::Block

Supersedes `plc-block-model.md`, which planned a variable model for blocks that
generated ST does not need. Written 2026-08-30, after generating and compiling
each piece rather than reasoning about it.

## The target

A control program is ST. Nothing in `src/libbotanik/plc/` survives as a base
class: no `Program`, no `Block`, no `BOTANIK_PLC_BLOCK`, no `Refs` structs, no
29 subclasses. What remains is a runtime that calls generated C.

## What matiec already replaces, proven

| `Plc::Program` does | ST replacement | how it was checked |
|---|---|---|
| `bind()` | `__INIT_EXTERNAL` per row, at init | `harness_site` binds against the site's 425 rows with no binding table |
| `run()` | `X_body__` | same |
| `_bind_input` vs `_bind_output` | `VAR_EXTERNAL CONSTANT` vs `VAR_EXTERNAL` | matiec REFUSES a write to a CONSTANT: *"Assignment to CONSTANT variables is not allowed."* A compile error, which is stronger than our runtime claim |
| type compatibility at bind | matiec type-checks the assignment | a REAL into a BOOL is rejected at compile time |
| task registration, group and period | `TASK fast (INTERVAL := T#100ms)` plus `PROGRAM a WITH fast : p;` | the RESOURCE generates `FAST = !(tick % 1); SLOW = !(tick % 10);` and calls each body |
| dispatch ORDER within a beat | the order programs appear in the RESOURCE | generated verbatim |
| `ProgramContext::dt_ms` | a `DT_MS` global the scheduler writes | `sys_dt_ms`, written before each body |
| a block's state | a plain member of the POU's `_data__` | `STAIRCASE3` holds its rung across scans in `harness_site` |

`Plc::Block` needs no replacement at all. A `FUNCTION_BLOCK` becomes a
generated struct of plain members plus a body function: no base class, no
rows, no pool. That is the whole of the anonymous-row problem, gone.

## What has no ST equivalent

**Two programs writing one row.** matiec compiles it silently - two POUs each
declaring `cli_coolDem` in a writable `VAR_EXTERNAL` is legal ST. Our
`begin_writer`/`end_writer` window catches it today.

`VARIABLES.csv` lists every external reference per program but does not mark
which are CONSTANT, so it cannot tell a reader from a writer on its own. Two
ways out, neither yet built:

- **Runtime.** `__SET_EXTERNAL` is emitted only where a POU WRITES, so the
  first write to a row can record `&(prefix name)` - unique per POU instance -
  and a later write from a different one is the collision. One pointer compare
  after the first write, and a per-row pointer table that can be
  `TARGET_SIMULATOR` only, where the suite runs.
- **Source.** A row appearing in a non-CONSTANT `VAR_EXTERNAL` of two programs
  is the same defect, visible by reading the `.st` files. A test can do it; no
  build step.

## What needs a decision

Using the generated `config_run__(tick)` is what makes task registration free,
and it is also what removes the per-program bracket. Three things live in that
bracket today:

- the `prog_<name>` health row
- INVALID: a program whose `bind()` missed a row never runs at all
- the writer window

With the generated dispatch these become per-RESOURCE rather than per-program:
one health row, and a resource that refuses to run if any name missed. The
alternative keeps a hand-written table of `{name, init_fn, body_fn, data*}`
and calls each `X_init__` ourselves, which keeps per-program granularity at
the cost of not using the generated dispatch. **The table is data, not
classes, so it does not bring `Plc::Program` back** - but it does mean the
RESOURCE section stops being the source of truth for what runs when.

## Order

1. The shim counts resolve misses, so a name matching no row is a bring-up
   failure rather than a silent inert sink. `resolve_ci` already returns the
   sink; nothing counts it yet.
2. A `StRuntime` component: `config_init__()` at setup, `config_run__(tick)`
   at PHASE_CONTROL, `sys_dt_ms` before each body.
3. Port the ~21 law programs. The ~7 conditioning programs stay C++ - they
   read TRANSPORT quality, which ST could not express then, and matiec emits
   every FB body `static`, so a generated program cannot call a hand-written
   block. The boundary is per-PROGRAM, and it is the same line the `_qlt`
   companions drew. (Both halves went in the end: quality is a `QUALITY` row ST
   reads and writes like any other, and every program is ST. See
   `st-migration.md`.)
4. Delete `plc/program.{h,cpp}`, `plc/blocks/block.h`, and the subclasses.
