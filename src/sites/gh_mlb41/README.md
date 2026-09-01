# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse.
`libbotanik` stays generic; this directory is the single source of truth for the
`gh_mlb41` greenhouse.

| Path | Origin | Holds |
|---|---|---|
| `site.toml` | **declared** | identity, tuning limits, feature flags |
| `tables/*.csv` | **declared** | every variable the site has, one file per row factory, plus the enums, the field devices and the program roster |
| `programs/<stage>/<class>/<class>.st` | **declared** | one control law, as IEC 61131-3 Structured Text |
| `blocks/<name>/<name>.st` | **declared** | a function block of the site's own, nameable from any of its programs |
| `generated/vars.gen.{h,cpp}` | generated | ONE descriptor table, the calibration blocks, the enums, the device map, the home screen, the build guards and the `g_site` `SiteDescriptor` |
| `generated/config.gen.h` | generated | `site.toml` as `#define BOTANIK_*`, and the assertion that this build turns on what the table was cut for. CMake reads it as the site config: there is no shim over it |
| `generated/plant.{st,cpp}` | generated | the CONFIGURATION matiec compiles, and the roster `Plc::Runtime::Plant` runs |
| `generated/POUS.*`, `plc.c`, `gh_mlb41.*` | generated | matiec's own output |
| `tables/home.csv` | **declared** | the home-screen captions - the one table no rule derives |
| `tables/pulse_in.csv` | **declared** | the edge-counted terminals, as ordinary rows |
| `site.h` | hand-written | includes the generated tables and `sim.h`. No class, no hook, no override |
| `sim.h` | hand-written | the simulated field: it writes the variable tables the way a Modbus driver will. Its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build |

Regenerate with `scripts/tables.py` - `generate`, `config`, `points` (which
writes `docs/reference/points.md`) and `plant` (which needs `IEC2C=` pointing
at a built matiec). The output is COMMITTED and is never a build step.

## The control laws

All 28 are **Structured Text**, compiled to C by matiec and run against the
same `Vars::Bank` the rest of the firmware reads. `tables/programs.csv` is the
roster - name, instance, source, rate - and it decides the order they run in.
`BotanikFirmware` owns the `Plc::Runtime::Plant` that dispatches them, one
`Tasks::Registry` entry each, so a law keeps its own period, its own
`prog_<name>` health row and its own writer slot.

A program reads and writes rows by NAME, through `VAR_EXTERNAL`. Nothing is
passed between them: `docs/dev/st-migration.md` has the migration's own
lessons, and `docs/dev/scan.md` the scan model.

There is no site subclass. `Site::firmware()` is a generated accessor over a
function-local static `BotanikFirmware`, bound to `g_site` - function-local
because on the OPTA the object must be constructed after the Arduino core's
`init()` has brought up the UARTs, and a file-scope one would run before it.
A site needing an extra page or a component of its own still derives
`BotanikFirmware` and overrides `register_pages()` / `on_setup()`; see
`src/libbotanik/firmware/firmware.h` for that extension model.

## The field bus

```
 * Bus: RS485, ModbusRTU master, 38400 baud, none/8/1, on COM2.
 * (BOTANIK_MODBUS_BAUD is pinned to match, in this site's config.h.)
 * 20 field devices, 102 mapped signals, 13 free channels. (The published map
 * lists 70, and omits MTR-2 object 2, active power, which the device answers.)
 *
 *   SRR/TAC/MTR-1  Eastron SDM630    addr 5   37 readings
 *   SRR/TAC/MTR-2  Lettel MCT1-140   addr 6    6 readings
 *   SRR/TDC/AI-1   Waveshare 25767   addr 11   5 readings (+3 free)
 *   SRR/TDC/AO-1   Waveshare 26211   addr 16   2 drives   (+6 free)
 *   SRR/TDC/RO-1   Waveshare 26517   addr 31   6 coils (+2 free)
 *   SRR/TDC/RO-2   Waveshare 26517   addr 32   6 coils (+2 free)
 *   SRR/TDC/RO-3   Waveshare 26517   addr 33   8 coils
 *   SRR/TDC/RO-4   Waveshare 26517   addr 34   8 coils
 *   SRR/CLI/TH-1..3   temp/RH           addr 41-43  exterior
 *   SRR/CLI/TH-4..6   temp/RH           addr 44-46  crawlspace
 *   SRR/CLI/TH-7..9   temp/RH           addr 47-49  low level
 *   SRR/CLI/TH-10..12 temp/RH           addr 50-52  high level
 *
 * Everything is XP_MODBUS today: the site has no XP_LOCAL variables yet.
 *
 * @warning Binding::reg is the Modbus REGISTER ADDRESS the master puts on the
 * wire: the coil number on the relay boards, and the protocol START ADDRESS on
 * the meters (30001 -> 0, 30003 -> 2, ...). NEVER the device profile's object
 * index - nothing translates one into the other, and a row carrying the index
 * decodes half of the row before it. Nor the register column of the device map,
 * which is the Opta's own Modbus *slave* address and belongs in the slave map.
 * Each row's notes record the object index, so a reading can still be traced
 * back to the published map.
 *
 * @note The meters answer FC04 (input registers). The SDM630 manual requires it;
 * the MCT1-140 accepts 03 or 04 and is given 04 to match.
 *
 * @note Both meters publish IEEE-754 singles, high word at the lower address, in
 * direct engineering units, so these rows are 1:1 and carry no calibration. The
 * MCT1-140's "unite et precision" column (0.1V, 0.01A, 10W) is RESOLUTION, not a
```

