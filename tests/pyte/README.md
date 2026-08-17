# pyte screen-assertion tests

These drive the real `botanik_sim` firmware over a **PTY** and replay its ANSI
output through [pyte](https://github.com/selectel/pyte) to reconstruct the 80x24
screen, then assert on that screen. They catch rendering regressions (cursor
moves, IL/DL, the line editor's reconciliation) that a source diff cannot.

## What belongs here

Only what needs a real terminal or a real process: the byte stream and its
reconciliation, page layout (gutters, wrap columns, chrome), state crossing a
reboot, and that the task table registered at all.

Everything else goes in `tests/test_console.cpp` against the component directly -
dispatch and usage through `CmdFixture`, the editor and kill ring through
`ShellFixture`, scrollback and table geometry through their own classes. A PTY
adds nothing to a claim about a selection index and costs ~200 ms to make it.

## The setup

- **PTY, not a pipe:** `RawSerial::availableForWrite()` uses `ioctl(TIOCOUTQ)`,
  which only answers on a tty. A pipe would bypass the baud throttle and the TX
  back-pressure the tests exist to exercise.
- **The tests drive the clock.** The sim advances one beat per command over a
  control pipe (`BOTANIK_SIM_CLOCK=manual`), so a wait is bounded in BEATS
  rather than host seconds: the same run gives the same screen whatever else the
  box is doing, and a failure costs the beats instead of waiting 15 s to see
  whether more output was coming.
- **The watchdog is off.** The clock is manual, so a case that stops running
  beats looks wedged; an armed watchdog would re-exec the sim under its own PTY.
  `test_watchdog.py` is the exception - it passes `watchdog=<ms>` to arm it with
  a short window and assert the reset actually happens.
- **Never a higher baud.** Console baud is compile-time
  (`console_baud`, 4800 by default) and the terminal derives its per-render byte
  budget from it, so raising the wire speed alone would leave the renderer pacing
  for 4800. Run more beats instead: every byte/event ratio then matches a real
  4800-baud run exactly.
- **The watchdog is live.** It arms at its real 10 s, which is 1000 beats, so a
  firmware that stops kicking gets caught here rather than only on hardware.
- **The tree's own sim** (`build/botanik_sim`), built by a plain `make -C build`.
  Under `check-full` it is instrumented like everything else, so each beat costs
  more host time - but the waits are bounded in beats, not seconds, so the suite
  gets slower without getting flakier. Nothing to retune.
- **A fast-living greenhouse is the point, and the hazard.** At scale 50 the mock
  plant lives 50x faster, so alarms cycle and the log tail floods while a test
  looks at the screen. `normalize.py` masks the cells that move, `drop_log_lines`
  takes the live tails out of a golden compare, and a `rows=`/`clip=` band
  narrows an assertion to the region it is about.

## Waiting

Every wait is the same loop, `Term.until(done, max_beats)`: run the sim in steps
of `BEATS_PER_STEP` beats until the predicate says yes, or `max_beats` have
passed. `settle()`, `wait_for()` and `wait_screen()` are each one predicate over
it, and the golden compare is `settle()` plus a diff.

| call | question |
|---|---|
| `wait_for(needles)` | has this string **ever** been on screen (latched as it went by, so a scrolled-away log line still counts) |
| `wait_screen(needle, present)` | is it on screen **now**, or has it left |
| `settle()` | has the repaint finished - the screen held still across two steps |

**The bound is BEATS, not seconds.** The sim runs only when the harness says so,
so a wait cannot be starved by a loaded box: the same run gives the same screen
every time, and a failing wait costs `MAX_BEATS` of simulated time rather than
15 s of real waiting to see whether more output was coming. Override the step and
the ceiling with `BOTANIK_PYTE_BEAT_STEP` / `BOTANIK_PYTE_MAX_BEATS`.

The one real-time bound left is `ACK_TIMEOUT_S`, and it is a liveness check
rather than a budget: it bounds the round-trip of a single `RUN n` command, so
firing it means the sim is **wedged**, not slow.

Wall-clock bounds were tried first (an idle heuristic plus a hard cap) and
neither works: idle cannot tell a stuck firmware from a starved one, and without
a cap a page with a live log tail repaints forever.

**Prefer content sync to `settle()`.** `settle()` waits out the whole 4800-baud
dribble; a content wait stops at the first frame that says yes. Reach for it only
when the test cannot name what it is waiting to see - capturing a golden, mostly.

**`wait_for` is latched**, which is a trap as much as a feature: a marker seen
earlier answers instantly, silently making an assertion vacuous (`wait_for
("saved")` after `^O` passed because the untouched note already read "saved").
When the question is about the screen *now*, use `wait_screen()`. And never
`assert not wait_for(...)` - it pumps the full `MAX_BEATS` budget before saying
no; read `snapshot()` instead.

`Term.view()` is the normalized screen, cached until the next byte arrives.
Normalizing costs ~1 ms, and every wait wants the same answer two or three times
per iteration, so go through it rather than re-normalizing `screen.display`.

## Dependencies (manual - the build never installs anything)

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cmake -B build -DBOTANIK_TARGET=SIMULATOR -DPython3_EXECUTABLE="$(which python3)"
```

If pyte/pytest are not importable at configure time, CMake skips the suite; it
never fails configure and never installs.

## Running

```sh
ctest --test-dir build -R pyte --output-on-failure
make -C build check                                  # part of the gate
pytest tests/pyte --sim build/botanik_sim            # or set BOTANIK_SIM
```

One CTest entry per test **file** (a glob at configure time). Per-function
entries each paid ~1.5 s importing pytest to run ~0.4 s of test; per-file keeps
failures attributable and the `-j` slots busy, measured 2.5x faster on 4 cores.
`.envrc` exports `CTEST_PARALLEL_LEVEL=$(nproc)`.

Because a CTest entry is a whole file, a file may share one booted sim across its
tests (`spawn_module`). `test_shell_reconcile` does; it resets state between tests
(`^U`+`^L`, or a freshly submitted line). Anything that **mutates** - an edit, a
reset, a persisted override or note - keeps its own `spawn`. It also pays to keep a one-assertion
test out of a file of its own: each entry costs an interpreter startup, which is
why the odds and ends live together in `test_page_behaviour.py`.

A boot costs ~0.02 s, and syncs on the status bar's tab strip - chrome, always on
screen. The network handshake behind it is **not** slow: measured, it spans
0.14-0.35 sim-seconds and did not miss once in 80 boots, including under 8x CPU
load (the mock responder binds its sockets before its thread starts, see
`botanik_sim/network.h`).

## Never sync on a log line the test does not own

The console is a throttled 4800-baud wire and the greenhouse lives 50x faster
than real time, so the log tail is a **firehose that drops what it cannot
paint**. Anything that only ever appears there can go unseen on a completely
healthy firmware, and the busier the branch, the likelier that is.

This bit twice, and both are worth remembering because the symptom looks like a
broken firmware rather than a broken test:

- `boot()` used to wait for "NTP synced" and "MQTT connected". On a branch whose
  modbus probe flaps once a sim-second, the alarm churn scrolled them past
  unpainted and **every test in the suite failed** - against a firmware that,
  asked directly with `net`, reported `online / synced / ready`. Boot now syncs
  on chrome, and `test_network_comes_up` asks `net`.
- A shell command's reply competes with that same firehose for the same wire, so
  `shell(spawn, quiet_logs=True)` runs `logs off` first. Any test asserting on a
  command's output wants it. It is not the default: it puts two lines in the
  transcript, which `test_shell_reconcile` counts as content.

The rule: sync on chrome, on the page's own widgets, or on output you asked for.

## Goldens

`page_home` is the only one, asserted by `test_page_behaviour.py`. There used to
be one per page; they cost ~10 s of the gate and a full-screen golden is an
expensive way to notice a renamed column.

A golden pins **layout** - which rows, in what order, with what labels, columns
and chrome - not **values**, which `normalize.py` masks. A renamed column or a
shifted table fails here; a warmer afternoon does not. Two caveats:
`meaningful_lines` drops blank lines, so pure vertical-spacing regressions pass;
and a mask inside a bordered table must be **width-preserving** or the row stops
matching (`_mask_table_cell` and `<ALARM_TS>` both pad).

## Regenerating

```sh
make -C build                 # goldens capture a STALE sim otherwise
make -C build pytest-update
```

Deliberately separate from `check`/`test` - the gate never rewrites its own
expectations.
**Review the diff before committing**: `--update` writes the full snapshot
including the `ALARM_*` noise lines that are only dropped at compare time, so
strip anything matching `normalize._NOISE`.

## Layout

- `conftest.py`   - the PTY/pyte `Term` (`poll` and its four predicates), the
  `spawn` / `spawn_module` factories, the `assert_screen` fixture.
- `normalize.py`  - the masks for volatile cells, by regex and by table geometry.
- `keys.py`       - input byte sequences matching `console/parser.cpp`.
- `helpers.py`    - `boot` / `goto` / `shell`, and `assert_editor`.
- `test_*.py`     - home reboot and network bring-up, the task table, param
  editing, notes, the cross-page kill ring, and shell reconciliation.
