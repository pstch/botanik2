// REVIEW_35: TODO
# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse and
its PLC program. `libbotanik` stays generic; this directory is the single source of
truth for the `gh_mlb41` greenhouse.

| File | Holds |
|---|---|
| `site.h` | the `g_site` `SiteDescriptor` bundling every table, and the thin `GreenhouseMLB41` binding. The uniform handle a harness builds against (`BotanikSite`). |
| `io.h` / `io.cpp` | the variable tables (analog/digital in/out + computed internals) and their RAM value stores. |
| `status.h` | the display groups (`VarGroup[]`) and the home-screen readings (`HomeVar[]`), as VarRefs into those tables. |
| `data.h` | `GreenhouseConfig_t`: the byte image the config parameters are laid out in - all that is left of the PLC payload. |
| `alarms.h` | the `g_alarms[]` condition table (one row per alarm: code, description, the VarRef carrying the condition, dwell). |
| `sim.h` | the header-only simulated field: it writes the variable tables the way a Modbus driver will. Its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build. |
| `config.h` | build-wide config overrides (`#define BOTANIK_*`); kept separate so CMake includes it before the library defaults. |

`GreenhouseMLB41` derives `BotanikFirmware` and just
passes `g_site` to the base constructor - it declares no pages and overrides no hooks.
The base owns every page (home/shell/logs/alarms/config/monitor) and wires the
descriptor's tables generically, so the site is data, not code. A site needing custom
per-beat logic or an extra page (e.g. re-adding a STATUS page by subclassing
`Pages::StatusPage`) would override the base's `on_step()` / `register_pages()`. See
`src/libbotanik/core/firmware.h` for the full extension model and a worked example.

## Data model

Everything the site exposes is a **variable**, declared in `io.h` and addressed by
table and index. There is no payload struct: nothing carries a byte offset any
more, so re-ordering a table cannot silently re-point a status row, an alarm or a
home field at its neighbour.

\startuml "gh_mlb41 variables: what the site declares"
class "io.h" as IO {
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

The one byte image left is `data.h`: `GreenhouseConfig_t`, the layout the config
**parameters** are stored in. Folding those into the variable model waits on an
index-addressed parameter store.

## Integrating into an Arduino sketch

The site cross-compiles as an Arduino library. The firmware is one object,
`GreenhouseMLB41`, bound to the M4 coprocessor's PLC shared memory. Allocate it as a
**function-local static** (no heap) so it is constructed only after the Arduino core's
`init()` has brought up the UARTs and the shared-memory region. A minimal sketch:

```cpp
#include <Arduino.h>
#include "sites/gh_mlb41/site.h"   // BotanikSite = GreenhouseMLB41

// Constructed on first call - after the core's init(), so the UARTs and the PLC
// shared region are valid to bind to - and kept in static storage for the whole run.
// The default constructor binds to the coprocessor's PLC region (via HAL) at the
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

- **Console baud / geometry.** The HAL constructor uses `BOTANIK_CONSOLE_BAUD` and
  80x24; pass `BotanikSite fw(cols, rows)` to change the geometry. The simulator passes
  an explicit low baud through the region constructor instead.
- **No simulated field on hardware.** `sim.h` compiles to nothing without
  `TARGET_SIMULATOR`; the Modbus driver fills the same variables instead.
- The `botanik_fw` target and its C-callable hooks in `src/botanik_fw/` are the
  canonical examples of this wiring (the Axel runtime drives the hooks, not a bare
  `loop()`).
