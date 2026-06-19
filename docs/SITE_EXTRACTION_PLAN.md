# Site Extraction Plan

Status: **done** (bar the deploy follow-up) - the gh_mlb41 site is extracted
(`070f55f`), the `BotanikFirmware<In, Out>` base + `GreenhouseMLB41` subclass are in
place (`049d01e`), the OPTA firmware is converged onto the same site (`8a99ec9`,
cross-built clean), and site selection via `-DBOTANIK_SITE` is wired (`d075ece`).
Remaining: the Arduino `.zip`/`.ino` deploy packaging (stage 4b below). This plan
stages the migration described in
the *Site-specific code* section of `CLAUDE.md`: promote `BotanikFirmware` into
`libbotanik` as a generic base, extract the `gh_mlb41` site out of `botanik_sim`
into its own unit, and converge the `botanik_fw` stub onto the same site. The
throughline is the CLAUDE.md rule: *generic, target-independent machinery lives in
`libbotanik`; one greenhouse's payload / pages / alarm table / shell commands /
data animator live in a site; targets are thin.*

This is a **pure refactor** - no behaviour change. The simulator is the canonical
`gh_mlb41` definition today, so it must keep building and rendering identically at
every checkpoint. The pyte goldens are the safety net: a byte change in a golden
during stages 1-2 means the move introduced a bug, not that the golden is stale.

**Sandbox caveat (applies to every checkpoint):** the pyte goldens are
environment-divergent in this sandbox (timing/locale). "Goldens unchanged" means
unchanged *relative to a baseline captured the same way on the same machine*
immediately before the stage - not necessarily matching the committed fixtures.
Capture a baseline run before starting, and diff against it after each stage.

---

## Open decisions (resolve before stage 2)

These are called out rather than guessed; stages 0-1 don't depend on them, but
stage 2 hard-codes them.

1. **Shared-memory injection.** The base binds the PLC region; its source differs
   per target (sim owns local statics, OPTA gets `HAL::get_plc_input()/output()`).
   **Proposed:** `BotanikFirmware` ctor takes `PlcInput<In>*` / `PlcOutput<Out>*`,
   the derived/harness passes the right source. (Alternative: a virtual getter -
   rejected, it complicates construction order for no gain.)
