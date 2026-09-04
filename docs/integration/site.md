# Declaring a site {#int_site}

A **site** is one directory. It is the single source of truth for one
installation, used verbatim by both the simulator and the hardware build - never
forked per target.

## The files

Every one is hand-written C++.

| File | Holds |
|---|---|
| `config.h` | `#define BOTANIK_*` build-wide overrides |
| `vars.h` | the device map the rows were derived from, the counts, the enums, and the home-screen captions |
| `vars.cpp` | the `Descriptor` arrays themselves, the calibration blocks, the field devices, and the home-field reference array |
| `programs/<stage>/<class>/<class>.h` | one control program's declaration: its rows, the blocks it holds, and its `bind()` |
| `programs/<stage>/<class>/<class>.cpp` | that program's law. The one place a site expresses *behaviour* |
| `programs.h` | the order the programs run in, as one X-macro |
| `blocks/<name>/<name>.{h,cpp}` | function blocks of the site's own, one directory each |
| `site.h` | the shell commands, the `SiteDescriptor`, and the firmware class |
| `sim.h` | the simulated field (whole body `#ifdef TARGET_SIMULATOR`) |

These files were generated once, from CSV and TOML, by a tool that has since
been retired: the shape they have is its doing, and it is worth keeping. They
are ordinary source now, with no build-time step behind them - the tree has no
Python dependency, and an Arduino sketch path could not have run one anyway.

What the generator used to prove, `tests/test_site.cpp` proves instead, walking
the bank the site actually built: register uniqueness and blocks, the parameter
run's contiguity, the persisted pool's size, alarm codes, and every default
sitting inside its own band. What no test can see until the run starts is caught
at bring-up: a program resolving a name no row declares is INVALID for the life
of the run, which the POST reports.

@warning Intra-site includes are relative (`vars.h`, and `../../../vars.h`
from inside a program directory); the library is reached as `libbotanik.h`, which resolves
against the build's include root. The sibling rule is load-bearing rather than
style: the Arduino PLC IDE adds no include directory inside a sketch folder, so
quoted-relative resolution is the only mechanism that works there for the site's
own files.

`config.h` stays separate from `site.h`, and the reason is ordering rather than
content: `libbotanik_config.h` reads it before its own `#ifndef` defaults, with
none of the library in scope.

## What is declared, and what is written

The line between the two halves is worth stating, because it is where most
questions about this tree land.

**Declared** is anything a rule can check: a row's type, band, register,
transport, safe state, access level; which program produces which row; what an
enum's codes are. It is C++, but it is data - `constexpr` factory calls and
`resolve_*` names, in tables and headers - and the suite checks all of it against
the bank the site built.

**Written** is behaviour: what a law does with the numbers. A program's `.cpp` is the
only file where a site decides anything.

## The binding

`site.h` is the firmware class. The descriptor is what the site hands the
library; everything else the base owns.

```cpp
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,      g_command_count,        // site shell commands
    g_home_fields,   g_home_field_count,     // home-screen readings
    g_system_fields, g_system_field_count,   // SYSTEM-page readings
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    &g_device_table,                         // what each unit answers
#endif
    rows, row_order, ROWS_COUNT, PARAM_FIRST, PARAM_COUNT,
};
```

Every pointer is **borrowed** and must outlive the firmware - site statics, not
locals. Any table may be null with count 0.

@note `SiteDescriptor` has no default member initialisers, deliberately. An NSDMI
would stop it being a C++11 aggregate, and the descriptor would no longer
constant-initialise into flash.

The variable table is NOT in the descriptor: the site hands it over by overriding
`register_vars()`, which calls `register_all()` in `vars.h`. That
function holds the ordering rule the parameter block depends on, so no law and
no page has to remember it.

```cpp
class GreenhouseMLB41 final : public Botanik::BotanikFirmware {
    // Plain members, in EXECUTION order: registering is what puts a program in
    // the beat, so there is no table of pointers to keep in step with one.
    Site::AcGridWatch        _ac_grid;
    Site::TempRHZone         _zones;
    // ...

public:
    GreenhouseMLB41()
        : Botanik::BotanikFirmware(Site::g_site),
          // The base is fully constructed before any derived member, so
          // setup_context() and io() are already alive here.
          _ac_grid(setup_context(), io()),
          _zones(setup_context(), io())
    {}

protected:
    void register_vars() override { Site::register_all(io()); }

    void on_setup() override {
        _ac_grid.register_tasks();
        _zones.register_tasks();
        // ...in the same order as the members above.
    }
};

using BotanikSite = GreenhouseMLB41;   // what the harness instantiates
```

@note The program list appears three times - member, constructor init, and
`register_tasks()` - and `SITE_PROGRAM_LIST` in `programs.h` is what keeps them
in step: one X-macro, expanded three times, so the order is written once.

## Running it

`botanik_sim` and `botanik_fw` are thin: each selects a site and runs it. The
site is chosen at configure time.

```bash
cmake -B build -DBOTANIK_TARGET=SIMULATOR -DBOTANIK_SITE=gh_mlb41
make -C build
```

`-DBOTANIK_SITE=<name>` resolves to `src/sites/<name>/site.h` and compiles every
`.cpp` beside it. `src/sites/bare` is the other end of the range: every optional
feature off and nothing declared, existing to be COMPILED rather than run, so an
OFF path cannot rot unnoticed (`make check-bare`).

## The workflow

1. add or change the row in `vars.cpp`, and its count or enum in `vars.h`
2. write or change the law in its own `programs/<stage>/<class>/<class>.{h,cpp}`
3. `make -C build check`

Step 3 is where most mistakes are caught, because `tests/test_site.cpp` walks
the bank the site actually built: a register collision, a register outside its
scope's block, two rows on one terminal, a default outside its own band, a name
nothing resolves. What it cannot see is caught the first time the firmware runs
instead - a program whose `bind()` missed a name is INVALID for the life of the
run.
