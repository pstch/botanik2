# C++ tests {#cpptests}

@brief Native unit tests for the host-runnable parts of @c libbotanik, built on
       [doctest](https://github.com/doctest/doctest).

The framework is a single vendored header (`tests/vendor/doctest.h`), so there
is nothing to fetch. The tests compile and run only in the @c SIMULATOR build:
they link the host `botanik` library and execute natively.

## Building & running {#cpptests_run}

From a configured SIMULATOR build tree:

```sh
cmake -B build -DBOTANIK_TARGET=SIMULATOR   # once
make -C build check                         # build the suite + run it
```

`make check` builds `botanik_tests` and runs it through CTest with failures
echoed inline. Lower-level equivalents:

```sh
make -C build botanik_tests          # just build the binary
./build/tests/botanik_tests          # run directly (doctest CLI)
./build/tests/botanik_tests -s       # ...with every successful check shown
ctest --test-dir build --output-on-failure
```

The raw binary accepts the full doctest command line - `-tc="format_float*"`
to run matching cases, `-ltc` to list them.

## Suites {#cpptests_suites}

The suite covers the site's control laws (`test_site_programs.cpp` per law,
`test_site_scenarios.cpp` end to end), the POU vocabulary (`test_plc.cpp`),
alarms, health, and the site's table integrity. Everything the operator SEES is
@ref termtests instead, which drives the real binary. `-ltc` lists the cases in
the current binary.

Object footprints are pinned at COMPILE time in `libbotanik_sizes.h`, not by a
case here.
