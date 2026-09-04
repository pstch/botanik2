# The scan cycle {#dev_scan}

How the control programs are scheduled, what they are guaranteed to see, and what
control authority does to them. The site-facing view - writing a program, the
function blocks - is @ref int_tasks; this page is the cycle itself.

## Two axes: rate and phase

A task has two independent attributes, and conflating them is the mistake to
avoid.

**Rate** is how often it runs - its group. **Phase** is where in the beat it sits.
Neither derives from the other: `sys_expire_stale` is background-rate and
input-phase; a control program is whatever rate and always execute-phase. A
program names a rate GROUP like anything else in the beat - it has no private
period, so retuning a rate class retunes it too.

Rate is a tuning knob. Phase is a correctness property - it encodes the data flow,
and getting it wrong corrupts a cycle rather than just costing CPU. So the phase
order stays written out as code in `firmware/firmware.h`, and only the rate is a
property of the task. A group must never imply an order, or retuning a rate would
silently reorder the cycle.

There is deliberately **no priority**. Priority in a PLC means preemption, and
there is none here: one cooperative loop, every step runs to completion
(@ref dev_concurrency). Ordering is the phase; the only meaningful "priority"
would be load shedding, and shedding a control program is not something that
should ever be automatic.

## The beat

Six phases run in this order, every beat: INPUT, CONFIG, SERVICE, EXECUTE,
OUTPUT, PRESENT, then the watchdog kick.

@image html beat.drawio.svg "One beat. The number on a task is its group period in ms; a task runs only on the beats its group has come due, so most beats are far emptier than this. Only the gated site hook is drawn - the five ungated ones sit at the end of their own phases."

Within a phase, tasks run in registration order; a task runs only on the beats its
group has come due (@ref dev_scheduler). The sandwich is **read, execute, write**,
and it holds on every beat regardless of which programs happen to be due: whatever
is due runs between this beat's input phase and this beat's output phase.

The dashed box is the control-authority gate. It is `Plant::scan()` alone now -
*Where the gate lives* below is the whole rule.
Note also that `sys_mbtcp_wr` sits in SERVICE although it is registered beside
its TCP siblings much further down the function: a task declares its phase, so
where its line falls in `_register_tasks()` says nothing about when it runs.

@note The diagram is `images/beat.drawio.svg`, an SVG that doubles as its own
draw.io document - open it in draw.io or the VS Code extension and save, and it
re-renders in place. Every diagram under `images/` works this way; see
[Diagrams](@ref dev_build) for the convention.

## What a program is guaranteed

- **Table order is execution order.** The programs run in the order the site
  declared them - not alphabetically, not by rate, not by construction order.
  They register in table order, and within a phase the registry dispatches in
  registration order, so the site's array IS the order.
  So a law that computes a setpoint and a law that acts on it complete in *one*
  beat when written in that order, and take a beat per link when written the
  other way. That is a contract sites rely on, so `test_plc_io.cpp` pins it both
  ways round: the same three programs in two orders must produce two orders.
- **The image is stable within a beat.** It mutates only between phases, never
  under a running program, because every writer is cooperative and runs to
  completion. Two programs due on the same beat agree about what they read.
- **`dt_ms` is the real elapsed time**, never the nominal period, and a program
  due late runs *once* - missed activations are dropped, not caught up. A burst of
  catch-up runs would land exactly when the system is already behind.