`Binding::reg` is the Modbus REGISTER ADDRESS the master puts on the wire: the
coil number on the relay boards, and the protocol START ADDRESS on the meters
(30001 -> 0, 30003 -> 2, ...). NEVER the device profile's object index -
nothing translates one into the other, and a row carrying the index decodes
half of the row before it. Each row's `notes` records the object index, so a
reading can still be traced back to the published map.

What each unit will answer, which is what `tables/modbus_models.csv` states as
a request ceiling and an alignment:

- **Eastron SDM630-Modbus V2** (manual V1.6B). 40 values (80 registers) per
  transaction; a float's start address and count must both be even; holding
  registers take one parameter per message, so no multi-write.
- **Lettel MCT1-140.** Max quantity 100, every measurement a register-pair
  float, so the same pair alignment applies. Nothing writes to it.
- **Waveshare Analog Input 8CH (25767).** The eight readings are INPUT
  registers at 3x0000..3x0007, read with 0x04; nothing sits at 4x0000, so
  reading them with 0x03 draws exception 02. One register each, no alignment.
- **Waveshare Analog Output 8CH (26211).** Speaks 03/06/10 - no 0x04 and no
  coils. The eight outputs are HOLDING registers at 4x0000..4x0007, read and
  written, so the multi-write is available.
- **Waveshare Relay (D) (26517)** ("Development Protocol V2"). The eight relays
  are coils at 0x0000..0x0007, read with 0x01 and written with 0x05 or 0x0F, so
  a block may cover at most those eight. 0x00FF is the all-relays broadcast and
  0x0200/0x0400 the flash timers; none is a coil a row should name.
- **The temp/RH sensors.** An f32 at 0 (temperature) and another at 2
  (humidity): four registers, pair-aligned and read-only. FC04 is assumed,
  matching every other measurement on this bus; if they answer only FC03 it
  shows as exception 02 on all twelve at once, and the fix is one character
  per row.
- **The weather station** is a PLACEHOLDER until the unit is chosen. Wind,
  gust, rain rate and irradiance as four IEEE singles, so eight registers read
  as one block; its address, register layout and word order are marked TODO on
  each of its four rows, and those are the only ones that move when it is.

Both meters publish IEEE-754 singles, high word at the lower address, in direct
engineering units, so those rows are 1:1 and carry no calibration. The
MCT1-140's "unite et precision" column (0.1V, 0.01A, 10W) is RESOLUTION, not a
scale factor - except on the Long energy counters, where it is a multiplier
(10 Wh per count).

## Data model

Everything the site exposes is a **variable**, declared in `tables/*.csv` and
addressed by index. There is no payload struct: nothing carries a byte offset, so re-ordering a
table cannot silently re-point a status row, an alarm or a home field at its
neighbour.

\startuml "gh_mlb41 variables: what the site declares"
class "tables/*.csv" as IO {
  analog in (43) : SDM630 three-phase meter (37) + MCT1-140 transfer switch (6)
  analog out (0) : AO-1 module wired but unmapped
  digital in (0) : AI-1 module wired but unmapped
  digital out (28) : relay coils on four 26517 expansions
  internal analog : values the control programs compute
  internal digital : the alarm conditions + program state flags
}
\enduml

Each row carries its name, unit, description, plausibility band, display
precision and - the load-bearing one - a **default**, which does three jobs: an
input's substitute when a read is invalid, an output's safe state on a fault, and
a parameter's factory value.

The home-screen readings live in `tables/home.csv` - a caption is a person's choice. Grouping for the MONITOR page and
telemetry is derived: a variable's group is the prefix of its name, and whether
it is published is its scope. Naming a variable files it.

Alarms are not a separate table either: an `A()` row carries the boolean that
raises the alarm and the policy the `AlarmController` runs it on, and the model finds
them by scanning for `SCOPE_A`.

Parameters are not a separate thing: they are the `ROLE_PARAM` rows of the same
table, kept contiguous so the parameter pages can address them with a dense
0-based index.

## Integrating into an Arduino sketch

