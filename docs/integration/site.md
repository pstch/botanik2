# Declaring a site {#int_site}

A **site** is one directory. It is the single source of truth for one installation,
used verbatim by both the simulator and the hardware build - never forked per
target.

## The files

| File | Origin | Holds |
|---|---|---|
| `vars.h` | generated | everything declarative: every variable in one table, the analog calibration blocks, the home-screen readings, and the value store + string pool |
| `tasks.h` | hand-written | the control programs |
| `sim.h` | hand-written | the simulated field, for the simulator |
| `config.h` | hand-written | `#define BOTANIK_*` build-wide overrides |
| `site.h` | hand-written | the shell commands, the `SiteDescriptor` bundling every table, the firmware binding and the uniform handle |

The generated half is schema; the hand-written half is code. Four files are yours to
write.

@note A site is **five files**: `vars.h` (everything declarative), `site.h` (the
commands, the descriptor and the binding), `tasks.h` and `sim.h` (the behaviour), and
`config.h`. Everything else that used to be its own file has folded in - the alarm
table (an alarm is a `SCOPE_A` row), the status groups (a group is a name prefix), the
home fields, the value store, the descriptor and the commands.

`config.h` stays separate, and the reason is ordering rather than content:
`libbotanik_config.h` reads it before its own `#ifndef` defaults, with none of the
library in scope. It can be folded into `site.h` behind a pass macro so the same file
answers both questions, but a header that is deliberately enterable twice is a lot of
machinery for five `#define`s.

@warning Intra-site includes are bare sibling names (`vars.h`, `tasks.h`); the
library is reached as `libbotanik.h`, which resolves against the build's include
root. The sibling rule is load-bearing rather than style: the Arduino PLC IDE adds
no include directory inside a sketch folder, so quoted-relative resolution is the
only mechanism that works there for the site's own files.

## The binding

`site.h` is the whole firmware class:

```cpp
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,    g_command_count,
    g_home_fields, g_home_field_count, g_home_refs,
    SITE_VAR_TABLES,
    g_programs,    g_program_count,
};

class GreenhouseXXX final : public Botanik::BotanikFirmware {
public:
    GreenhouseXXX() : Botanik::BotanikFirmware(Site::g_site) {}
};

#include "sim.h"

using BotanikSite = GreenhouseXXX;
```

It declares no members and overrides nothing. The base owns every page and wires
every table generically. A site that needs custom per-beat logic or an extra page
overrides a hook here - see @ref int_extending.

Every site exposes the same three names, so a harness never spells the site name:

| Name | From | Is |
|---|---|---|
| `BotanikSite` | `site.h` | the firmware type to instantiate |
| `Site::simulate(Vars::Bank&, uint32_t now_ms)` | `sim.h` | the simulated field |
| `dilated_timeout(uint32_t base_ms)` | `sim.h` | simulator-only timeout dilation |

`sim.h` is included **after** the variable tables it animates, and its whole body is
`#ifdef TARGET_SIMULATOR` so it compiles to nothing on hardware. Keep
simulator-only code inside that guard rather than in the portable path.

## Running it

A harness includes `BOTANIK_SITE_HEADER`, allocates a `BotanikSite`, and calls
`setup()` then `loop()`:

```cpp
#include BOTANIK_SITE_HEADER

BotanikSite firmware;

int main() {
    firmware.setup();
    for (;;) firmware.loop();
}
```

On hardware, allocate it as a **function-local static** so it is constructed after
the platform's own init has brought up the UARTs, not before `main()`:

```cpp
static BotanikSite& firmware() { static BotanikSite fw; return fw; }

void setup() { firmware().setup(); }
void loop()  { firmware().loop();  }
```

`setup()` and `loop()` are non-virtual. The bring-up order and the per-tick cycle
are fixed by the base and cannot be reordered or skipped - see @ref dev_architecture
for what that order is and why.

Anything tunable before start-up is a modifier called ahead of `setup()`:
`set_console_baud()`, `set_monitor_baud()`, `set_hw_watchdog_ms()`.

## Selecting a site at build time

Two cache variables, checked in this order:

| Variable | Selects |
|---|---|
| `BOTANIK_SITE_DIR` | any directory on disk - a site in your own project tree |
| `BOTANIK_SITE` | a directory under `src/sites/` in the library tree (default `gh_mlb41`) |

```sh
cmake -B build -DBOTANIK_TARGET=SIMULATOR -DBOTANIK_SITE_DIR=/path/to/my_site
make -C build botanik_sim
```

The directory must contain both `config.h` and `site.h`, or the configure step
fails. CMake then defines two absolute paths on the `botanik` target:

```cmake
target_compile_definitions(botanik PUBLIC
    BOTANIK_SITE_CONFIG="${_site_dir}/config.h"
    BOTANIK_SITE_HEADER="${_site_dir}/site.h")
```

Nothing is added to the include path. That is deliberate: flat site filenames like
`config.h` must not be able to shadow the platform's own headers.

@warning Those defines are `PUBLIC` for a reason. **Any target that includes
`libbotanik_config.h` must link `botanik`**, or it silently compiles against
un-overridden defaults and mismatches the rest of the build.

Without CMake, `#define BOTANIK_SITE <name>` resolves the pair relative to `src/`;
with neither, the library compiles standalone against its own defaults. A vendored
build can also just drop a `botanik_site_config.h` beside `libbotanik_config.h` and
it is picked up automatically.

## Building

```sh
cmake -B build -DBOTANIK_TARGET=SIMULATOR    # host simulator
cmake -B build -DBOTANIK_TARGET=OPTA         # cross-compile for the OPTA
make -C build
```

See @ref dev_build for the build options, and @ref reference for a complete worked
site.
