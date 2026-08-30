# The matiec experiment

`destrat_controller`, written as ST and compiled by matiec, driving a real
`Vars::Bank` through a replaced `accessor.h`. It works.

## Running it

matiec is not vendored. Build it once (flex and bison come from `apt-get
download` + `dpkg-deb -x`, no sudo needed), then:

    iec2c -I <matiec>/lib -T out st/destrat.st

    g++ -std=c++11 -DTARGET_SIMULATOR -DDEBUG -D_GNU_SOURCE \
        -DBOTANIK_SITE_CONFIG='"<abs>/src/sites/gh_mlb41/config.h"' \
        -DBOTANIK_SITE_HEADER='"<abs>/src/sites/gh_mlb41/site.h"' \
        -I src -I . -I st/shim -I out \
        -o harness st/shim/harness.cpp build/libbotanik.a && ./harness

The site config defines MUST match the ones `libbotanik.a` was built with.
They size `Vars::Bank`, so a mismatch is an ODR violation that shows up as a
bank whose rows all read as the first one - it cost an hour here.

## What is in it

| | |
|---|---|
| `destrat.st` | the program, an FB and a CONFIGURATION, in plain ST |
| `shim/accessor.h` | matiec's seven accessor macros, over `Vars::Ref` |
| `shim/iec_std_lib.h` | the IEC type names, and nothing else of its runtime |
| `shim/harness.cpp` | a bank, a `bind()`, and four cases through the body |
| `out/` | what `iec2c` emitted, committed so the shim can be read against it |
