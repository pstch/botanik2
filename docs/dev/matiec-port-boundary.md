# Porting onto matiec: what goes, what stays, what has no home

Measured 2026-08-30 on branch `matiec`, after `destrat` and `target_temp`
compiled and ran against a real bank.

## 1. What matiec REPLACES

| today | becomes |
|---|---|
| a program's `.cpp` body | ST between `PROGRAM` and `END_PROGRAM` |
| a program's `.h` - members and `bind()` | generated: `_data__` is the members, the CONFIGURATION's connection list is `bind()` |
| `programs.h`, the X-macro of execution order | the `RESOURCE`'s `PROGRAM ... WITH <task>` list |
| `plc/blocks/*` - Tof, Ewma, Staircase3, Pid... | `FUNCTION_BLOCK` in an ST library |
| the site's block wrappers (`ZoneProbes`, `AcSupply`...) | the same, one `FUNCTION_BLOCK` each |

`st/lib/botanik.st` has three of them already and compiles. **Not** matiec's
`lib/*.txt`: those are GPL and a translation of them is a derivative work.

## 2. What SURVIVES untouched

Everything that is not a law. matiec generates a body; it has no opinion about
where the values live or how they reach a wire.

`vars/` (bank, Ref, quality, overrides, persistence), `tasks/`, `scheduler/`,
`hal/`, `modbus/`, `network/`, `console/`, `pages/`, `shell/`, `logs/`,
`alarms/`, `health/`, and the `Component`/registry half of `Plc::Program`.

The `_data__` struct becoming a struct of `Vars::Ref` is what joins the two,
and it is 41 lines of macros.

## 3. What has NO HOME in ST - the real cost

### Quality, and it is most of the tree

**21 of 29 programs read `.valid()` or `.held()`.** ST has no notion of it, and
no expression can be written for it: `blind := NOT valid(rain) OR NOT
valid(wind)` is not IEC.

On the write side it is smaller but not small: **15 of 91 output writes carry a
COMPUTED quality** (`demand.set(dem, measured)`); the other 76 pass `true` and
port unchanged.

So "quality lives in blocks, ST expresses the law" costs more than it sounds.
For a law that is MOSTLY a quality decision - `strat_sensor` is two lines, one
of which is `measured := valid(ceiling) AND valid(floor)` - the ST left over
says almost nothing, and the block it delegates to is the law.

Three ways out, none free:

1. **Every quality-reading law stays C++.** 21 of 29 programs, so ST would
   describe the 8 that are pure arithmetic. Not worth a compiler.
2. **Quality becomes a companion variable.** For each row `X`, an `X_qlt` the
   shim keeps in step. ST reads it like any other row and the cascade survives -
   at the cost of doubling the visible variable count and letting a law write a
   quality it did not earn.
3. **A hand-written FB the generator cannot see through.** `VAR_IN_OUT` passes
   a reference, so a `VALID` block WOULD receive the `Ref` and could ask it -
   but matiec generates every FB body from ST, so the body would have to be
   swapped after generation. That is a post-processing step on generated code,
   which is the thing that makes a toolchain unmaintainable.

**(2) is the only one that keeps both, and it is a design change to the
variable model, not an integration detail.**

**Resolved: (2), taken all the way.** Neither objection survived. The doubling
does not happen because the companions are GENERATED from a rule rather than
declared - one per input, one per row a program that opted in writes - which is
127 rows on `gh_mlb41`, not one per row. And "a law writing a quality it did not
earn" stopped being possible to worry about once the companion became the ONLY
store: there is no second truth for it to contradict, and `tables.py plant`
refuses an opted-in program that leaves one of its own unwritten. See
`quality-companions.md` for the rule and `vars.md` for the model.

`roof_lockout` is the worked case and the reason the rule exists. Both weather
rows declare 0, so a dead station reads as fine weather; `roof_lockout.st` runs
on `GOOD(cli_rain_mmph_qlt)` and `GOOD(cli_wind_mps_qlt)`, and its harness shows
the roof opening to `cap = 1.00` the moment the two reads are taken out.

### The descriptor

IEC's `VAR_GLOBAL` carries a type and an initial value. A `Descriptor` carries
`def`, `name`, `unit`, `desc`, `notes`, `pay` (band or pool slot), `bind`
(transport, device, fc, register, word order), `timeout_ms`, `poll_ms`, the
enum renderer, `type`, `role`, `scope`, `digits` and `flags`.

Everything past type and default has no ST spelling. `AT %IW100` covers the
register and nothing else - not the device, the function code, the poll period,
the staleness timeout or the plausibility band.

**So `vars.cpp` does NOT go away.** matiec's `VARIABLES.csv` lists name and
type, which is the half we could check the table against rather than generate
it from.

### The scan's own facts

`first_scan` and `dt` are ProgramContext fields with no ST equivalent - the
standard's runtime supplies cycle time implicitly and has no first-cycle flag.
Both had to become `VAR_INPUT` in `st/target_temp.st`. That works and is
honest, but every program declares two inputs that are not about the plant.

## 4. What the experiment proved on the way

- **`DT` is a reserved word** (DATE_AND_TIME). Ours is `DT_MS`.
- **matiec's C compiles as C++** unmodified.
- **EN/ENO and every private VAR need rows** - three per FB instance before
  its interface counts.
- **`Ref` keeping its `Bank*` is what makes the generated code work as-is**,
  which is in tension with stage 1 of the block plan.

## 5. The honest read

The integration is cheap - 41 lines of macros, proven twice. The PORT is not,
and the reason is not effort: it is that this codebase's best property, a
per-variable quality that cascades, is an extension the language cannot
express. Decide (2) above before porting anything else, because every law
written in ST before that decision would be rewritten after it.
