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
- **`BOTANIK_CONSOLE_BAUD`:** overrides the sim's console baud (default 4800)
  for smoketesting - it drives both the simulated UART's write throttle and
  `present()`'s per-tick byte budget. The suite deliberately does *not* use it
  (see the next bullet): changing the baud changes the byte/event ratios the
  expected screens were captured at, whereas time dilation does not. Reach for
  it when hand-driving `botanik_sim`, not when writing a test. 0/junk keeps 4800.
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

## Expectations: goldens, and when not to use one

A golden earns its keep only where the expected screen is genuinely a *picture*
- a lot of stable text no one would want to hand-write. That is the Home widget,
and it is the only golden left (`golden/home_boot.txt`). Regenerate it after an
intentional UI change, **review the diff**, and commit:

```sh
make -C build-debug pytest-update
```

This runs the suite with `--update`, which rewrites the goldens from live output
instead of asserting. It is deliberately separate from `check`/`test`.

Everywhere else, assert inline. Once `normalize.py` has masked the volatile
cells and `meaningful_lines` has dropped the blank and log rows, most screens
reduce to two rows of chrome plus one to three rows of payload - so a 24-line
fixture spent 23 lines restating the same constants to pin the row the test was
actually about, and every unrelated UI tweak dragged all of them through a
regen. The shell reconciliation tests use `helpers.assert_editor(term, *rows)`
instead: the expected rows are spelled out in the test, next to the input that
produced them. Prefer that. Reach for `wait_for()` when the assertion is about
content that appears (a log line, a table header) rather than a whole screen.

## Layout

- `conftest.py`  - the PTY/pyte `Term`, the `spawn` and `assert_screen` fixtures.
- `normalize.py` - field-level regex that masks volatile cells (clock, uptime,
  cpu/stk, home readings, log timestamps) before comparison.
- `keys.py`      - input byte sequences matching `console/parser.cpp`.
- `helpers.py`   - boot / page-switch helpers, and `assert_editor`.
- `test_*.py`    - home, logs, alarms, monitor, params, shell commands, and the
  shell reconciliation scenarios.
