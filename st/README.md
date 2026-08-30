# The ST toolchain

The `.st` sources are NOT here. They live beside the C++ they replace:

| POU | Where |
|---|---|
| library blocks | `src/libbotanik/plc/blocks/<file>.st`, named for the header holding the C++ block |
| site blocks | `src/sites/gh_mlb41/blocks/<name>/<name>.st` |
| site programs | `src/sites/gh_mlb41/programs/<stage>/<class>/<class>.st` |

This directory holds what turns them into C: the accessor shim, a cut-down
`iec_std_lib.h`, and one worked harness.

## The target

**matiec generates the POU BODIES. The variable descriptor tables stay ours.**

ST names a variable. What a name means - its unit, band, digits, register,
transport, device, word order, timeout, poll period, enum renderer, role and
scope - is the `Descriptor` table's, and matiec never sees any of it. IEC's
`VAR_GLOBAL` carries a type and an initial value and stops there, which is
about a tenth of what a row here carries.

`shim/accessor.h` is the whole join, 41 lines. matiec emits every read as
`__GET_VAR(name)` and every write as `__SET_VAR(prefix, name, suffix, value)`,
so redefining that handful of macros points the generated C at
`Vars::Bank` instead of at a `{value, flags}` struct.

## Building it

matiec is not vendored. Build it once (flex and bison come from `apt-get
download` + `dpkg-deb -x`, no sudo needed), then concatenate the blocks a POU
uses ahead of it - matiec has no include - and compile:

    cat src/libbotanik/plc/blocks/timers.st \
        src/sites/gh_mlb41/programs/actuators/roof_lockout/roof_lockout.st > /tmp/rl.st
    iec2c -I <matiec>/lib -T out_roof /tmp/rl.st

    g++ -std=c++11 -DTARGET_SIMULATOR -DDEBUG -D_GNU_SOURCE \
        -DBOTANIK_SITE_CONFIG='"<abs>/src/sites/gh_mlb41/config.h"' \
        -DBOTANIK_SITE_HEADER='"<abs>/src/sites/gh_mlb41/site.h"' \
        -I src -I . -I st/shim -I out_roof \
        -o harness st/shim/harness_roof.cpp build/libbotanik.a && ./harness

The site config defines MUST match the ones `libbotanik.a` was built with.
They size `Vars::Bank`, so a mismatch is an ODR violation that shows up as a
bank whose rows all read as the first one - it cost an hour here.

## The harness

`shim/harness_roof.cpp` binds matiec's generated `ROOF_LOCKOUT_data__` against
a real `Vars::Bank` and runs the body. It is the worked example because
`roof_lockout` is the law that needs the most from the join:

- a nested FB instance (`TOF_MS`), with the three anonymous rows per instance
  that its implicit EN/ENO and private VAR cost
- three standard-library calls, which is what `shim/iec_std_lib.h` carries
- the two `_qlt` companions, which is the ONLY way a quality decision survives
  into ST - see `docs/dev/quality-companions.md`

Neuter its `blind` and the two dead-station cases come back `cap 1.00`, roof
wide open. That is the property the whole companion design exists for.

## What is still open

The CONFIGURATION section maps onto `bind()` almost exactly - `VAR_GLOBAL` is
the bank, `WITH <task>` is the rate group, and a formal-parameter connection is
a two-byte `VarRef` copy. Generating `bind()` from it is the next step, and
what would delete the hand-written binding table in the harness.
