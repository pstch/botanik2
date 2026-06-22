# gh_mlb41 site

The reference Botanik **site**: everything specific to one physical greenhouse and
its PLC program. `libbotanik` stays generic; this directory is the single source of
truth for the `gh_mlb41` greenhouse.

| File | Holds |
|---|---|
| `site.h` | the PLC payload (`PlcInput/OutputData_t` + status enums and their string companions) **and** the `GreenhouseMLB41` firmware subclass. The uniform handle a harness builds against (`BotanikSite`, the payload, the sim-only mock-PLC entry). |
| `pages.h` | the site pages (`GreenhouseHomePage`, `GreenhouseStatusPage`). |
| `alarms.h` / `alarms.cpp` | the `g_alarms[]` condition table (one row per alarm bit). |
| `sim.h` | the header-only simulator mock PLC (data animator + emulator wiring); its whole body is `#ifdef TARGET_SIMULATOR`, so it drops out of the hardware build. |
| `config.h` | build-wide config overrides (`#define BOTANIK_*`); kept separate so CMake includes it before the library defaults. |

`GreenhouseMLB41` derives `BotanikFirmware<PlcInputData_t, PlcOutputData_t>` and adds
its pages, its alarm table, and one example shell command (`gh`). It never overrides
`setup()`/`loop()`; it customizes through the base's hooks (`register_pages()`,
`register_commands()`, `on_setup()`). See `src/libbotanik/firmware.h` for the full
extension model and a worked example.

## Data model

`PlcInputData_t` is empty (the host writes only the library log/sync fields back to
the PLC). `PlcOutputData_t` is the greenhouse the PLC publishes each cycle - sensors
and actuator feedback, grouped below. Three `uint8_t` fields carry enumerated states
with a `str()` companion (`UpsStatus`, `TrayStatus`, `NetStatus`):

\dot "gh_mlb41 PlcOutputData_t: the greenhouse state the PLC publishes to the host"
digraph datamodel {
  bgcolor="transparent"; rankdir=LR; nodesep=0.3; ranksep=0.6;
  node [shape=plaintext, fontname="Helvetica", fontsize=9];
  edge [arrowsize=0.6, fontname="Helvetica", fontsize=8, color="#888888"];

  payload [label=<
    <table border="0" cellborder="1" cellspacing="0" cellpadding="5" color="#9e9e9e">
      <tr><td colspan="2" bgcolor="#37474f"><font color="#ffffff"><b>PlcOutputData_t</b> &#160; (gh_mlb41, PLC &#8594; host)</font></td></tr>
      <tr><td bgcolor="#f6c6c2"><b>alarms</b></td><td align="left">uint64 bitset (one condition per bit)</td></tr>
      <tr><td bgcolor="#fde6c0"><b>power</b></td><td align="left">grid_ac / dc_bus / battery / generator (mV), grid_freq</td></tr>
      <tr><td bgcolor="#d6e4f0"><b>climate</b></td><td align="left">ext/floor/ceil temp + RH, wind_peak, rain_age</td></tr>
      <tr><td bgcolor="#cfe7d8"><b>irrigation</b></td><td port="tray" align="left">water_in / water_out, current_tray, tray_status*</td></tr>
      <tr><td bgcolor="#eceff1"><b>system</b></td><td align="left">sunrise_min, sunset_min, ntp_offset_ms</td></tr>
      <tr><td bgcolor="#e6dcf0"><b>actuators</b></td><td align="left">window a-d, blower a-d, extractor a/b, thermal_screen (%)</td></tr>
      <tr><td bgcolor="#d7ecd9"><b>status</b></td><td port="stat" align="left">ups_status*, dc_bus_a/b_ok, rain, cooling, generator,<br align="left"/>heat_blower, heatpump, net_status*, ntp_synced</td></tr>
    </table>
  >];

  ups [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="3" color="#9e9e9e"><tr><td bgcolor="#455a64"><font color="#ffffff"><b>UpsStatus</b></font></td></tr><tr><td bgcolor="#eceff1" align="left">OFFLINE / ONLINE / ON_BATTERY / FAULT</td></tr></table>>];
  tray [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="3" color="#9e9e9e"><tr><td bgcolor="#455a64"><font color="#ffffff"><b>TrayStatus</b></font></td></tr><tr><td bgcolor="#eceff1" align="left">IDLE / FILLING / SOAKING / DRAINING</td></tr></table>>];
  net [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="3" color="#9e9e9e"><tr><td bgcolor="#455a64"><font color="#ffffff"><b>NetStatus</b></font></td></tr><tr><td bgcolor="#eceff1" align="left">DOWN / LINK / ONLINE</td></tr></table>>];

  payload:tray -> tray [label="tray_status"];
  payload:stat -> ups  [label="ups_status"];
  payload:stat -> net  [label="net_status"];
}
\enddot

## Integrating into an Arduino sketch

The site cross-compiles as an Arduino library. The firmware is one object,
`GreenhouseMLB41`, bound to the M4 coprocessor's PLC shared memory. Allocate it as a
**function-local static** (no heap) so it is constructed only after the Arduino core's
`init()` has brought up the UARTs and the shared-memory region, then call `setup()`
once and `loop()` every iteration.

A minimal sketch:

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
- **Console baud / geometry.** The HAL constructor uses `BOTANIK_CONSOLE_BAUD`
  (pin it in `config.h`) and 80x24; pass `BotanikSite fw(cols, rows)` to change the
  geometry. The simulator runs slower on purpose, so its harness passes an explicit
  low baud through the region constructor instead.
- **No mock PLC on hardware.** `sim.h` compiles to nothing without
  `TARGET_SIMULATOR`; the real M4 coprocessor animates the payload instead.
- The standalone `botanik_fw` target and the C-callable `botanik_setup()` /
  `botanik_step()` hooks in `src/botanik_fw/` are the canonical examples of this
  wiring (the Axel PLC runtime drives the hooks instead of a bare `loop()`).
