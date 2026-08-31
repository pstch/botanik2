# Handoff: the ST migration

Live working document. Written 2026-08-31 to hand this work to a fresh
session. Everything below is either done and verified, or a decision already
taken with its reasoning, or a task not yet started.

---

## What this is

`botanik`'s control programs are being rewritten as IEC 61131-3 **Structured
Text**, compiled to C by **matiec**, and run against the existing `Vars::Bank`.
The end state the user asked for:

> `src/sites/<NAME>/` contains only CSV tables, ST programs, and a root TOML.

**14 of 29 programs are ported. 15 remain.**

---

## The task list

### 1. Finish the port - 15 programs

**Blocks were never the hard part.** A survey of all fifteen (2026-08-31)
found five things ST has no expression for at all, and an ordering law that
decides what may move when. Read both before picking the next program.

| program | blocks needed | what ST cannot say |
|---|---|---|
| `WxSensors` | `WeatherProbes` (site) | - |
| `AcGridWatch` | `AcSupply` (site) | `.valid()`/`.invalidate()` on 4 mirrors |
| `AcLocalWatch` | `AcSupply` (site), `Ton` | same |
| `DcWatch` | `Deadband`, `Ewma`, `Ton` | `.valid()`/`.invalidate()` |
| `CoolingController` | `Pid` | - |
| `ScreenController` | `Actuator` + `ActuatorConfig` | - |
| `WindowsController` | `Actuator` + `ActuatorConfig` | `.invalidate()` |
| `SkirtController` | `ValveController` (site) | descriptor read (fail direction) |
| `IrrValves` | **none** - it only calls `.set()` | - |
| `IrrigationModulation` | `Ton` | `.invalidate()`, `powf` |
| `IrrMtrInput` | `Derivative`, `Ton` | **`HAL::Pulse::sample()`** |
| `IrrMtrOutput` | `Deadband`, `Ewma`, `Integral` | `.held()`, `.hold()` |
| `IrrigationSchedule` | none | **`HAL::Clock::wall_ms()`**, **`.edit()`**, `.hold()` |
| `IrrigationCycle` | `Ton` | **`.edit()`**, `.held()` |
| `SunWatch` | none | **`_sun->*`**, **`wall_ms()`** |

### The five missing mechanisms

1. **`.edit()` - a retained parameter.** `irr_last_tray1..8` are
   `retained=true`, and `Ref::edit()` exists because "`set()` writes the value
   and leaves flash owing nothing". `__SET_EXTERNAL` is `Ref::set(v, true)`.
   Porting the schedule or the cycle today makes the plant FORGET every tray's
   watering date on reboot. Needs an accessor path, or a row flag saying a
   drive to this row is an edit.
2. **The wall clock.** `HAL::Clock::wall_ms()`, for the UNIX day the schedule
   counts against. Wants a row, the shape `sys_now_ms` already has.
3. **Pulse counts.** `HAL::Pulse::sample()`. The firmware attaches the
   terminals already; the counts want rows.
4. **The sun tracker.** `_sun->elevation/ready/sunrise_ms/sunset_ms`. Known
   since the handoff was written; `sol_ready` was the first row of the answer.
5. **A descriptor read.** `ValveController::bind()` learns its fail direction
   from the coil row's declared default, so "the table stays the one place it
   is declared". ST cannot read a descriptor. Passing FAIL_OPEN in as an input
   works and declares it twice, which is what the block was written to avoid.

`.valid()`/`.invalidate()`/`.hold()`/`.held()` are all expressible, but each
costs COMPANION ROWS - the AC pair alone wants about sixteen, and a register is
a Modbus contract that does not move. Budget them before starting a program.

### The ordering law, learned the hard way

**All ST runs before all C++** (`site.h` registers the resource first), so a
program ported while its PRODUCER is still C++ reads inputs one beat old.
That is fine for a wind interlock - 100 ms, and `site.h` says so. It is not
fine for `IrrValves`, which opens the fill valve on `irr_state`: ported ahead
of `IrrigationCycle` it lagged the state machine by a beat and broke five
water scenarios. It was reverted rather than absorbed into the tests.

**So port UPSTREAM first.** The safe order for the unblocked set is
`WxSensors`, then the `Ac*` pair, `DcWatch`, `CoolingController`, and only
then `ScreenController`/`WindowsController`/`SkirtController`, which read
`cli_coolDem`. The whole irrigation chain goes last, and `IrrValves` last of
all - it is the only consumer at the end of it.

