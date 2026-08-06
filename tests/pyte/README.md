# pyte screen-assertion tests

These tests drive the real `botanik_sim` firmware over a **PTY** and replay its
ANSI output through [pyte](https://github.com/selectel/pyte) to reconstruct the
80x24 screen, then assert on that screen. They catch terminal-rendering
regressions (cursor moves, insert/delete, IL/DL, the line editor's
reconciliation) that a source diff can't.

## Why a PTY, and why time dilation

- **PTY, not a pipe:** `RawSerial::availableForWrite()` uses `ioctl(TIOCOUTQ)`,
  which only answers on a tty. A pipe would bypass the baud throttle and the TX
  back-pressure path we want to exercise.
- **Console baud is compile-time only** (`Config::CONSOLE_BAUD`, 4800). The
  terminal derives its per-render byte budget from it at construction, so there
  is deliberately no runtime override: one could only speed the wire up while the
  renderer kept pacing for 4800. To go faster, use time dilation below - it keeps
  every byte/event ratio identical to a real 4800-baud run, which is exactly the
  property the expected screens were captured under.
- **`BOTANIK_SIM_CPU`:** which core the loop thread is pinned to (default 0). A
  property of the dev box, so it is an env var rather than a build setting.
- **Speed via `BOTANIK_TIME_SCALE` (not a higher baud):** the simulator's clock
  runs N times faster than wall-clock (default scale 50). Every byte/event ratio
  is identical to a real 4800-baud run; only wall time is compressed. See
  `hal/simulator.h`. Override per run with `PYTE_TIME_SCALE=N` (env, honored
  through pytest, `ctest`, and `make` alike). The suite is written to tolerate a
  fast-drifting sim, so it stays green far higher (verified 19/19 at scale 500).
- **Resilience to a fast-living greenhouse:** at high scale the mock greenhouse
  (correctly) lives N x faster, so during the harness's wall-clock settle/assert
  windows its state drifts - alarms cycle, `SYS_RST` clears, the boot log scrolls
  under the alarm flood, mock values move. The assertions treat that as volatile:
  `normalize.py` masks the live alarm bar / active count / drifting fields and
  drops `ALARM_*` log spam; a `rows=`/`clip=` band compares only the region a
  test is about (home tests assert the widget, not the live log tail); the
  log/alarm-catalog tests assert behaviour/structure rather than pinning churn.
- **Determinism vs. async boot chatter:** the network handshake (DNS -> NTP ->
  MQTT, which retries on a missed reply) logs at timing-dependent moments. If it
  is still in flight when a test leaves the Home page, a late line lands in the
  Shell page's live log tail. `boot()` waits for the handshake's final markers
  before returning (`helpers.py`), which is
  what makes high scales flake-free; a plain `settle()` quiet window is not
  enough (it can return in a gap mid-handshake).
- **Release sim only:** the tests run against a release, no-sanitizer
  `botanik_sim` (the CMake target builds one in a sub-tree). A debug+ASAN build
  is too slow per dilated tick.
- **Comparison, not equality:** screens are compared on `meaningful_lines`
  (volatile fields normalized, blank and alarm-transition rows dropped) and, for
  a whole-screen assert, with the live log tail dropped on top of that. A stray
  repaint or an unrelated log line then cannot fail a correct screen.

## Dependencies (manual - the build never installs anything)

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
```

Then point CMake at that interpreter when configuring (or just have it active):

```sh
cmake -B build-debug -DBOTANIK_TARGET=SIMULATOR -DPython3_EXECUTABLE="$(which python3)"
```

If pyte/pytest aren't importable at configure time, CMake skips the suite (it
never fails configure and never installs).

## Running

```sh
ctest --test-dir build-debug -R pyte --output-on-failure   # via CTest
make -C build-debug check                                  # part of the gate
```

Each test function is its own CTest entry (`pyte:<file>::<func>`, discovered at
configure time), so `ctest -j` runs them concurrently - this is where the wall-
time win comes from. The `.envrc` exports `CTEST_PARALLEL_LEVEL=$(nproc)` so a
bare `ctest` parallelizes; `make check` passes the same `-j` explicitly. The job
count defaults to the core count: each dilated sim is happiest with a core to
itself, so this avoids piling more sims onto the box than it can run at once.

Or directly (needs a release sim binary). Pass the test dir explicitly so pytest
loads this `conftest.py` (and the `--sim`/`--update` options) regardless of CWD:

```sh
pytest tests/pyte --sim build-debug/pyte-sim/botanik_sim   # or set BOTANIK_SIM
```

## Every page has a display test

**Standing rule: each page registered in `BotanikFirmware::register_pages()` has
one settled-screen golden, in `test_pages.py`.** Its `PAGES` table is the list;
adding a page means adding a row and regenerating. `test_pages.py` also asserts
this against the running firmware - it reads the status bar's tab strip, which
terminal.cpp draws from the page list, and fails if a tab is there that `PAGES`
does not cover. So a new page cannot quietly arrive without a display test.

The suite walks all six pages in **one** sim: booting the firmware and waiting
out its network handshake is the expensive part of a screen test, and paying it
per page would make this the most costly thing in the gate. Nothing in
`test_pages.py` mutates state, so the pages cannot contaminate each other;
anything that edits, resets or overrides uses its own `spawn`.

## What a golden is for, and what it is not

A golden pins **layout**: which rows a page draws, in what order, with what
labels, columns, rules and chrome. It does not pin **values** - `normalize.py`
masks every cell the greenhouse moves (readings, clock, uptime, load, alarm
state, quality) - because those are what a page exists to show live, and pinning
them is what made an earlier generation of goldens flaky enough to delete.

So: a renamed column, a dropped panel, a shifted table all fail here. A warmer
afternoon does not.

That division is why the earlier passes' reasoning does not apply to these. They
deleted 24-line fixtures that spent 23 lines restating chrome to pin one to
three rows of payload - a *behavioural* assertion wearing a screenshot's
clothing. These pin the chrome deliberately, because the chrome is the thing
under test, and nothing else in the suite covers it.

Behaviour still belongs inline, next to the input that produced it:
`helpers.assert_editor(term, *rows)` for the shell reconciliation scenarios, and
`wait_for()` when the assertion is about content appearing (a log line, a
committed value). The per-page files next door do exactly this and no longer
restate the chrome `test_pages.py` covers.

Two pages are honestly thin, and the reason is worth knowing:

- **LOGS** is nothing *but* the live scrollback, and `drop_log_lines` removes
  log entries (and their wrapped `>` continuation rows) from both sides of the
  comparison - so its golden pins the frame alone. Freezing the log would need a
  knob the firmware does not have; content is asserted in `test_logs.py`.
- **ALARMS** is pinned in its **ALL** view, not the default RECENT one. RECENT
  orders active-first via `_before()`, so both its row set and their order move
  with the fast greenhouse; `alarms.h` returns before that sort for `MODE_ALL`,
  which leaves the fixed catalog in table order.

## Regenerating

```sh
make -C build pyte-sim        # goldens run a STALE sim otherwise
make -C build pytest-update
```

This runs the suite with `--update`, which rewrites the goldens from live output
instead of asserting. It is deliberately separate from `check`/`test` - the gate
never rewrites its own expectations. **Review the diff before committing**:
`--update` writes the full snapshot including the `ALARM_*` noise lines that are
only dropped at compare time, so strip anything matching `normalize._NOISE` from
the goldens to keep the artifacts clean.

Regeneration deliberately runs at `PYTE_TIME_SCALE=6` while the assert suite
defaults to 50, so captures are low-drift.

One thing the comparison does not catch: `meaningful_lines` drops blank lines,
so a golden compares a *sequence* of non-blank rows rather than their absolute
positions. A regression that only changes vertical spacing passes.

## Layout

- `conftest.py`  - the PTY/pyte `Term`, the `spawn` / `spawn_module` factories
  and the `assert_screen` fixture.
- `normalize.py` - field-level regex that masks volatile cells (clock, uptime,
  cpu/stk, home readings, log timestamps) plus the geometry-based mask for the
  MONITOR/ALARMS live table columns, before comparison.
- `keys.py`      - input byte sequences matching `console/parser.cpp`.
- `helpers.py`   - boot / page-switch helpers, and `assert_editor`.
- `test_pages.py` - **the per-page display goldens**, and the invariant that
  every registered page has one.
- `test_*.py`    - the per-page *behaviour*: home reboot, log marks, alarm view
  cycling, monitor scrolling, param editing, shell commands and the shell
  reconciliation scenarios.
