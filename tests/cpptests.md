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

One `tests/test_<subsystem>.cpp` per subsystem, named after the part of
`src/libbotanik/` it covers. `-ltc` lists the cases in the current binary.

## Object sizes {#cpptests_sizes}

The OPTA core is RAM-constrained, so the suite prints the size of the big
long-lived structures on every run and fails only if one grows past a generous
budget. The page sizes are dominated by config-driven line/log buffers, so they
track the `Config::*` buffer knobs rather than code changes, and the host
figures differ from the ARM target's (narrower pointers there).

Run `./build/tests/botanik_tests -s` for the current numbers.
