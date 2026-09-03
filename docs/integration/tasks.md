# Control programs {#int_tasks}

Control logic lives in the **scan cycle**: a table of programs, each with a
declared rate, run in PHASE_CONTROL between the input refresh and the output
write. There is no driver stepping them - **a program registers itself with the
task registry** like everything else in the beat, so it gets its own rate, its
own row on the TASKS page and its own cost sample.

A program is one **Structured Text** file, compiled by matiec into the firmware
image. It has no C++ side at all: no class, no header, no registration. Its
interface is the rows it names, and its law is the body.

```
PROGRAM <name>
VAR_EXTERNAL CONSTANT   (* rows it READS *)   END_VAR
VAR_EXTERNAL            (* rows it WRITES *)  END_VAR
VAR                     (* state, kept between scans *)     END_VAR
VAR_TEMP                (* scratch, this activation only *) END_VAR
  ...the law...
END_PROGRAM
```

There is no second hook for reporting. A status value or an alarm derived from
what the law just decided is written by the same body, after the decision -
there used to be two, and with a program either running or not running they were
called back to back every beat, which is one body with a convention about where
to put things.

A program that is not running is not called, and what it drove drops to its
declared safe state, so there is nothing for it to do on the way down. On the
way back up it starts from its first activation, and every block it holds seeds
on that call - which is what a first-scan flag used to be for.

## Declaring one

A program is one ST file, `programs/<stage>/<name>/<name>.st`, and one row in
`tables/programs.csv` naming it, its instance, its source and its rate. Nothing
else: there is no class, no header, and no registration to write.

```
(* strat_sensor program
   ------------------------------------------------------------------------ *)

PROGRAM strat_sensor
VAR_EXTERNAL CONSTANT
  cli_temp_hi     : REAL;
  cli_temp_hi_qlt : QUALITY;
  cli_temp_lo     : REAL;
  cli_temp_lo_qlt : QUALITY;
END_VAR
VAR_EXTERNAL
  cli_dTStrat_degC     : REAL;
  cli_dTStrat_degC_qlt : QUALITY;
END_VAR
VAR
  smooth : EWMA;          (* state, carried between scans *)
END_VAR
VAR_TEMP
  measured : BOOL;        (* this activation only *)
END_VAR
  measured := GOOD(cli_temp_hi_qlt) AND GOOD(cli_temp_lo_qlt);
  IF measured THEN
    cli_dTStrat_degC := ABS(cli_temp_hi - cli_temp_lo);
  ELSE
    cli_dTStrat_degC := cli_dTStratLow_degC;
  END_IF;
  (* the WORSE of the two, so the reason travels and not just the verdict *)
  cli_dTStrat_degC_qlt := WORST_OF(cli_temp_hi_qlt, cli_temp_lo_qlt);
END_PROGRAM
```

**Which block a row goes in is the declaration of intent, and it is checked.**
`VAR_EXTERNAL CONSTANT` is read; plain `VAR_EXTERNAL` is written, and says this
program is the row's author. `scripts/tables.py plant` reads what matiec says
each body actually writes and fails the build on a row declared writable that
the body never writes, or an output, alarm or status row that no program writes
at all. Two programs writing one row is reported rather than refused - shared
read-modify-write state is the design.

Rows are bound by NAME, case-folded, once at bring-up. A name matching no row
lands on the inert sink, which reads 0 for ever - so the program is INVALID and
never runs, said once as `LOG_CRITICAL` with its name. @see @ref dev_scan

There is no way to declare an ARRAY of rows: a row is named, not indexed. Eight
trays means eight names, or a FUNCTION_BLOCK instanced eight times with the
arithmetic written once - which is what `TRAY_DUE` is.

## Registering one

Nowhere, by hand. The row in `tables/programs.csv` is the whole of it:
`scripts/tables.py plant` generates the roster, the trampolines and the registry
entry, and `Plc::Runtime::Plant` registers one task per program at PHASE_CONTROL
with the rate the table gave it.

## What a program gets

No argument, and no context struct. A PROGRAM POU takes nothing, so everything a
law needs is a row it names:

| | |
|---|---|
| `sys_now_ms` | the scan clock, ms since boot. Blocks difference it themselves |
| `sys_today` | days since 1 Jan 1970; **0 before the clock is set**, which a schedule refuses to run against |
| a `_qlt` companion | the row beside it is believable, or why not. `GOOD(q)` to branch on one, `WORST_OF(a, b)` to inherit two, `QUAL(ok)` to make one from a boolean |
| `prog_<name>_park` | this program has left the scan and should travel to its safe state. Only for a program that opted in with `park`; it answers on `prog_<name>_parked` |

**There is no `dt_ms` and no `first_scan`.** A block that needs elapsed time
takes `NOW` and keeps its own last reading, which is what makes it
rate-independent - the same block works on any task, and a scan it missed costs
it nothing. A block that needs "have I run before" keeps its own `seeded`, and
seeds on its first call.

Two guarantees the scaffolding still provides:

**Coherency.** Inputs refresh once per beat, before any program runs, so every
program in a beat sees the same instant. Programs run sequentially in one
thread and never span a beat.

**Lateness.** A program that comes due late runs **once** - missed activations
are dropped, not caught up, because a catch-up burst arrives exactly when the
system is already behind. Since a block differences `sys_now_ms` itself, the
elapsed time it sees is the ACTUAL one either way.

## Writing one

Everything above the body is declaration; the body is the law, top to bottom,
once per activation. What a program may reach for:

- the **library POUs** in `plc/functions/*.st` and `plc/blocks/*.st` - the
  ladders, the timers, `EWMA`, `PID`, `ACTUATOR`, the meters
- the **site's own blocks**, `blocks/<name>/<name>.st`, for a shape only this
  greenhouse has
- `st/lib/ieclib.txt` for the standard functions - `LIMIT`, `MIN`, `MAX`, `ABS`,
  `POW`, and the conversions. **These are ours, not matiec's**, and REAL-only:
  ST has no overloading, so a UDINT comparison is written out by hand

**Outputs are always written**, every beat, whatever the inputs did. Skipping
the write leaves a stale number that still reads as current, which is worse than
a fresh one computed from doubtful inputs. The doubt goes in the `_qlt` COMPANION
the law writes beside the value - and if the program opted in, the build refuses
a row whose companion it leaves unwritten.

## The POU library

Two kinds of piece, kept apart the way IEC 61131-3 does:

| POU | Here | Has |
|---|---|---|
| FUNCTION | `plc/functions/*.st` | nothing. Same inputs, same output, always |
| FUNCTION_BLOCK | `plc/blocks/*.st`, and the site's `blocks/` | instance state, carried between scans |
| PROGRAM | the site's `programs/` | blocks, and the field |

The line is mechanical rather than a judgement call: **if it has to remember
something it is a block.** `MEDIAN3` and `AVG2` are functions; `EWMA` and
`TOF_MS` are blocks because they carry a clock.

A block that reads the clock takes `NOW` and a `HOLD` input, and **must be
called every scan** - one skipped for the length of an outage snaps to the first
reading back instead of rejoining where it left. @see @ref blk_hold

## When programs run

Between the input refresh and the output write, at the rate the table gave them,
and **only while `Plc::Runtime::Plant::running()`** - which is the operator's
standing choice and the system's hold, stricter wins. @see @ref dev_scan

A program that is not running is not activated at all, and what it drove drops
to its declared safe state rather than freezing where the law left it. There is
no finer state: no parking, no inhibited-but-still-reporting. A law that must
not move an actuator under some condition tests that condition itself.