The site cross-compiles as an Arduino library. `Site::firmware()` is generated
and hands back the one firmware object, a **function-local static** so it is
constructed on first use rather than during static init - on the OPTA that has
to be after the Arduino core's `init()` has brought up the UARTs, and a
file-scope object would run before it. A minimal sketch:

```cpp
#include <Arduino.h>
#include "sites/gh_mlb41/site.h"   // Site::firmware()

void setup() {
    // boot wiring: UARTs, sinks, pages, banner, POST, watchdog
    Site::firmware().setup();
}

void loop() {
    Site::firmware().loop();   // one gated main-loop iteration
}
```

Notes:

- **Console baud** is the `console_baud` parameter (4800 by default, from
  `Terminal::ConsoleConfig`). It reaches the port through `ConfigBinder`, which
  retunes the UART and the terminal's per-render TX budget in the same call, so
  the two cannot fall out of step.
- **No simulated field on hardware.** `sim.h` compiles to nothing without
  `TARGET_SIMULATOR`; the Modbus driver fills the same variables instead.

## The simulated field

`sim.h` animates the tables off ONE reading of the wall clock, which is the
same clock the sun tracker works from - so a temperature minimum at dawn and
`PHASE_DAWN_BREEZE` are the same morning by construction rather than by
coincidence. `BOTANIK_SIM_EPOCH` moves the date, and with it the season: the
declination sets how high the sun gets, which sets the day length, the
irradiance and the seasonal mean, about 4 C in January and 20 C in July.

Three layers:

| Layer | What it is |
|---|---|
| the sky | season, solar elevation, cloud, irradiance, and the day's mean and swing |
| the house | one lumped thermal model, plus a stratification gap and a dew point |
| the field | the twelve probes, the two meters, the dividers and the weather station, all read off the two above |

The house is a **closed loop**: it reads the windows, the intake fans, the
destratification fans, the screen and the skirt, and answers them. Shut in full
sun it runs about 19 C over ambient; open it tracks ambient within about 2 C.
That is the difference between exercising the control programs and merely
running them - unfed back, the PID never settles and the ladders never stop
climbing. The exterior temperature is the solar forcing LAGGED by about 2.5
hours, which is what puts the minimum at dawn and the maximum mid-afternoon in
every season without either being a constant.

### Scenarios

Rare things a plant has to survive are not rare enough to wait for, so
`BOTANIK_SIM_SCENARIO` names one and the field puts the plant through it every
simulated day. They are the weather the control-system scenarios in
`tests/test_site_scenarios.cpp` are written against, as a whole DAY rather than
a held step.

| `BOTANIK_SIM_SCENARIO` | The day it makes | What it is for |
|---|---|---|
| `cycle` (default) | each of the below in turn, one per simulated day | an unattended run that still sweeps the state space |
| `fair` | an ordinary day for the season | the baseline |
| `hot` | cloudless, 7 C over the seasonal mean | the whole cooling ladder, `cli_tooHot_degC`, the cold bank it arms |
| `cold` | clear and 6 C under it | the screen, the negative half of the ladder, `cli_tooCold_degC` |
| `storm` | sustained wind over 75 kmph from 06:00 to 20:00, with rain | the roof shut, `alarm_weather_storm`, and how long the rain hold outlives the shower |
| `damp` | overcast, air within a few degrees of saturation | the rot guard, and the one thing the wet skirt must never do |
| `dry` | hot and parched | the other arm of the humidity ladder |
| `probes` | ordinary weather; the ridge probes stop answering 04:00-06:00, one at a time | the quorum holding, and then not |
| `supply` | ordinary weather; a sag at 09:00, the grid gone 13:00-15:00 with the generator carrying it, the UPS calling its battery low at 14:00 | the plant-wide lockout, and that it is reversible |

A scenario repeats daily, so its window comes round every simulated day, and
`BOTANIK_SIM_EPOCH` is a UNIX second rather than just a date, so a run can
START inside one. `scripts/sim_scenario.sh` is that, per scenario:

```bash
scripts/sim_scenario.sh list          # the scenarios, and what each one shows
scripts/sim_scenario.sh hot           # a minute a second, from 11:00
scripts/sim_scenario.sh supply 600    # ...and one at ten minutes a second
scripts/sim_scenario.sh cold 60 2026-01-15    # on a chosen day
```

It picks the start HOUR per scenario, so a fault window is minutes away rather
than most of a simulated day, and parks the mock network's chatter in a log
instead of over the console. Under `cycle` the character CROSS-FADES over the
first three hours of a day: stepping the weather at the stroke of midnight
would be the implausibility this model exists to remove.

Under `cycle` a full sweep is eight simulated days, which is about an hour of
wall clock in fast mode. See `src/botanik_sim/README.md` for the clock.
- `src/botanik_fw/` is the canonical example of this wiring.