Library blocks still to write as ST (in `src/libbotanik/plc/blocks/*.st`):
`Ton`, `Pid`, `Deadband`, `Integral`, `Derivative`, `Actuator`.
Site blocks: `WeatherProbes`, `AcSupply`, `ValveController`.

`Plant` holds `Slot _slot[16]` and asserts `PROGRAM_COUNT` against it at
bring-up. 14 are in use. The 17th port has to widen it first (32 is the
obvious number: 29 programs plus room).

**Do NOT use matiec's own blocks.** See "Licensing" below.

### 2. Then: the base firmware owns the `Plant`

`Plc::Runtime::programs[]` and `PROGRAM_COUNT` are already `extern` symbols
that the site's generated `plant.cpp` defines, so `BotanikFirmware` can own the
component and reference them by link. That also makes the "ST registers before
the C++ programs" ordering automatic instead of a comment in `site.h`, because
`_register_tasks()` runs before the site's `on_setup()`.

### 3. Then: `SiteDescriptor` carries the row tables

So `register_vars()` stops being a site hook. After 2 and 3, `site.h` is a
descriptor plus an empty subclass - generate it, or move the descriptor into
`generated/` and delete it.

---

## How to work

### Rebuild matiec (it lives in a scratchpad and will be gone)

Not vendored, GPLv3, built from source without sudo:

```
apt-get download flex bison libfl-dev   # then dpkg-deb -x each into a prefix
git clone https://github.com/nucleron/matiec   # or the beremiz mirror
autoreconf -i && ./configure && make
```

Then point the generator at it: `IEC2C=/path/to/iec2c`.

### Regenerate

```
IEC2C=... python3 scripts/tables.py plant      # plant.st + plant.cpp, then iec2c
python3 scripts/tables.py generate             # vars.gen.{h,cpp}
python3 scripts/tables.py config               # config.gen.h
python3 scripts/tables.py check                # CSVs vs a hand-written vars.cpp
```

`scripts/tables.py` is ~430 lines and is the only generator. Its output is
COMMITTED and it is never a build step.

### Verify - all four, every time

```
make -C build botanik_tests && ./build/tests/botanik_tests   # 236/236
make -C build                                                # sim, expect 0 warnings
ctest --test-dir build -R pyte                               # 7/7
make -C build-opta                                           # exit 0
```

Footprints are pinned in `src/libbotanik_sizes.h` for BOTH targets. When one
moves, measure rather than guess: compile a probe with `-DBOTANIK_PIN_SIZES=0`
(host), and for OPTA a `template <unsigned long N> struct Probe;` instantiated
on the size, reading the number out of the error message.

### The 5 expected test failures are gone

They asserted the OLD quality mechanism, and all five were fixed with the
`temp_rh_zone` port. Four of them keyed on a row that program writes, so its
move to ST is what forced them. What each needed:

- three of them clear the `_qlt` COMPANION where they used to invalidate the
  row. `cli_temp_in` and `cli_dpMargin_degC` are written every scan now.
- `cooling: the floor carries it` moved to `CoolingRig` and drives the PROBES.
  Whether one level can carry the house is temp_rh_zone's law, and a rig that
  does not run it can only force the answer.
- `cooling: an unknown demand` needed two things that were never about quality:
  the screen only deploys in the CLOSING half of the day (`_allows`), so the
  case is set in `PHASE_DUSK_BREEZE`; and a margin of 20 degC is past
  `cli_humidStart_degC`, which asks the skirt to humidify - a demand of its own
  in a case about the ones that are missing. The margin is now in the dead zone.
- `irrigation: a modulation nobody can read` was not a temp_rh_zone case at all.
  `Live::believe_all()` sets every companion TRUE at construction, so a case
  rehearsing an unreadable row has to clear the one it means: `irr_mod_qlt`.

---

## The architecture, and why

### The site

```
src/sites/gh_mlb41/
  site.toml            identity, tuning, feature flags
  tables/*.csv         20 tables: rows by scope, enums, devices, programs
  programs/**/*.st     the laws
  blocks/**/*.st       site blocks
  generated/           vars.gen.{h,cpp}, config.gen.h, plant.st, plant.cpp,
                       and matiec's own output (POUS.*, plc.c, gh_mlb41.*)
  config.h  vars.h     thin shims over the generated headers
  site.h  sim.h  programs.h   the remaining real C++
```