- **Staleness is explicit, not inferred.** A row past its timeout goes invalid
  (`Bank::expire_stale()`), so logic reads quality rather than guessing from the
  value. A row whose reading has left its declared band goes invalid too, judged
  as it arrives (`publish()`, or `Bank::publish_wire()` for a scaled row, whose
  band is in the calibration's parameters) - a dead 4-20 mA loop reads full
  scale, and no timeout catches that.
- **A program always writes its outputs**, every beat, whatever its inputs did.
  It runs the same law on whatever those inputs hold. Skipping the write leaves
  a stale number that still reads as current, which is worse than a fresh one
  computed from doubtful inputs.
  ST has no way to write a quality, so a derived row that carries doubt carries
  it in a COMPANION the law writes beside the value - `cli_dpMargin_degC` and
  `cli_dpMargin_degC_qlt`. @see @ref dev_quality_companions.
  An ALARM boolean cannot carry quality at all, so the only policy it can
  express is that an unmeasured condition raises nothing - which is why every
  alarm is gated on the companion of the row it reads.

Rows carry independent ages, and nothing pretends otherwise: each has its own
`stamp_ms` and `valid`. Snapshotting the image into a per-cycle shadow would
restore "one instant" only in appearance - it would make the values *stable*,
which they already are, without making them *contemporaneous*.

## How a program declares its variables

A program names the rows it uses in its own `VAR_EXTERNAL` blocks, and the
section it puts a row in IS the declaration of intent:

```
PROGRAM dc_watch
VAR_EXTERNAL CONSTANT      (* read: the program may not write these *)
  pwr_dc1_V     : REAL;
  pwr_dc1_V_qlt : QUALITY;
END_VAR
VAR_EXTERNAL               (* written: this program is the row's author *)
  sys_dc_ok     : BOOL;
END_VAR
```

There is no per-row `bind()` to write, and nothing is resolved at bring-up.
`__INIT_EXTERNAL` binds each row to a generated INDEX - `ROW_PWR_DC1_V`, which
`plant.cpp` defines from `Vars::Row::pwr_dc1_V` - so a name no table declares
fails to COMPILE. That is the whole check: there is no unresolved count, no sink
a law could sit on for the life of the run, and no bring-up verdict about it.

The type is the row's own, and a site ENUMERATION is a type like any other: a
phase compares against `PHASE_DAY` rather than against 3 (@ref dev_st_enums).
The reference used to carry a DIRECTION too, so a write to a
`VAR_EXTERNAL CONSTANT` row failed to compile - but every ST row resolves
`VAR_IN_OUT`, so that guard could never fire. The section is checked at
**generate time** instead, which is stronger:
`scripts/tables.py plant` reads what matiec says each body writes
(`__SET_EXTERNAL`) and fails the build on

- a row declared in `VAR_EXTERNAL` that the body never writes (it belongs in
  `VAR_EXTERNAL CONSTANT`, and leaving it there puts a row the program only
  reads on its safe-state roster), and
- an output, alarm or status row that **no** program writes at all.

Two programs writing one row is reported rather than refused: read-modify-write
shared state is the design, and the irrigation force flags and watering dates
are exactly that.

## Program health

A program answers for itself with a `Plc::Health`, and there is one answer:

| | | |
|---|---|---|
| `HEALTHY` | 0 | wired, and running |

There was a second, `INVALID`, for a program that named a row no table declared.
It cannot happen now: a binding is a generated index, so such a program does not
compile. The code is published as a `T_ENUM` row, so the vocabulary may gain
labels and must never renumber them.

Anything finer belongs to the ROWS. A reading that is not believable says so in
its own quality, and every consumer already reads that; a program-level
"degraded" would be a second, coarser place to say the same thing.

## Control authority

Two INDEPENDENT sources decide whether a law may drive the field, and the
STRICTER wins:

| Source | What it is | Where |
|---|---|---|
| the OPERATOR's standing choice | `sys_control` for all of them, `programs disable <pat\|all>` for one | a retained parameter; the per-program gate is not retained, so a maintenance session cannot leave one silently out after a power cut |
| the SYSTEM's hold | anything but `Scheduler::RunState::RUN` - `sys_startup_sec` at bring-up, `sys_shutdown_sec` on the way out | @ref dev_health |

`Plc::Runtime::Plant::running()` is where they meet.

**A program that is not running does not FREEZE what it drove.** Its rows are
WRITTEN to their declared value - which is what `Descriptor::def` means for an
output - and the write marks them owed to the field, so the coil actually moves.
A `_qlt` companion in the same roster drops to `Q_NONE` with them. A coil left
energised by a law nobody is running is the thing this exists to prevent.

### Parking {#dev_scan_parking}

A declared value is where a row READS. For an actuator that has to travel, it is
not where the actuator IS: a roof half open when its law stops is a roof left
half open, whatever `cli_window1_tgt` says afterwards.

So a program may **park**. It says so with `quality`'s neighbour `park` in
`tables/programs.csv`, and gets two generated rows:

| row | who writes it |
|---|---|
| `prog_<name>_park` | `Plant`, when the program leaves the scan |
| `prog_<name>_parked` | the program, when it has arrived |

A parking program is not dropped on the edge out. `park` goes true, its body
**keeps being called** - it needs scans to drive the travel - and the rows go to
their declared values only once it answers on `parked`. `Plant::parking()` is
true throughout, and the firmware's STOP waits on it, so a reboot shuts the roof
before the unit halts rather than five seconds into the travel.

Bounded at both ends. `plc_parkTimeout_sec` caps any one park: past it the
program is dropped exactly as a non-parking one would be and the log names it,
because the way out is not allowed to depend on an actuator. And a park in
flight when the program comes back into service is abandoned where it got to,
which is where the law is about to take over from anyway.

`tables.py plant` refuses a program that opts in and never writes its own
`_parked` row: one that never answers holds the shutdown open for the whole
timeout, every time.

**Not every actuator wants one.** The thermal screen's safe state is wherever it
was - a deployed screen left deployed is not a hazard, and travelling it on the
way out would be a movement nobody asked for. Parking is for the ones where a
position is a risk: the roof against the weather, the irrigation valves against
a tray left full.

Which rows those are cannot be asked of the bank: an ST row resolves
`VAR_IN_OUT` and records no exclusive writer. The roster is generated per
program from what matiec says its body writes, and the drop happens on the EDGE
out of running - so the shutdown path gets it for free, since leaving `RUN` for
`STOP` is that edge.

**The whole body is held, not each program's inputs.** Every stateful block
seeds on its FIRST call - a filter adopts the reading it starts on, a timer
datums its clock - so a held program starts clean rather than climbing out of a
fiction it was seeded with.

## Where the gate lives

At the one dispatch point every program goes through: `Plant::scan(i, now_ms)`.
`Vars::Var::set()` is not gated - the handle is two pointers passed by value,
made on the fly and passed everywhere, and giving it a route to the verdict
would cost bytes on the hot path to re-answer a question the caller already
knew.

The clock and the date are published even while held, so `sys_now_ms` stays
truthful; only the laws wait.

Everything else in the beat runs regardless, and that is deliberate: the other
phases are bookkeeping, derived values and telemetry, all still correct while
the plant is stopped. Withholding control writes safe states into the image, and
something still has to carry them outward.

## What a block promises

### It seeds on its first call {#blk_contract}

A FUNCTION_BLOCK keeps its state in its own `VAR` section, and there is no
`reset()` to call. There used to be: a C++ block had one, and a program called
it on the blocks it chose when control authority came back. The ST blocks
replaced it with something the caller cannot forget - **every stateful block
seeds on its first activation**. A filter adopts the reading it starts on
rather than climbing to it from zero; a timer datums its clock rather than
measuring from an instant that predates the outage.

That is why the control gate holds the whole program BODY rather than its
inputs: a held program is one that has not had its first call yet, so when it
gets one it starts clean.

### HOLD, not "do not call it" {#blk_hold}

A block that reads the clock takes `NOW` - `sys_now_ms` - and differences it
itself, which is what makes it rate-independent and lets the same block work on
any task. The consequence is that a caller must not skip it:

```
  filt(X := raw, TAU_S := tau, HOLD := NOT measured, NOW := sys_now_ms);
```

A caller that stopped calling `filt` while its input was out would leave `last`
behind, and the first sample after the outage would arrive with the whole outage
as `dt` - alpha goes to nearly 1 and the average SNAPS to the new reading
instead of rejoining where it left. Five minutes against a 30 s tau is alpha
0.91. So the block is called every scan and told there is nothing to fold.

IEC's implicit `EN` would do the same damage for the same reason: matiec's guard
returns before every statement, the clock update included.

### Calling one {#blk_call}

A call that would wrap is written as assignments to the block's inputs, then a
bare call; one that fits on a line keeps the argument form. The two are the
same to matiec - `__SET_VAR` is a token paste - so this is legibility, not
semantics. @see `docs/dev/st-migration.md`
