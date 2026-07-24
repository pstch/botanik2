// REVIEW_35: TODO
# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse and
its PLC program. `libbotanik` stays generic; this directory is the single source of
truth for the `gh_mlb41` greenhouse.

| File | Holds |
|---|---|
| `site.h` | the PLC payload (`PlcInput/OutputData_t` + status enums and their string companions), the declarative tables (status-variable `ScalarGroup[]`, the `gh` shell command), the `g_site` `SiteDescriptor` bundling every table, and the thin `GreenhouseMLB41` binding. The uniform handle a harness builds against (`BotanikSite`, the payload, the sim-only mock-PLC entry). |
| `alarms.h` | the `g_alarms[]` condition table (one row per alarm: code, description, the payload boolean's `offsetof`, dwell). |
| `sim.h` | the header-only simulator mock PLC (data animator + emulator wiring); its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build. |
| `config.h` | build-wide config overrides (`#define BOTANIK_*`); kept separate so CMake includes it before the library defaults. |

`GreenhouseMLB41` derives `BotanikFirmware<PlcInputData_t, PlcOutputData_t>` and just
passes `g_site` to the base constructor - it declares no pages and overrides no hooks.
The base owns every page (home/shell/logs/alarms/config/monitor) and wires the
descriptor's tables generically, so the site is data, not code. A site needing custom
per-beat logic or an extra page (e.g. re-adding a STATUS page by subclassing
`Pages::StatusPage`) would override the base's `on_step()` / `register_pages()`. See
`src/libbotanik/core/firmware.h` for the full extension model and a worked example.

## Data model

`PlcInputData_t` is empty (the host writes only the library log/sync fields back to
the PLC). `PlcOutputData_t` is the greenhouse the PLC publishes each cycle - sensors
and actuator feedback, grouped below. Three `uint8_t` fields carry enumerated states
with a `str()` companion (`UpsStatus`, `TrayStatus`, `NetStatus`):

\startuml "gh_mlb41 PlcOutputData_t: the greenhouse state the PLC publishes"
class PlcOutputData_t {
  alarms : one uint8 boolean per condition (AlarmModel reads each by offset)
  power : grid_ac / dc_bus / battery / generator (mV), grid_freq
  climate : ext/floor/ceil temp + RH, wind_peak, rain_age
  irrigation : water_in / water_out, current_tray, tray_status*
  system : sunrise_min, sunset_min, ntp_offset_ms
  actuators : window a-d, blower a-d, extractor a/b, thermal_screen (%)
  status : ups_status*, dc_bus_a/b_ok, rain, cooling, generator, heat_blower, heatpump, net_status*, ntp_synced
}
\enduml

The `*` fields are enums with a `str()` companion:

- `UpsStatus` - OFFLINE / ONLINE / ON_BATTERY / FAULT
- `TrayStatus` - IDLE / FILLING / SOAKING / DRAINING
- `NetStatus` - DOWN / LINK / ONLINE

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

- **Payload contract.** On hardware `PlcOutputData_t` must match the PLC IDE's
  generated type byte-for-byte. Once those types are in scope, assert it:
  ```cpp
  static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(Botanik::PlcIO::PlcInput<PlcInputData_t>));
  static_assert(alignof(PLCSharedVarsOutput_t) == alignof(Botanik::PlcIO::PlcOutput<PlcOutputData_t>));
  ```
- **Console baud / geometry.** The HAL constructor uses `BOTANIK_CONSOLE_BAUD` and
  80x24; pass `BotanikSite fw(cols, rows)` to change the geometry. The simulator passes
  an explicit low baud through the region constructor instead.
- **No mock PLC on hardware.** `sim.h` compiles to nothing without `TARGET_SIMULATOR`;
  the real M4 coprocessor animates the payload instead.
- The `botanik_fw` target and its C-callable hooks in `src/botanik_fw/` are the
  canonical examples of this wiring (the Axel runtime drives the hooks, not a bare
  `loop()`).
