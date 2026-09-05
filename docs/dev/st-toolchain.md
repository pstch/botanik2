# The ST toolchain

What turns the site's `.st` into the C that is committed under
`src/sites/<site>/generated/`. The four `matiec-*.md` notes beside this file,
and `st-migration.md`, are dated records of the port; this one is the current
state.

## Where the pieces are

| | |
|---|---|
| library blocks and functions | `src/libbotanik/plc/{blocks,functions}/*.st` |
| our IEC library | `src/libbotanik/plc/functions/ieclib.txt` |
| the join onto `Vars::Bank` | `src/libbotanik/plc/runtime/accessor.h` |
| the types matiec's C names | `src/libbotanik/plc/runtime/iec_std_lib.h` |
| the site's enumerated types | `src/sites/<site>/generated/enums.st` + `enums.gen.h` |
| site blocks | `src/sites/<site>/blocks/<name>/<name>.st` |
| site programs | `src/sites/<site>/programs/<stage>/<class>/<class>.st` |
| the compiler | `vendor/matiec/`, pinned - see its `UPSTREAM` |
| the CMake targets | `cmake/matiec.cmake`, `cmake/matiec-build.cmake` |

## The boundary {#dev_st_boundary}

**matiec generates the POU BODIES. The variable descriptor tables stay ours.**

ST names a variable. What a name MEANS - its unit, band, digits, register,
device, word order, timeout, poll period, enum renderer and kind - is the
`Descriptor` table's, and matiec never sees any of it. IEC's `VAR_GLOBAL` carries
a type and an initial value and stops there, which is about a tenth of what a row
here carries.

A row is not resolved at bring-up either. `__INIT_EXTERNAL` binds a generated
INDEX - `ROW_CLI_TEMP_HI`, which `plant.cpp` defines from
`Vars::Row::cli_temp_hi` - so a name no table declares fails to COMPILE.

`accessor.h` is the whole join. matiec emits every read as `__GET_VAR(name)`
and every write as `__SET_VAR(prefix, name, suffix, value)`, so redefining that
handful of macros points the generated C at `Vars::Bank` instead of at a
`{value, flags}` struct.

`ieclib.txt` is the other half of it, and the reason `-I` never points at
matiec's own `lib/`: matiec loads `<dir>/ieclib.txt` unconditionally, and its
own declares 28 function blocks under names we use for our own - `TON`, `PID`,
`INTEGRAL` among them - with bodies that are GPL. Ours declares the standard
FUNCTIONS with our bodies and no block at all, so every block in the firmware is
one of ours and nothing of matiec's reaches the image.

## Enumerated types, on both sides {#dev_st_enums}

An enumerated code is declared ONCE, in `tables/enums.csv`, and generated into
two languages that are the SAME type:

| | |
|---|---|
| `generated/enums.st` | `TYPE DayPhase : (PHASE_NIGHT, PHASE_COLD_BANK, ...); END_TYPE`, compiled ahead of the blocks and the laws |
| `generated/enums.gen.h` | the C++ `enum`, its `_label` converter, the `BOTANIK_ST_TYPE_*` block, and a `RefIo<>` so a `Ref` of one reaches the row |

So a law compares against the WORD:

```
IF cli_phase = PHASE_COLD_BANK AND cli_coldBankArm THEN ...
IF mode = IRR_FILLING THEN fill_at := irr_inTotal_L; END_IF;
```

matiec numbers an implicit enumeration 0,1,2... in declaration order and rejects
explicit values, so **the order IS the wire contract**: a member may be appended,
never moved. The generator refuses a set that is not contiguous from zero, since
the codes are also a Modbus register's contents.

matiec's own member list is DISCARDED. `__DECLARE_ENUMERATED_TYPE(name, ...)`
expands to `BOTANIK_ST_TYPE_<NAME>`, which declares each member as a named
constant of the real C++ type - so `DAYPHASE__PHASE_DAY` in the generated C IS
`Site::PHASE_DAY`, and a member added on one side and not the other fails to
compile. `QUALITY` works exactly this way, from `plc/runtime/quality.gen.h`.

@warning matiec UPPER-CASES every identifier, so an enumerated type and a program
INSTANCE that differ only in case are one name for two things. matiec says only
"invalid program name", about the RESOURCE, three thousand lines away from the
cause; `tables.py plant` refuses the pair up front and names it.

## The compiler

matiec is VENDORED at `vendor/matiec`, pinned to one upstream commit. See
`vendor/matiec/UPSTREAM` for which, what was removed from it, and why. It is
pinned because its output is COMMITTED: nothing else would notice two developers
regenerating `POUS.c` with different compilers.

    make -C build matiec            # build it: build/matiec/iec2c
    python3 scripts/tables.py plant # regenerate, in place
    make -C build check-generated   # ...or regenerate into a copy and diff

`check-generated` is part of `check-full` and covers every committed artefact,
not just matiec's: it re-runs `library`, `generate`, `config`, `plant` and
`points` into a scratch copy and diffs. It is what catches a generated file
hand-edited, a table changed without a regeneration - and, because it is made
against the PINNED compiler, a matiec that is not the pinned one.

Building it needs **flex, bison, autoconf and automake** - upstream ships
`configure.ac` and no `configure`, so `autoreconf` runs first. flex and bison
come from `apt-get download` + `dpkg-deb -x`, no sudo needed. Where any of the
four is missing the three targets are not declared at all, and the ordinary
build is unaffected.

`scripts/tables.py plant` finds `build/matiec/iec2c` by itself; `IEC2C=` points
it elsewhere.

## One POU on its own

matiec has no include, so compiling a single law by hand means concatenating the
blocks it uses ahead of it:

    cat src/libbotanik/plc/blocks/timers.st \
        src/sites/gh_mlb41/programs/actuators/roof_lockout/roof_lockout.st > /tmp/rl.st
    build/matiec/iec2c -I src/libbotanik/plc/functions -T /tmp/out /tmp/rl.st

Linking the result against `libbotanik.a` needs the site's own config defines to
MATCH the ones the library was built with - they size `Vars::Bank`, so a
mismatch is an ODR violation that shows up as a bank whose rows all read as the
first one.
