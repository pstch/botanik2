# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse.
`libbotanik` stays generic; this directory is the single source of truth for the
`gh_mlb41` greenhouse.

| Path | Origin | Holds |
|---|---|---|
| `tables/*.csv` | **declared** | every variable the site has, one file per row factory |
| `programs/<stage>/<class>/<class>.toml` | declared | one control program: what it connects to, what it holds, what it declares |
| `programs/order.toml` | declared | the order the programs run in |
| `generated/vars.gen.{h,cpp}` | **generated** | the descriptor arrays, the calibration blocks, the enums, the RAM value store |
| `generated/programs.gen.h` | generated | every program's header, the map of what they pass to each other, and `SITE_PROGRAM_LIST` |
| `programs/<stage>/<class>/<class>.gen.h` | generated | one program's whole declaration part |
| `programs/<stage>/<class>/<class>.cpp` | hand-written | one control program's body: the law itself |
| `blocks/*.{h,cpp}` | hand-written | function blocks of the site's own, nameable from a program's table |
| `vars.h` | hand-written | what the device map MEANS, and the home-screen captions |
| `site.h` | hand-written | the shell commands, the `g_site` `SiteDescriptor`, and the `GreenhouseMLB41` binding |
| `config.h` | hand-written | build-wide config overrides (`#define BOTANIK_*`); separate because it is read with none of the library in scope |
| `sim.h` | hand-written | the simulated field: it writes the variable tables the way a Modbus driver will. Its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build |

A program is one directory: the table that declares it, the header generated
from that table, and the `.cpp` you write. `docs/generated/gh_mlb41_report.pdf`
lists all 23 of them with what each is connected to, holds and remembers.

`GreenhouseMLB41` derives `BotanikFirmware` and just
passes `g_site` to the base constructor - it declares no pages and overrides no hooks.
The base owns every page (home/shell/logs/alarms/config/monitor) and wires the
descriptor's tables generically, so the site is data, not code. A site needing custom
per-beat logic or an extra page (e.g. re-adding a STATUS page by subclassing
`Pages::StatusPage`) would override a base phase hook / `register_pages()`. See
`src/libbotanik/firmware/firmware.h` for the full extension model and a worked example.

## Data model

Everything the site exposes is a **variable**, declared in `vars.h` and addressed by
index. There is no payload struct: nothing carries a byte offset, so re-ordering a
table cannot silently re-point a status row, an alarm or a home field at its
neighbour.

\startuml "gh_mlb41 variables: what the site declares"
class "vars.h" as IO {
  analog in (43) : SDM630 three-phase meter (37) + MCT1-140 transfer switch (6)
  analog out (0) : AO-1 module wired but unmapped
  digital in (0) : AI-1 module wired but unmapped
  digital out (28) : relay coils on four 26517 expansions
  internal analog : values the control programs compute
  internal digital : the 23 alarm conditions + program state flags
}
\enduml

Each row carries its name, unit, description, plausibility band, display
precision and - the load-bearing one - a **default**, which does three jobs: an
input's substitute when a read is invalid, an output's safe state on a fault, and
a parameter's factory value.

The home-screen readings live in `vars.h`. Grouping for the MONITOR page and
telemetry is derived: a variable's group is the prefix of its name, and whether
it is published is its scope. Naming a variable files it.

Alarms are not a separate table either: an `A()` row carries the boolean that
raises the alarm and the policy the `AlarmController` runs it on, and the model finds
them by scanning for `SCOPE_A`.

Parameters are not a separate thing: they are the `ROLE_PARAM` rows of the same
table, kept contiguous so the parameter pages can address them with a dense
0-based index.

## Integrating into an Arduino sketch

The site cross-compiles as an Arduino library. The firmware is one object,
`GreenhouseMLB41`. Allocate it as a **function-local static** (no heap) so it is
constructed only after the Arduino core's `init()` has brought up the UARTs.
A minimal sketch:

```cpp
#include <Arduino.h>
#include "sites/gh_mlb41/site.h"   // BotanikSite = GreenhouseMLB41

// Constructed on first call - after the core's init(), so the UARTs are valid -
// and kept in static storage for the whole run. The default constructor uses the
// config-default console baud; pass (cols, rows) to override the 80x24 geometry.
static BotanikSite& firmware() {
    static BotanikSite fw;
    return fw;
}

void setup() {
    firmware().setup();   // boot wiring: UARTs, sinks, pages, banner, POST, watchdog
}

void loop() {
    firmware().loop();    // one gated main-loop iteration
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
| `storm` | wind over `cli_windShut_mps` from 06:00 to 20:00, with rain | the roof lockout, and how long it outlives the shower |
| `damp` | overcast, air within a few degrees of saturation | the rot guard, and the one thing the wet skirt must never do |
| `dry` | hot and parched | the other arm of the humidity ladder |
| `probes` | ordinary weather; the ridge probes stop answering 04:00-06:00, one at a time | the quorum holding, and then not |
| `supply` | ordinary weather; a sag at 09:00, the grid gone 13:00-15:00 with the generator carrying it, the UPS calling its battery low at 14:00 | the plant-wide lockout, and that it is reversible |

A scenario repeats daily, so its window comes round every simulated day.
`BOTANIK_SIM_EPOCH` is a UNIX second, not just a date, so a run can START
inside one:

```bash
# straight into the outage: 2026-07-01 12:45 UTC, the supply scenario
BOTANIK_SIM_CLOCK=fast BOTANIK_SIM_SCENARIO=supply \
  BOTANIK_SIM_EPOCH=$(date -u -d '2026-07-01 12:45' +%s) ./build/botanik_sim
```

Under `cycle` a full sweep is eight simulated days, which is about an hour of
wall clock in fast mode. See `src/botanik_sim/README.md` for the clock.
- `src/botanik_fw/` is the canonical example of this wiring.
