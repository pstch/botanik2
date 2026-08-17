# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse.
`libbotanik` stays generic; this directory is the single source of truth for the
`gh_mlb41` greenhouse.

| File | Origin | Holds |
|---|---|---|
| `vars.h` | generated | everything declarative: every variable in one table (parameters, alarms, field IO, computed and status values), the analog calibration blocks, the home-screen readings, and the RAM value store + string pool behind them |
| `site.h` | hand-written | the shell commands, the `g_site` `SiteDescriptor`, and the `GreenhouseMLB41` binding |
| `config.h` | hand-written | build-wide config overrides (`#define BOTANIK_*`); separate because it is read with none of the library in scope |
| `tasks.h` | hand-written | the control programs |
| `sim.h` | hand-written | the simulated field: it writes the variable tables the way a Modbus driver will. Its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build |
| `site.h` | hand-written | the thin `GreenhouseMLB41` binding and the uniform `BotanikSite` handle |

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

The home-screen readings are the only declarative variable table left, and they
live in `vars.h` with everything else. Grouping for the MONITOR page and
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
- `src/botanik_fw/` is the canonical example of this wiring.
