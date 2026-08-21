# Declaring a site {#int_site}

A **site** is one directory. It is the single source of truth for one
installation, used verbatim by both the simulator and the hardware build - never
forked per target.

## The files

Three kinds, and which kind a file is decides how you change it.

| File | Kind | Holds |
|---|---|---|
| `tables/*.csv` | **declared** | every variable the site has, one file per row factory |
| `tables/enums.csv` | declared | the vocabularies its enumerated rows are written over |
| `programs/<stage>/<class>/<class>.toml` | declared | one control program: what it connects to, what it holds, what it declares |
| `programs/order.toml` | declared | the order the programs run in, which is the one thing about a program that is not a property of the program |
| `generated/vars.gen.h` | **generated** | the `Descriptor` arrays, the calibration blocks, the enums and their converters |
| `generated/programs.gen.h` | generated | every program's header, the map of what they pass to each other, and `SITE_PROGRAM_LIST` |
| `programs/<stage>/<class>/<class>.gen.h` | generated | one program's whole declaration part: its rows, its blocks, its `bind()` |
| `config.h` | hand-written | `#define BOTANIK_*` build-wide overrides |
| `vars.h` | hand-written | what the device map MEANS, and the home-screen captions |
| `vars.cpp` | hand-written | the home-field reference array |
| `programs/<stage>/<class>/<class>.{h,cpp}` | hand-written | one control program: the state the table cannot describe, its helpers, and the law itself. The one place a site expresses *behaviour* |
| `blocks/*.{h,cpp}` | hand-written | function blocks of the site's own, nameable from a program's table |
| `site.h` | hand-written | the shell commands, the `SiteDescriptor`, and the firmware class |
| `sim.h` | hand-written | the simulated field (whole body `#ifdef TARGET_SIMULATOR`) |

**The generated C++ is committed and never built.** The tree has no Python build
dependency, and an Arduino sketch path could not run a generator. Edit a table,
re-run `scripts/vars_gen.py`, commit both - `make check` runs `check-vars`, which
regenerates into memory and fails if the committed C++ differs.

@warning Intra-site includes are relative (`vars.h`, `generated/vars.gen.h`, and
`../../../vars.h` from inside a program directory); the library is reached as `libbotanik.h`, which resolves
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
enum's codes are. All of it is CSV or TOML, all of it is checked before the
firmware is built, and none of it is C++ you can get subtly wrong.

**Written** is behaviour: what a law does with the numbers. A program's `.cpp` is the
only file where a site decides anything.

## The binding

`site.h` is the firmware class. The descriptor is what the site hands the
library; everything else the base owns.

```cpp
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,     g_command_count,                  // site shell commands
    g_home_fields,  g_home_field_count,  home_refs,   // home-screen readings
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    &g_device_table,                                  // what each unit answers
#endif
};
```

Every pointer is **borrowed** and must outlive the firmware - site statics, not
locals. Any table may be null with count 0.

@note `SiteDescriptor` has no default member initialisers, deliberately. An NSDMI
would stop it being a C++11 aggregate, and the descriptor would no longer
constant-initialise into flash.

The variable table is NOT in the descriptor: the site hands it over by overriding
`register_vars()`, which calls the generated `register_all()`. That function holds
the ordering rule the parameter block depends on, so no site has to remember it.

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
`register_tasks()` - and `programs/order.toml` already knows it. Generating the trio is
an obvious next step and has not been taken; until it is, the three lists are
kept in step by hand and by the site test that walks them.

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

1. edit a table under `tables/`
2. `python3 scripts/vars_gen.py` - validates, then writes `generated/`
3. write or change the law in its own `programs/<stage>/<class>/<class>.cpp`
4. `make -C build check`

Step 2 is where most mistakes are caught: a register collision, a program
producing a row another already produces, a parameter no law reads, a default
outside its own band, a diagram naming a program that is not there. See
@ref int_tables_schema for every column and every rule.
