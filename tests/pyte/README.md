# pyte screen-assertion tests

These tests drive the real `botanik_sim` firmware over a **PTY** and replay its
ANSI output through [pyte](https://github.com/selectel/pyte) to reconstruct the
80x24 screen, then assert it against committed golden fixtures. They catch
terminal-rendering regressions (cursor moves, insert/delete, IL/DL, the line
editor's reconciliation) that a source diff can't.

## Why a PTY, and why time dilation

- **PTY, not a pipe:** `RawSerial::availableForWrite()` uses `ioctl(TIOCOUTQ)`,
  which only answers on a tty. A pipe would bypass the baud throttle and the TX
  back-pressure path we want to exercise.
- **Speed via `BOTANIK_TIME_SCALE` (not a higher baud):** the simulator's clock
  runs N times faster than wall-clock (the harness uses scale 3). Every
  byte/event ratio is identical to a real 4800-baud run; only wall time is
  compressed. See `hal/simulator.h`. Scale 3 leaves ~3ms wall per 10ms tick;
  pushing higher (>=5) makes input/render race at the ~1ms budget and the sim
  starts dropping keystrokes (measured: flaky from scale 4 up, and no wall-time
  win since the per-test `settle`/pump windows are wall-clock, not sim-time). The
  speedup comes from running the tests in parallel instead (see Running), so the
  factor stays at 3.
- **Release sim only:** the tests run against a release, no-sanitizer
  `botanik_sim` (the CMake target builds one in a sub-tree). A debug+ASAN build
  is too slow per dilated tick.
- **`BOTANIK_NO_XRUN_WARNING`:** the harness sets this so the scheduler's
  load-dependent overrun warning (which dilated time provokes) doesn't pollute
  the log-writer pages. Screens are compared on `meaningful_lines` (volatile
  fields normalized, overrun/blank noise dropped) for extra resilience.

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
count is bounded to the core count on purpose: each dilated sim needs a whole
core to keep its per-tick budget, so oversubscribing them reintroduces the
dropped-input flakiness (same failure mode as raising the time scale).

Or directly (needs a release sim binary). Pass the test dir explicitly so pytest
loads this `conftest.py` (and the `--sim`/`--update` options) regardless of CWD:

```sh
pytest tests/pyte --sim build-debug/pyte-sim/botanik_sim   # or set BOTANIK_SIM
```

## Golden fixtures

Expected screens live in `golden/*.txt`. Regenerate them after an intentional UI
change, **review the diff**, and commit:

```sh
make -C build-debug pytest-update
```

This runs the suite with `--update`, which rewrites the goldens from live output
instead of asserting. It is deliberately separate from `check`/`test`.

## Layout

- `conftest.py`  - the PTY/pyte `Term`, the `spawn` and `assert_screen` fixtures.
- `normalize.py` - field-level regex that masks volatile cells (clock, uptime,
  TX/RX depths, cpu/stk, PLC state, log timestamps) before comparison.
- `keys.py`      - input byte sequences matching `terminal/parser.cpp`.
- `helpers.py`   - boot / page-switch helpers.
- `test_*.py`    - home, status, logs, and the shell reconciliation scenarios.
