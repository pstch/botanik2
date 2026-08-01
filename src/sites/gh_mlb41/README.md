# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse.
`libbotanik` stays generic; this directory is the single source of truth for the
`gh_mlb41` greenhouse.

| File | Origin | Holds |
|---|---|---|
| `botanik.h` | generated | the only file that knows where `libbotanik` sits, so the directory is relocatable |
| `vars.h` / `vars.cpp` | generated | every variable in one table (field inputs/outputs, computed internals, parameters), plus its RAM value store and string pool |
| `status.h` | generated | the display groups (`VarGroup[]`) and the home-screen readings (`HomeVar[]`), as `VarRef`s |
| `alarms.h` | generated | the `g_alarms[]` condition table: code, description, the `VarRef` carrying the condition, dwell, remediation |
| `descriptor.h` | generated | the one `g_site` `SiteDescriptor` bundling every table |
| `config.h` | generated | build-wide config overrides (`#define BOTANIK_*`); kept separate so CMake includes it before the library defaults |
| `commands.h` | hand-written | the site's shell commands |
| `tasks.h` | hand-written | the control programs |
| `sim.h` | hand-written | the simulated field: it writes the variable tables the way a Modbus driver will. Its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build |
| `site.h` | hand-written | the thin `GreenhouseMLB41` binding and the uniform `BotanikSite` handle |

`GreenhouseMLB41` derives `BotanikFirmware` and just
passes `g_site` to the base constructor - it declares no pages and overrides no hooks.
The base owns every page (home/shell/logs/alarms/config/monitor) and wires the
descriptor's tables generically, so the site is data, not code. A site needing custom
per-beat logic or an extra page (e.g. re-adding a STATUS page by subclassing
`Pages::StatusPage`) would override the base's `on_step()` / `register_pages()`. See
`src/libbotanik/core/firmware.h` for the full extension model and a worked example.

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

`status.h` groups those variables for display (`VarGroup[]`, one sub-topic each)
and lists the home-screen readings; `alarms.h` names, per condition, the internal
digital that carries it. All three are generated from the same source, so their
indices agree by construction.

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

- **Console baud.** Defaults to `BOTANIK_CONSOLE_BAUD`; call `set_console_baud()`
  before `setup()` to change it, as the simulator harness does.
- **No simulated field on hardware.** `sim.h` compiles to nothing without
  `TARGET_SIMULATOR`; the Modbus driver fills the same variables instead.
- `src/botanik_fw/` is the canonical example of this wiring.