2. **Base location + name.** **Proposed:** `src/libbotanik/firmware.h`,
   `template<class In, class Out> class Botanik::BotanikFirmware`. Header-only
   (it's a template); site pages stay in their own `.cpp`.
3. **Site directory + selection.** **Proposed:** `src/sites/gh_mlb41/`, selected
   at configure time with `-DBOTANIK_SITE=gh_mlb41` (default `gh_mlb41`). The
   selection mechanism can land in stage 4; stages 1-3 can hard-code the path.
4. **Injection seam.** Base owns generic components + core pages and exposes a
   reference to each; `setup()` / `loop()` are virtual, the derived overrides and
   calls the base first (per CLAUDE.md composition rules). Settled - listed for
   completeness.

---

## Build wiring constraint (read first)

`CMakeLists.txt` globs `src/libbotanik/*.cpp` into the generic `botanik` static
lib (this feeds *both* targets, incl. the OPTA `botanik` archive). Site code must
**not** land in that glob. The site is compiled into the *application* target
instead:

- SIMULATOR: `botanik_sim` executable (currently `main.cpp` + `simulator.cpp`).
- OPTA: `botanik_fw` static lib (`firmware.cpp` + `main.cpp` + `botanik_arduino.cpp`).

So the site gets its own glob - `file(GLOB src/sites/${BOTANIK_SITE}/*.cpp)` -
added to whichever application target is being built. Sim-only site files
(`sim.cpp`) compile out on OPTA via their `#ifdef TARGET_SIMULATOR` guard, so the
same glob is safe for both. The base (`firmware.h`) is header-only and rides into
`libbotanik` with no source-list change; the `-Werror` `botanik_strict` mirror
only exercises it once a TU instantiates it (stage 2).

---

## Stage 0 - baseline  [x] done

Captured: SIMULATOR builds clean; the pyte suite fails 5 screens (home boot/delay/
reset, logs_marks, status_after_delay) and passes 12, purely from sandbox
divergence (`sched_setscheduler`/`mlockall` denied, volatile mock values drift).
That exact pass/fail split is the baseline stages 1-2 must preserve.

Capture the pre-refactor invariant so later stages have something to diff against.

- `cmake -B build -DBOTANIK_TARGET=SIMULATOR && make -C build` - clean.
- Run the pyte suite, save the rendered screens as the local baseline.
- `cmake -B build-opta -DBOTANIK_TARGET=OPTA && make -C build-opta` - confirm the
  stub OPTA build compiles *before* we touch it (so a later break is attributable).

No code change.

---

## Stage 1 - extract the site (move only, old `main()` still drives it)  [x] done

**Implemented (commit `070f55f`):** created `src/sites/gh_mlb41/` with `payload.h`
(was `simulator.h`), `pages.h` (the two Greenhouse pages + display helpers, moved
out of `main.cpp`), `alarms.{h,cpp}` (the `g_alarms[]` table, now extern), and
`sim.{h,cpp}` (was `simulator.cpp`, body guarded by `TARGET_SIMULATOR`).
`botanik_sim/main.cpp` is now a harness: it includes the site, owns the
process-lifecycle flags/handler, and drives the loop. CMake globs
`src/sites/gh_mlb41/*.cpp` into `botanik_sim`; `test_sizes.cpp` repoints to
`payload.h`. Builds clean incl. the `-Werror` strict gate; size tests pass; the
pyte render matches the stage-0 baseline (same 5 env-divergent failures, same 12
pass), confirming the move is render-neutral.

**Deviation from plan:** `version` and `alarms` were *not* moved into a site
`commands.cpp`. They're site-independent (Config/HAL and the generic AlarmModel),
so they became shell **built-ins** (`_cmd_version` / `_cmd_alarms`); `alarms` reads
the model via a new `AnsiTerminal::alarms()` accessor. This frees the single
`set_commands()` table slot for genuinely site-specific commands later. The site
has no commands of its own yet, so `commands.{h,cpp}` were dropped.

Original plan for reference:


Create `src/sites/gh_mlb41/` and move the site definitions out of `botanik_sim`
verbatim. The inline `main()` keeps wiring everything; only the `#include`s and
file locations change. This alone closes "the site is trapped in `botanik_sim`".

Moves (no logic edits):
- `gh_mlb41/payload.h` <- `PlcInput/OutputData_t` + the `UpsStatus` / `TrayStatus`
  / `NetStatus` enums (from `botanik_sim/simulator.h`).
- `gh_mlb41/pages.h` (+ `.cpp` if needed) <- `GreenhouseHomePage`,
  `GreenhouseStatusPage`, and the display helpers (`ups_str`, `tray_status_str`,
  `net_str`) currently file-static in `main.cpp`.
- `gh_mlb41/alarms.cpp` <- the `g_alarms[]` table.
- `gh_mlb41/commands.cpp` <- `SITE_COMMANDS[]` + the `cmd_*` callbacks.
- `gh_mlb41/sim.cpp` (whole body under `#ifdef TARGET_SIMULATOR`) <-
  `fill_greenhouse_output` + `start_greenhouse_emulator` + `plc_dilated_timeout`
  (from `botanik_sim/simulator.cpp`). This is the file the eventual zip excludes
  by simply not defining `TARGET_SIMULATOR`.

Wiring:
- CMake: add `file(GLOB GH_SOURCES src/sites/gh_mlb41/*.cpp)` to the `botanik_sim`
  target's sources (replacing `simulator.cpp`).
- `botanik_sim/main.cpp`: include `sites/gh_mlb41/...` instead of
  `botanik_sim/simulator.h`. Everything else unchanged.
- Delete `botanik_sim/simulator.h` / `.cpp` once empty.

**Checkpoint:** SIMULATOR builds clean; pyte goldens unchanged vs the stage-0
baseline (pure move). OPTA untouched.

---

## Stage 2 - promote `BotanikFirmware` + introduce `GreenhouseMLB41`  [x] done

**Implemented (commit `049d01e`):** `libbotanik/firmware.h` holds the generic
`BotanikFirmware<In, Out>` base (components + accessors + the per-tick `loop()`);
`GreenhouseMLB41` derives it and overrides `setup()` to chain the base then splice
its pages in. `botanik_sim/main.cpp` is a thin harness. Builds clean (sim +
`-Werror` strict gate); size tests pass.

**Decisions resolved:** shared-mem is ctor-injected; the base lives at
`libbotanik/firmware.h` (`Botanik::BotanikFirmware`); per-target timing (sim
dilation, HW-watchdog period) is applied by the harness via accessors +
`set_hw_watchdog_ms()` before `setup()`.

**Deviation from plan - page registration:** instead of the originally-sketched
`register_pages()`/`register_sinks()` hooks, the site overrides `setup()` (chains
the base) and splices its pages with a new `AnsiTerminal::add_page(page, index)`
(insert + shift). Because `switch_to(0)` only *records* the boot index until the
first render, inserting the home page at index 0 in the site's `setup()` makes it
the boot page - so the site owns page order without re-listing the core pages.

**Note - release pyte not yet re-verified clean.** Verified via standalone render
(boot screen, correct tab order) + the debug build; a trustworthy lean-release
pyte pass is still owed once the concurrent terminal/alarms work settles (the
sub-build was polluted by another agent's stray stdout debug prints during this
work). The base `loop()` also carries coordinated CPU-health local alarms added
by that concurrent work.

Original plan for reference:


The load-bearing stage. Extract the generic wiring from `main()` into the base,
and the site wiring into a derived class. Behaviour is preserved by construction -
both halves come straight out of the working `main()`, and the per-tick order must
match exactly (the goldens enforce it).

- `src/libbotanik/firmware.h` - `template<class In, class Out> class
  BotanikFirmware`:
  - Owns the generic components currently built inline: logger, monitor, scheduler,
    terminal, `PlcSyncHandler`, `PlcLogReader`, `AlarmReader`, `AlarmModel`,
    notifier, and the core pages (`ShellPage`, `LogsPage`). Exposes a reference
    accessor for each.
  - Ctor takes the shared-mem pointers (decision 1) + the alarm table
    (`AlarmCondition*`, count) the site supplies.
  - `virtual void setup()` - registers core pages + log sinks, selects the
    terminal type, shows the boot banner, arms the watchdog (the boot half of
    `main()`).
  - `virtual void loop()` - the exact per-tick order from the current loop:
    `tick -> receive -> plc_handler -> plc_log_reader -> alarm_reader/model ->
    logger -> terminal -> monitor.drain -> transmit -> watchdog_kick`.
- `src/sites/gh_mlb41/greenhouse.h` (+ `.cpp`) - `class GreenhouseMLB41 : public
  BotanikFirmware<PlcInputData_t, PlcOutputData_t>`:
  - Declares the site pages (`GreenhouseHomePage`, `GreenhouseStatusPage`,
    `AlarmsPage`) as members, built from the base's exposed refs (terminal, PLC
    `output_data()`, alarm model).
  - `setup()` override: `BotanikFirmware::setup()` first, then register the site
    pages, `set_commands(SITE_COMMANDS, ...)`, and any site log sinks.
  - `loop()` override: `BotanikFirmware::loop()` first, then site-local steps if
    any (today none beyond what the base covers; SYS_RST local-alarm set can move
    into the base or stay site - keep where it reads cleanest).
