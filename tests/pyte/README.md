# pyte screen-assertion tests {#dev_pyte}

These drive the real `botanik_sim` over a PTY and replay its ANSI output
through [pyte](https://github.com/selectel/pyte) to reconstruct the 80x24
screen, then assert on that screen. They catch rendering regressions - cursor
moves, IL/DL, the line editor's reconciliation - that a source diff cannot.

## What belongs here

Only what needs a real terminal or a real process: the byte stream and its
reconciliation, page layout (gutters, wrap columns, chrome), state crossing a
reboot, and that the task table registered at all.

Everything else goes in `tests/test_console.cpp` against the component
directly. A PTY adds nothing to a claim about a selection index and costs
~200 ms to make it.

## The setup

- **A PTY, not a pipe.** `RawSerial::availableForWrite()` uses
  `ioctl(TIOCOUTQ)`, which only answers on a tty. A pipe bypasses the baud
  throttle and the TX back-pressure these tests exist to exercise.
- **The tests drive the clock.** The sim advances one beat per command over a
  control pipe (`BOTANIK_SIM_CLOCK=manual`), so a wait is bounded in beats
  rather than host seconds. The same run gives the same screen whatever else
  the box is doing.
- **The watchdog is off** (`BOTANIK_WATCHDOG=0`). The clock is manual, so a
  case that stops running beats looks wedged and would be re-executed under its
  own PTY. `test_watchdog.py` passes `watchdog=True` to arm it and assert the
  reset happens; the window is the firmware's real 10 s, which costs about a
  second of real time because it is measured on the virtual clock.
- **Never raise the baud.** Console baud is compile-time (`console_baud`, 4800)
  and the terminal derives its per-render byte budget from it, so a faster wire
  alone would leave the renderer pacing for 4800. Run more beats instead: every
  byte/event ratio then matches a real 4800-baud run.
- **The tree's own sim** (`build/botanik_sim`), built by a plain `make -C
  build`. Under `check-full` it is instrumented, so each beat costs more host
  time; the waits are bounded in beats, so the suite gets slower without
  getting flakier.
- **The mock plant lives 50x faster than real time.** Alarms cycle and the log
  tail floods while a test looks at the screen. `normalize.py` masks the cells
  that move, `drop_log_lines` takes the live tails out of a golden compare, and
  a `rows=`/`clip=` band narrows an assertion to the region it is about.

## Waiting

Every wait is `Term.until(done, max_beats)`: run the sim in steps of
`BEATS_PER_STEP` beats until the predicate says yes, or `max_beats` have
passed. `settle()`, `wait_for()` and `wait_screen()` are each one predicate
over it, and the golden compare is `settle()` plus a diff.

| call | question |
|---|---|
| `wait_for(needles)` | has this string ever been on screen (latched, so a scrolled-away log line still counts) |
| `wait_screen(needle, present)` | is it on screen now, or has it left |
| `settle()` | has the repaint finished - the screen held still across two steps |

The bound is beats, not seconds, so a wait cannot be starved by a loaded box.
Override the step and the ceiling with `BOTANIK_PYTE_BEAT_STEP` /
`BOTANIK_PYTE_MAX_BEATS`. The one real-time bound left is `ACK_TIMEOUT_S`, a
liveness check on the round-trip of a single `RUN n` command: firing it means
the sim is wedged, not slow.

Prefer a content wait to `settle()`, which waits out the whole 4800-baud
dribble. Reach for `settle()` only when the test cannot name what it is waiting
to see, which is mostly when capturing a golden.

`wait_for` is latched, which is a trap: a marker seen earlier answers instantly
and silently makes the assertion vacuous. When the question is about the screen
*now*, use `wait_screen()`. Never write `assert not wait_for(...)` - it pumps
the full `MAX_BEATS` budget before saying no; read `snapshot()` instead.

`Term.view()` is the normalized screen, cached until the next byte arrives.
Normalizing costs ~1 ms and every wait wants the same answer two or three times
per iteration, so go through it rather than re-normalizing `screen.display`.

## Never sync on a log line the test does not own

The console is a throttled 4800-baud wire and the greenhouse lives 50x faster
than real time, so the log tail drops what it cannot paint. Anything that only
ever appears there can go unseen on a healthy firmware, and the busier the
branch, the likelier that is.

- `boot()` syncs on chrome - the status bar's tab strip - not on log lines.
  `test_network_comes_up` asks `net` instead.
- A shell command's reply competes with the same firehose for the same wire, so
  `shell(spawn, quiet_logs=True)` runs `logs off` first. Any test asserting on a
  command's output wants it. It is not the default: it puts two lines in the
  transcript, which `test_shell_reconcile` counts as content.

Sync on chrome, on the page's own widgets, or on output you asked for.

## Goldens

`page_home` is the only one, asserted by `test_page_behaviour.py`. A golden
pins layout - which rows, in what order, with what labels, columns and chrome -
not values, which `normalize.py` masks. A renamed column or a shifted table
fails here; a warmer afternoon does not.

Two caveats: `meaningful_lines` drops blank lines, so a pure vertical-spacing
regression passes; and a mask inside a bordered table must be width-preserving
or the row stops matching (`_mask_table_cell` and `<ALARM_TS>` both pad).

## Dependencies

Manual: the build installs nothing.

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cmake -B build -DBOTANIK_TARGET=SIMULATOR -DPython3_EXECUTABLE="$(which python3)"
```

If pyte and pytest are not importable at configure time, CMake skips the suite.
It never fails configure and never installs.

## Running

```sh
ctest --test-dir build -R pyte --output-on-failure
make -C build check                          # part of the gate
pytest tests/pyte --sim build/botanik_sim    # or set BOTANIK_SIM
```

One CTest entry per test file, from a glob at configure time. Per-function
entries each paid ~1.5 s importing pytest to run ~0.4 s of test; per-file keeps
failures attributable and the `-j` slots busy.

Because an entry is a whole file, a file may share one booted sim across its
tests (`spawn_module`); `test_shell_reconcile` does, resetting state between
tests with `^U`+`^L` or a freshly submitted line. Anything that mutates - an
edit, a reset, a persisted override or a note - keeps its own `spawn`. It also
pays to keep a one-assertion test out of a file of its own, which is why the
odds and ends live together in `test_page_behaviour.py`.

Cases here name only the library's rows, so they pass on any full-profile
site. A case that needs a site's own table (its HOME golden, its programs, its
rows) lives beside that site, e.g. `examples/ref/tests/pyte/`: a conftest that
imports the fixtures from `harness.py`, registered with
`botanik_add_pyte_tests(<name> <dir>)`, and goldens in `golden/` next to it.

