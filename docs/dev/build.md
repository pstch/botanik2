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
| `CMAKE_BUILD_TYPE` | `DEBUG` | `DEBUG` is `-O0 -g3`; `RELEASE` is `-O3` + LTO, `NDEBUG`, stripped. The OPTA image takes its level from `BOTANIK_OPTA_OPT` instead, on every build type |
| `BOTANIK_SANITIZE` | `OFF` | `address` / `undefined` / `address+undefined` to instrument a whole tree. Off by default because it more than doubles compile CPU; `make check-full` is where it runs |
| `BOTANIK_WERROR` | `ON` | warnings are errors on the library's own sources (PRIVATE, so the sketch and the suite keep `-Wall -Wextra` without it). `OFF` for a WIP tree |
| `BOTANIK_OPTA_OPT` | `-Os` | optimization level for the OPTA image. `-Os` is smallest by a wide margin; see @ref dev_footprint |
| `BOTANIK_OPTA_NANO` | `OFF` | link newlib-nano on the OPTA image: ~32 KB smaller, not yet run on hardware. See @ref dev_footprint |
| `BOTANIK_FEATURE_<X>` | see @ref int_config | `ON`/`OFF` per subsystem |

`scheduler.cpp` is compiled with `-Wdouble-promotion` to keep the per-beat metric
math single-precision.

## Checks

`make check` is the pre-push gate and fails on the first problem:

```sh
make -C build check
```

1. **a warning-clean build** - the library compiles `-Werror` (`BOTANIK_WERROR`),
   so building the suite at all fails on any warning in a library source. This
   replaced `botanik_strict`, a second copy of the whole library compiled purely
   for its diagnostics, which cost 63 duplicate TU compiles every run.
2. **the doctest and pyte suites**, via CTest - one entry per `TEST_CASE`, plus one
   per pyte test file.

The gate runs uninstrumented. `make check-full` is the same gate again under
ASan+UBSan, in a tree of its own under `build/asan/`:

```sh
make -C build check-full
```

Run it before a push that touches memory handling, and periodically otherwise -
the same periodic-sweep treatment clang-tidy gets. Its pyte half is not
instrumented and cannot be: that sim is pinned to `RELEASE` with sanitizers off,
because the screen tests need one that is not sanitizer-slow.

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
rm -rf build/clang-tidy        # force a full re-lint next time
```

It runs the `bugprone-*` checks from `.clang-tidy` with findings promoted to errors,
and the targets exist only when `clang-tidy` was found at configure time.

## Docs

```sh
make -C build docs        # -> docs/doxygen/html/index.html
make -C build docs-open
```

The target runs Doxygen. Warnings land in `docs/doxygen/doxygen-warnings.log`.

@note Doxygen's `INPUT` is `src tests docs` with `*.md` in `FILE_PATTERNS`, so **every
markdown file under those trees becomes a page**, whether or not anything links to it.
A new page in `docs/` must be `@subpage`d from its part's hub (`manual.md`,
`integration.md`, `development.md`) or it floats unparented in the tree. Give it an
explicit `{#anchor}` on its title so cross-references stay readable and survive a
rename.

Every narrative page is markdown - there are no `.dox` files. `# Title {#id}` is what
`@page id Title` used to be, `##` / `###` replace `@section` / `@subsection` (each
still taking a `{#id}`), and every other Doxygen command works unchanged inside a
markdown file. `docs/index.md` is the mainpage by way of `USE_MDFILE_AS_MAINPAGE`
rather than an `@mainpage` command.

### Diagrams

Three kinds, and the choice is mechanical:

| Kind | Written as | Examples |
|---|---|---|
| Flow, structure, timeline | a **draw.io SVG** in `images/`, referenced with `@image html <name>.drawio.svg "caption"` | @ref dev_input, @ref dev_logging, @ref dev_scan |
| Class and relationship | an inline `\startuml … \enduml` block (PlantUML, `PLANTUML_JAR_PATH`) | @ref dev_input, @ref dev_pages |
| A monospace artifact shown as it really renders | a fenced ASCII block | the screen layouts in @ref user_console and @ref dev_pages, the log line in @ref user_logging, the editor rows in @ref dev_editor |

The last row is the one worth stating: a terminal screen mockup, an annotated log
line and the editor's row layout are all **text**, drawn in the same monospace grid
the firmware actually paints. Redrawing those as boxes would lose the thing they
document. Everything that is genuinely a graph of boxes and arrows is a draw.io file.

Each `images/*.drawio.svg` is a plain SVG that **doubles as its own draw.io
document** - the editable model rides in the root element's `content` attribute.
Open one in draw.io or the VS Code extension and save; it re-renders the SVG in
place. There is no export step, no second source file, and nothing to regenerate,
so the file in `images/` is always the only copy. `IMAGE_PATH` is `images`, so
`@image html` finds them by bare filename and Doxygen copies them into the HTML
output.