- `botanik_sim/main.cpp` shrinks to a harness: HAL/console setup, signal handlers,
  allocate the `shared_in/out` statics, construct `GreenhouseMLB41` against them,
  `start_greenhouse_emulator(...)` (sim-only), `gh.setup()`, then
  `while (botanik_running) gh.loop();`, then `HAL::shutdown(...)`.

**Checkpoint:** SIMULATOR builds clean; `make check` (the `-Werror`
`botanik_strict` mirror) clean now that the template is instantiated; pyte goldens
unchanged vs baseline. If a golden moves here, the per-tick order or a page
registration diverged - fix the move, don't re-bless the golden.

---

## Stage 3 - converge `botanik_fw` onto the site  [x] done

**Implemented (commit `8a99ec9`):** dropped the `botanik_fw` stub (its concrete
`BotanikFirmware`, 2-field payload, stub HomePage). The OPTA firmware is now
`GreenhouseMLB41` bound to `HAL::get_plc_input()/get_plc_output()`, exposed as a
function-local-static `Botanik::Firmware::instance()`; `main()` and the
`botanik_setup()`/`botanik_step()` Axel hooks drive `setup()`/`loop()`/`run()`.
CMake globs `src/sites/gh_mlb41/*.cpp` into `botanik_fw`; `sim.cpp` drops out via
its `TARGET_SIMULATOR` guard, so only `alarms.cpp` carries through.