The harness itself - `harness.py`, `helpers.py`, `keys.py`, `normalize.py` - is
`tests/kit/pyte/`, the part a site repository builds on; this directory holds
only botanik's own cases.

A boot costs ~0.02 s and syncs on the tab strip. The network handshake behind
it spans 0.14-0.35 sim-seconds; the mock responder binds its sockets before its
thread starts (see `botanik_sim/network.h`).

## Regenerating goldens

```sh
make -C build                 # or the capture takes a stale sim
make -C build pytest-update
```

Kept out of `check` and `test` so the gate never rewrites its own expectations.
Review the diff before committing: `--update` writes the full snapshot
including the `ALARM_*` noise lines that are only dropped at compare time, so
strip anything matching `normalize._NOISE`.

## Layout

- `conftest.py` - the PTY/pyte `Term` (`poll` and its four predicates), the
  `spawn` / `spawn_module` factories, the `assert_screen` fixture.
- `normalize.py` - the masks for volatile cells, by regex and by table geometry.
- `keys.py` - input byte sequences matching `console/parser.cpp`.
- `helpers.py` - `boot` / `goto` / `shell`, and `assert_editor`.
- `test_*.py` - home reboot and network bring-up, the task table, param
  editing, notes, the cross-page kill ring, shell reconciliation, the watchdog.