### The runtime - `Botanik::Plc::Runtime`, in `src/libbotanik/plc/runtime/`

| file | what |
|---|---|
| `accessor.h` | matiec's macros redefined onto `Vars::Bank`. THE adapter |
| `iec_std_lib.h` | the types matiec's C names, plus `__IL_DEFVAR_T` |
| `resolve.h` | case-insensitive name lookup (IEC upper-cases everything) |
| `bank.h` | the `Vars::Bank*` every generated read names |
| `program.h` | the `Program` record: name, period, init, run, task |
| `runtime.{h,cpp}` | `Plant`, a `Component`: one registry task per program |

`st/lib/ieclib.txt` is OUR IEC library. `st/shim/` holds two harnesses.

### How a program runs

matiec generates `X_init__` (the binding) and `X_body__` (the law), both
`static` in `POUS.c`. The site's generated `plant.cpp` includes that TU, so it
can see them, and expands the roster three times: trampolines, indices, table.
`Plant` then registers **one `Tasks::Registry` entry per program**.

We deliberately do NOT call matiec's `config_run__`. Its RESOURCE would
dispatch the programs as one fixed sequence with no hook in it. Calling each
body ourselves is what keeps a program a program:

- its own period, from `programs.csv`
- its own entry in the TASKS table, with cost and missed counts
- its own `prog_<name>` health row
- its own writer slot, which restores the double-claim check
- its own INVALID when a name it binds resolves to nothing

**No program is bound to a TASK** in `plant.st`. One `TASK unbound` remains
because matiec refuses a RESOURCE without a periodic task; nothing reads it.

---

## Decisions already taken - do not relitigate

### Licensing: none of matiec's own code reaches the firmware

matiec always loads `<-I dir>/ieclib.txt`, and `-I` is ours to point. Ours is
`st/lib/ieclib.txt`, so matiec's 28 standard FUNCTION_BLOCKs are never
declared: not `TON`, not `HYSTERESIS`, not `PID`. Those names are therefore
free, and our blocks may take any name.

This matters beyond names:

- Their `TON`'s `VAR` carries `STATE`, `PREV_IN`, `CURRENT_TIME`, `START_TIME`,
  none of which IEC mentions. That is matiec's design, and its body is in an
  LGPLv3 header.
- Their `IEC_TIME` is a packed 12-byte timespec, putting `TON_data__` at 52
  bytes against our `TOF_MS` at 19.

The standard FUNCTIONS we call are ST, with our bodies, in `st/lib/ieclib.txt`.
Add one when a POU starts needing it: the error is "function invocation ... not
declared", at compile time. `EXP` went in for `SAT_VAPOUR_PRESSURE`.
A declaration alone is not enough - matiec special-cases its own standard names
and generates nothing, but ours are ordinary user functions and it emits a
body, empty if we do not write one.

### Quality: `_qlt` companion rows

`Ref::get()` substitutes a row's DECLARED value when it is not believable, so
most `.valid()` reads were computing a number substitution would have given
anyway. 49 reads became 22, with 11 companions.

**ST cannot write a quality.** `__SET_EXTERNAL` has nowhere to put one. So an
ST-written row is ALWAYS valid and the companion is the only signal, and
substitution stops applying to it - a program that used to publish an invalid
row must now publish the safe VALUE explicitly (see `strat_sensor`, which
publishes the low rung).

Where quality still legitimately lives:

1. inside a conditioning block, on a row the TRANSPORT fills
2. `Ref::held()` on a totaliser
3. a producer deriving its own output's quality

`Vars::QualityMirror` (a `Component` at `PHASE_INPUT`, after the bank's expiry
sweep) copies a FIELD row's validity into its `X_qlt` companion. Only rows a
transport fills - a derived row's companion is a program's verdict.

### The clock, not a delta

`sys_now_ms` is a row. Blocks keep their own `last` and difference it. A C++
law is handed `dt` through `ProgramContext`; ST has no argument to be handed
anything. This is rate-independent and survives any scheduling change.

**Accelerated time still works**: `ctl_advance_one_beat()` adds one
`TASK_PERIOD_MS` of virtual time and `BOTANIK_SIM_SPEED` only paces how often
that happens, so a block differencing the virtual clock reads virtual ms.

### Task groups are gone

A task states its period in ms. `Tasks::EVERY_BEAT` spells the common case.
Each component names its own rate in its own concern header.

---

