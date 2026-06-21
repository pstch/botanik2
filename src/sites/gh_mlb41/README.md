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

## Integrating into an Arduino sketch

The site cross-compiles as an Arduino library. The firmware is one object,
`GreenhouseMLB41`, bound to the M4 coprocessor's PLC shared memory. Allocate it as a
**function-local static** (no heap) so it is constructed only after the Arduino core's
`init()` has brought up the UARTs and the shared-memory region, then call `setup()`
once and `loop()` every iteration.

A minimal sketch:

```cpp
#include <Arduino.h>
#include "sites/gh_mlb41/site.h"   // BotanikSite = GreenhouseMLB41, + the payload

// Constructed on first call - after the core's init(), so the UARTs and the PLC
// shared region are valid to bind to - and kept in static storage for the whole run.
static BotanikSite& firmware() {
    static BotanikSite fw(
        static_cast<Botanik::PlcIO::PlcInput<PlcInputData_t>*  >(Botanik::HAL::get_plc_input()),
        static_cast<Botanik::PlcIO::PlcOutput<PlcOutputData_t>*>(Botanik::HAL::get_plc_output()),
        Botanik::Config::CONSOLE_BAUD, /*cols=*/80, /*rows=*/24);
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
- **Console baud / geometry** are passed in (the simulator runs slower on purpose);
  a site can pin its own default in `config.h` (`BOTANIK_CONSOLE_BAUD`).
- **No mock PLC on hardware.** `sim.h` compiles to nothing without
  `TARGET_SIMULATOR`; the real M4 coprocessor animates the payload instead.
- The standalone `botanik_fw` target and the C-callable `botanik_setup()` /
  `botanik_step()` hooks in `src/botanik_fw/` are the canonical examples of this
  wiring (the Axel PLC runtime drives the hooks instead of a bare `loop()`).