**Cross-build verified** with `arm-none-eabi` + the installed `mbed_opta` core
(`cmake -B build-opta -DBOTANIK_TARGET=OPTA && make botanik_fw`) - compiles clean.
The one OPTA `ShellPage` footprint that had drifted (others' layout work; never
caught because OPTA isn't built in the sim workflow) was refreshed 18140 -> 18144.
Runtime/flash still needs real hardware (can't run here), per the OPTA caveat.

**Layout `static_assert`s:** the per-site `sizeof/alignof` check against the PLC
IDE's generated `PLCSharedVars*_t` is noted in `botanik_fw/firmware.cpp` as the
integrator's step once those generated types are in hand; not added here (the types
aren't available in this tree).

Original plan for reference:


Replace the OPTA stub with the real site. The `botanik_fw/firmware.h` placeholder
payload + stub `HomePage` + concrete `BotanikFirmware` all disappear.

- OPTA entry (`botanik_fw/firmware.cpp` / `main.cpp`): construct a
  `GreenhouseMLB41` bound to `HAL::get_plc_input()/get_plc_output()`, call
  `setup()` then `loop()` forever. Drop the placeholder `PlcOutputData_t` and the
  stub page.
- CMake (OPTA branch): add the same `src/sites/gh_mlb41/*.cpp` glob to
  `botanik_fw`. `sim.cpp` compiles out (no `TARGET_SIMULATOR`).
- Add the layout-match `static_assert`s noted in `plcio/vars.h` / the old
  `simulator.h` header against the PLC IDE's generated types (or leave a marked
  TODO until the generated types land).

**Checkpoint:** OPTA cross-build compiles (`make -C build-opta`). Runtime/flash
verification is out of scope in this environment - note it as hardware-pending,
matching the `HAL_PLATFORM_PLAN.md` OPTA caveat. Refresh `libbotanik_sizes.h` if
the footprint record shifts.

---

## Stage 4a - site selection  [x] done

**Implemented (commit `d075ece`):** `set(BOTANIK_SITE gh_mlb41 CACHE STRING ...)`
drives `file(GLOB src/sites/${BOTANIK_SITE}/*.cpp)` for both the simulator and OPTA
targets, and `BOTANIK_SITE_HEADER` (= `sites/${BOTANIK_SITE}/site.h`) is passed to
the harness / firmware / `test_sizes` as a compile define so they `#include
BOTANIK_SITE_HEADER` instead of naming the site. Each site exposes a uniform surface
through `site.h`: `BotanikSite` (the firmware type), the payload, and the sim-only
mock-PLC entry (`start_greenhouse_emulator` -> `start_site_emulator`). Defaults to
`gh_mlb41`; both targets build clean.

## Stage 4b - Arduino deploy packaging  [ ] (deferred)

The deliverable for the OPTA path: the Arduino `.zip` (currently `libbotanik` only)
should also carry the selected `src/sites/${BOTANIK_SITE}/`, and the integrator's
entry file (`#include <libbotanik.h>` + the site, defining the `botanik_setup()`/
`botanik_step()` the IDE calls) needs generating. `src/botanik_fw/` is the working
reference for that entry. Pending: confirmation of the Axel IDE's exact hook names
(see the note in `botanik_fw/botanik_arduino.h`) and the PLC IDE's generated
`PLCSharedVars*_t` types (for the layout `static_assert`s). A build-project concern,
not an in-tree refactor.

---

## Suggested order & overall validation

1. **Stage 0** - baseline (build both targets, snapshot goldens).
2. **Stage 1** - pure move; the cheapest win, fully golden-guarded.
3. **Stage 2** - base + derived; the real work. Build + `make check` + goldens.
4. **Stage 3** - OPTA convergence; cross-build only here.
5. **Stage 4** - selection/deploy, when a second site or the deploy step is needed.

After every stage: `make -C build` (SIMULATOR) and diff the pyte render against
the stage-0 baseline. Stages 1-2 must be byte-identical; a difference is a
regression in the move. OPTA edits (stage 3) are compile-reasoned only here and
need real-hardware confirmation before they ship.