## matiec gotchas, learned the hard way

- `DT` is `DATE_AND_TIME`, reserved. So is `ON`. Cost two debugging rounds.
- Redefining a name its library declares is an **internal compiler error**, not
  a diagnostic: `Internal compiler error in file ../util/symtable.cc`.
- A FUNCTION's locals are `VAR`, never `VAR_TEMP`.
- `MAX`/`MIN` are VARIADIC: matiec passes the argument count first.
- A `{ ... }` verbatim block in a FUNCTION puts matiec in IL context, which
  needs `__IL_DEFVAR_T` declared. Used for the four type conversions, since ST
  cannot change a value's type without a conversion function.
- A zero-argument FUNCTION does not parse, even with `-i`. An enumerated `TYPE`
  with explicit values does not parse. So **ST writes enum codes as literals** -
  the generator puts them in `plant.st` as a reference block.
- The formal-parameter connection list (`PROGRAM p : law(x := row)`) emits a
  copy in and out per scan, bypasses the accessor macros, and **does not compile
  against matiec's own runtime**. Use `VAR_EXTERNAL`.
- matiec upper-cases every identifier and the original case survives nowhere,
  including `VARIABLES.csv`. Hence `resolve.h` folds case. All 425 site rows
  stay unique under folding; a test should keep that true.

---

## Test patterns, which are most of the work per port

Porting a program is a small ST file and a larger test migration. The rules:

- **A row an ST program writes cannot be `set()` by a case** - the next scan
  overwrites it. `force()` in `tests/program_rig.h` uses `override_set`, which
  SHADOWS the live value and survives. It also forces the `_qlt` companion,
  because forcing a reading without saying it is believable leaves a law
  reading the number and declining to act.
- **A case about how a law RESPONDS to an input forces the input; a case about
  how that input is PRODUCED drives the chain.** Over-applying this broke the
  screen and skirt tests once: `cli_coolDem` is still written by C++.
- `field()` publishes a field row and its companion. `put_phase()` drives the
  SUN, because `day_phase` owns `cli_phase` and both bank arms.
- Rigs run `mirror.scan()` then the ST then the C++ programs - `Plant::scan()`,
  and `Plant::wire()` at construction. `StRig` seeds all probes alike so
  destrat asks for nothing.
- Driving through the real chain means EWMA settling: `cli_sensorTau_ms` is
  30 s, so a settled value needs ~2000 beats at 100 ms.
- **Never assert exactly ON a filter's boundary.** Settling from 8.0 to
  "exactly 2.0" lands on 2.007, and a rung releasing at `<= 2.0` then holds.
- **`Live::believe_all()` sets every `_qlt` companion TRUE at construction**,
  because most companions belong to a program the rig is not running. A case
  rehearsing an unreadable row therefore has to clear the companion it means -
  invalidating the ROW does nothing to a law that reads the companion. This is
  what the last of the five deferred failures turned out to be.
- **A rig's driver order is the firmware's**: companions, then `Plant::scan()`,
  then the C++ cycle. `CoolingRig` had it the other way round, which was fine
  while nothing in the ST fed the ladder; `temp_rh_zone` does.

---

## Things found along the way, still open

- **The destratification hysteresis was unreachable.** `cli_dTStratLow_degC`
  minus `cli_dTStratHyst_degC`, both 2.0, put rung 1's release edge at a gap of
  0.0 - and the gap is an ABSOLUTE difference. The fans held speed 1 for the
  life of the run. Fixed by halving the hysteresis to 1.0 (user's call).
- **`.gitignore` had a bare `generated/`** from the retired generator, which
  silently hid every file matiec produces. Anchored to `/generated/`. Worth
  remembering that a "generate offline, commit the output" contract can be
  quietly not holding.
- The `docs/reference/points.md` counts and several `docs/dev/` files are stale
  after all of this. Not touched.
- `st/` at the repo root still holds a `.plant.st.swp` from the user's editor.
  The live file is `generated/plant.st`.

---

## A correction worth carrying

I claimed a live out-of-bounds read in `vars.cpp` (`ROWS_COUNT` 307 against 279
rows) and was **wrong**. My CSV parser was missing four factories - `RO`, `PE`,
`PS`, `G` - and 28 relay outputs use `RO`. The round-trip check compared parser
output to parser output, so it confirmed its own blind spot.

**A round trip between two runs of the same code proves nothing about what that
code cannot see.** The build and the suite are the real check.
