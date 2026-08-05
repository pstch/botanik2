# Building and testing {#dev_build}

One codebase, two targets, both running the same site verbatim. The only difference
is the HAL.

| Target | Is |
|---|---|
| `SIMULATOR` (default) | an ordinary Linux process: the full UI, logging, scheduler and scan cycle, no hardware |
| `OPTA` | cross-compiled for the STM32H747 M7, producing a flashable image |

- @subpage md_src_2botanik__sim_2README - the simulator harness, and the environment
  variables that compress a run.
- @subpage md_src_2botanik__fw_2README - the OPTA entry point and the ARM build.

## Configure and build

```sh
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build                    # everything
make -C build botanik_sim        # just the simulator
```

Requires CMake 3.10+ and a C++11 compiler. `ccache` is used automatically when
installed, so a clean rebuild replays cached objects in seconds. The repo ships an
`.envrc` exporting `MAKEFLAGS=-j4` and `CTEST_PARALLEL_LEVEL`; without
[direnv](https://direnv.net), pass `-j` yourself.

The OPTA build needs the Arduino Mbed OPTA core, installed once:

```sh
arduino-cli core install arduino:mbed_opta
cmake -B build-opta -DBOTANIK_TARGET=OPTA
make -C build-opta               # -> build-opta/botanik_opta.bin
```

CMake auto-discovers the highest version under `~/.arduino15`; override with
`-DMBED_CORE_ROOT=...` or the `ARDUINO_DATA_DIR` environment variable. Only three
things are taken from that package: the prebuilt `libmbed.a`, the linker script (which
is preprocessed, because it references macros), and the target's `PinNames`.

@warning Two constraints are load-bearing on the OPTA link. `libmbed.a` is built
`-mfloat-abi=softfp` and **mixing float ABIs does not link at all**; and the linker
script must be run through the preprocessor before use. `libmbed.a` is linked
`--whole-archive` to keep the vector table and RTOS entry, then `--gc-sections`
prunes.

## Build options

| Option | Default | |
|---|---|---|
| `BOTANIK_TARGET` | `SIMULATOR` | `SIMULATOR` or `OPTA` |
| `BOTANIK_SITE` / `BOTANIK_SITE_DIR` | `gh_mlb41` | which site to build (see @ref int_site) |
| `CMAKE_BUILD_TYPE` | `DEBUG` | `DEBUG` is `-O0 -g3`; `RELEASE` is `-O3` + LTO, `NDEBUG`, stripped |
| `BOTANIK_SANITIZE` | `address+undefined` on DEBUG SIMULATOR | recoverable, so the interactive sim stays usable. `OFF` to disable |
| `BOTANIK_FEATURE_<X>` | see @ref int_config | `ON`/`OFF` per subsystem |

`scheduler.cpp` is compiled with `-Wdouble-promotion` to keep the per-beat metric
math single-precision.

## Checks

`make check` is the pre-push gate and fails on the first problem:

```sh
make -C build check
```

1. **`botanik_strict`** - a `-Werror` mirror of the library. Plain `make` stays
   warning-tolerant; this fails on any warning.
2. **the doctest and pyte suites**, via CTest - one entry per `TEST_CASE`, plus one
   per pyte test file.

```sh
ctest --test-dir build --output-on-failure          # every entry
ctest --test-dir build -R pyte --output-on-failure  # just the screen tests
```

See @ref testing for the suites themselves.

@note Because each doctest case gets its own process, a test that corrupts state for
later ones **hides** under `ctest`. Run `./build/tests/botanik_tests` directly to put
them all in one process.

### clang-tidy is not in the gate

It is a periodic sweep, run when a batch review is due - not a per-change check.

```sh
make -C build clang-tidy       # incremental: one stamp per TU under build/clang-tidy/,
                               # so only sources whose text or included headers
                               # changed are re-analysed (Makefile generator only)
make -C build clang-tidy-all   # the whole tree, ignoring the stamps
rm -rf build/clang-tidy        # force a full re-lint next time
```

It runs the `bugprone-*` checks from `.clang-tidy` with findings promoted to errors,
and the targets exist only when `clang-tidy` was found at configure time.

## Docs

```sh
make -C build docs        # -> docs/doxygen/html/index.html
make -C build docs-open
```

The target regenerates the LoC / churn / coverage charts (`scripts/code_charts.py`)
and then runs Doxygen. Warnings land in `docs/doxygen/doxygen-warnings.log`.

@note Doxygen's `INPUT` is `src tests docs` with `*.md` in `FILE_PATTERNS`, so **every
markdown file under those trees becomes a page**, whether or not anything links to it.
A new page in `docs/` must be `@subpage`d from its part's hub (`manual.dox`,
`integration.dox`, `development.dox`) or it floats unparented in the tree. Give it an
explicit `{#anchor}` on its title so cross-references stay readable and survive a
rename.
