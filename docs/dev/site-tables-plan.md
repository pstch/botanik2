# Plan: the site's tables come back as data

Status: **agreed in shape, not started.** Written 2026-08-31. Nothing below
happens until the ST migration is finished - the program table has a second
consumer that does not exist yet.

## Why now, having retired the last one

`scripts/` and every CSV and TOML were deleted on 2026-08-24, and the tables
became ordinary hand-edited C++. That was the right call at the time: a
generator that runs at BUILD time is a dependency every contributor pays for,
and the checks it did were better expressed as tests. `tests/test_site.cpp`
still does them.

Two things have changed.

**We already ship generated code.** `iec2c` turns `.st` into C, and its output
is committed under `src/sites/gh_mlb41/st/generated/`. So the pattern is
settled: **generate offline, commit the result, no build step.** That is the
half of the old generator that hurt, and it is not coming back.

**The program table now has two consumers.** The per-program dispatch design
needs a C++ table of `{POU, instance, period}` while `plant.st` separately
declares the same programs to matiec. One of those has to be derived from the
other, or they drift. A program CSV generating both removes the duplication
rather than adding one.

## What the site becomes

```
src/sites/gh_mlb41/
  site.toml            <- the root: metadata, config overrides, table paths
  tables/*.csv         <- the tables, by convention
  generated/           <- committed output, never edited
  programs/<stage>/<name>/<name>.st
  blocks/<name>/<name>.st
```

`site.toml` carries, all paths relative to the site directory:

- site metadata: name, version
- config overrides, which is `config.h` today
- one path per table: alarms, params, modbus in, modbus out, analog in,
  analog out, digital in (with pulse inputs), digital out, status variables,
  analog calibration, programs

## What it generates, and what it does not

| generated | from |
|---|---|
| the `Descriptor` arrays and their counts | the ten row tables |
| `register_all()`, in the order the bank requires | the same |
| the `AnalogCal` blocks and each row's pointer into them | the calibration table |
| `config.h` | the `[config]` section of the TOML |
| the C++ program dispatch table | the program table |
| `plant.st`'s `CONFIGURATION`: `VAR_GLOBAL` and the `PROGRAM` instances | the program table plus the rows each POU declares |

**Not generated, and this is the point:** every `.st` file. A program's LAW is
written by a person and lives beside its C++ ancestor. The tables say what
exists; the ST says what it does.

## The invariants a generator inherits

These are load-bearing today and `tests/test_site.cpp` checks them. The
generator must produce them, and the tests stay as the check:

- **Parameters are contiguous** - the `ROLE_PARAM` run is what the params page
  and the retain block walk.
- **Library rows come first**, and their parameters last within that block.
  Registers 900-999 are the library's; a site tunes one by REDECLARING it, and
  `freeze()` keeps the last declaration.
- **Registers are unique**, across all ten tables.
- **`LIBRARY_ROWS`, `PARAMS_COUNT`, `ROWS_COUNT` agree with the arrays.** Three
  hand-kept numbers today, and the reason a row added in the middle silently
  truncated the table twice this month.
- **An `AnalogCal` is SHARED** by the rows that point at it, and `scale`/
  `offset` are derived rather than stored.
- **A `_qlt` companion's base row exists**, and its transport decides whether
  the bank mirrors it or a program writes it. Worth generating both the
  companion row and its entry in whichever list.

## Staging

1. **The ST migration finishes.** The program table's shape is not knowable
   until every program is ST and the dispatch design has settled.
2. **The generator, offline, output committed.** Round-trip it against the
   CURRENT hand-written tables first: generate, diff, and require the diff to
   be empty before deleting anything. That is the whole safety argument.
3. **`site.toml` and `tables/` land, `vars.cpp` goes.** 682 lines of C++ for
   455 rows becomes ten CSVs.
4. **`plant.st`'s configuration section becomes generated**, which is when the
   duplication between it and the dispatch table actually disappears.
5. **`config.h` last**, because it is the one with real prose in it - the
   comments explaining WHY a limit is what it is have to survive as TOML
   comments or move to `docs/`.

## Open questions

- **What language is the generator in?** It runs offline, so the old objection
  is gone, but a Python script is still a thing a contributor has to have. A
  small C++ tool built by the same CMake is heavier to write and lighter to
  run. Undecided.
- **Where do the row COMMENTS go?** `vars.cpp` carries a `desc` and a `notes`
  per row, and both are read on the params page. They are already data and move
  to CSV columns cleanly. But the comments BETWEEN rows - the ones explaining a
  block of registers - have no column.
- **Does the site keep any hand-written C++ at all?** After this, `site.h` is
  a `SiteDescriptor` and the simulated field. Possibly nothing else.
- **How is the round-trip kept honest afterwards?** Step 2 diffs once. A test
  that regenerates and diffs on every run would need the generator at build
  time, which is what we are avoiding. Probably a `make` target somebody runs,
  plus the existing invariant tests.
