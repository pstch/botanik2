# pyte screen-assertion tests

These drive the real `botanik_sim` firmware over a **PTY** and replay its ANSI
output through [pyte](https://github.com/selectel/pyte) to reconstruct the 80x24
screen, then assert on that screen. They catch rendering regressions (cursor
moves, IL/DL, the line editor's reconciliation) that a source diff cannot.

## The setup

- **PTY, not a pipe:** `RawSerial::availableForWrite()` uses `ioctl(TIOCOUTQ)`,
  which only answers on a tty. A pipe would bypass the baud throttle and the TX
  back-pressure the tests exist to exercise.
- **Speed via `BOTANIK_TIME_SCALE`, never a higher baud.** Console baud is
  compile-time (`Config::CONSOLE_BAUD`, 4800) and the terminal derives its
  per-render byte budget from it, so raising the wire speed alone would just
  leave the renderer pacing for 4800. Dilating the clock keeps every byte/event
  ratio identical to a real run and compresses only wall time. Default 50;
  override with `PYTE_TIME_SCALE=N` (honored through pytest, ctest and make).
  Green far higher (verified at 500).
- **Release sim only** (`build/pyte-sim/`, RELEASE, no sanitizers). A debug+ASAN
  build is too slow per dilated tick. `make -C build` does **not** rebuild it -
  run `make -C build pyte-sim`, or the suite silently tests a stale binary.
- **A fast-living greenhouse is the point, and the hazard.** At scale 50 the mock
  plant lives 50x faster, so alarms cycle and the log tail floods while a test
  looks at the screen. `normalize.py` masks the cells that move, `drop_log_lines`
  takes the live tails out of a golden compare, and a `rows=`/`clip=` band
  narrows an assertion to the region it is about.

## Waiting without a wall clock

A host-seconds deadline is a statement about the **machine**, not the firmware.
The sim gets roughly a millisecond of wall budget per tick, so on a loaded box it
takes more wall time to do the same work, and any fixed timeout reclassifies
"slower" as "broken" - which is how `make check` came to fail on a *different*
screen test run to run, each passing on its own.

So no wait is budgeted in host seconds. Two rules replace it:

1. **Progress.** Give up only once the screen has stopped changing. A loaded
   machine still advances, so the wait stretches to match it; a screen that has
   settled on the *wrong* content stops changing and fails promptly.
2. **Sim time.** "Stopped" is measured on the firmware's own clock, read off the
   status bar. The budget is denominated in the thing being observed, so it
   scales with load automatically.

`Term.poll(done, idle_sim_s)` is that loop, and the only one: `settle()`,
`wait_for()`, `wait_screen()` and the golden compare are each a predicate over
it. Two budgets, because the waits ask different questions:

| call | question | budget |
|---|---|---|
| `wait_for(needles)` | has this string **ever** been on screen (latched as it went by, so a scrolled-away log line still counts) | `STALL_SIM_S`, 90 sim-s |
| `wait_screen(needle, present)` | is it on screen **now**, or has it left | `STALL_SIM_S` |
| `settle(quiet)` | has the repaint I just triggered finished | `SETTLE_SIM_S`, 5 sim-s |

`settle()` is a debounce, not an event wait, which is why its budget is short: an
action that paints nothing pays it in full, every time.

`HARD_CAP_S` (180 host-s) is the one host-time bound left. It is not a timing
assertion - it is the backstop for a sim that has wedged, so its clock never
advances and rule 2 alone would wait forever.

Two traps worth knowing:

- **`wait_for` is latched.** A marker seen earlier answers instantly, which can
  make an assertion vacuous (`wait_for("saved")` after `^O` passed because the
  untouched note had already read "saved"). Use `wait_screen()`, or read the row.
- **The sim's clock steps backwards.** The displayed time is `boot_epoch +
  uptime`, and the epoch is re-derived from the *undilated* host clock, so under
  dilation it is re-latched a couple of seconds lower every ~40 ms. `Stall` reads
  the far half of the midnight wrap as the step it is and re-baselines.

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
pytest tests/pyte --sim build/pyte-sim/botanik_sim   # or set BOTANIK_SIM
```

One CTest entry per test **file** (a glob at configure time). Per-function
entries each paid ~1.5 s importing pytest to run ~0.4 s of test; per-file keeps
failures attributable and the `-j` slots busy, measured 2.5x faster on 4 cores.
`.envrc` exports `CTEST_PARALLEL_LEVEL=$(nproc)`.

Because a CTest entry is a whole file, a file may share one booted sim across its
tests (`spawn_module`) - the boot handshake is the expensive part of a screen
test. `test_pages`, `test_shell_reconcile` and `test_shell_scroll` do; each
resets state between tests (`^U`+`^L`, or a freshly submitted line). Anything
that **mutates** - an edit, a reset, a persisted override or note - keeps its own
`spawn`.

## Every page has a display test

**Standing rule: each page registered in `BotanikFirmware::register_pages()` has
one settled-screen golden in `test_pages.py`.** Its `PAGES` table is the list, and
`test_every_registered_page_has_a_golden` checks it against the running firmware
by reading the status bar's tab strip - so a new page cannot quietly arrive
without a display test.

A golden pins **layout**: which rows a page draws, in what order, with what
labels, columns, rules and chrome. It does not pin **values** - `normalize.py`
masks every cell the greenhouse moves - because those are what a page exists to
show live, and pinning them is what made an earlier generation of goldens flaky
enough to delete. A renamed column, a dropped panel, a shifted table fail here. A
warmer afternoon does not.

Behaviour belongs next to the input that produced it, in the per-page files:
`helpers.assert_editor(term, *rows)` for the reconciliation scenarios, `wait_for`
for content appearing.

Two pages are honestly thin, and the reason is worth knowing:

- **LOGS** is nothing *but* the live scrollback, which `drop_log_lines` removes
  from both sides, so its golden pins the frame alone. Freezing the log would
  need a knob the firmware does not have; content is asserted in `test_logs.py`.
- **ALARMS** is pinned in its **ALL** view. RECENT sorts active-first, so both
  its row set and their order move with the greenhouse; `alarms.h` returns before
  that sort for `MODE_ALL`, leaving the catalog in table order.

One thing the comparison does not catch: `meaningful_lines` drops blank lines, so
a golden compares a *sequence* of non-blank rows rather than absolute positions.
A regression that only changes vertical spacing passes.

Anything masked inside a bordered table must be **width-preserving**, or the row
changes width and stops matching: `_mask_table_cell` pads to the cell it
replaces, and `<ALARM_TS>` pads to the stamp it replaces for the same reason.

## Regenerating

```sh
make -C build pyte-sim        # goldens capture a STALE sim otherwise
make -C build pytest-update
```

Deliberately separate from `check`/`test` - the gate never rewrites its own
expectations. Regeneration runs at `PYTE_TIME_SCALE=6` so captures are low-drift.
**Review the diff before committing**: `--update` writes the full snapshot
including the `ALARM_*` noise lines that are only dropped at compare time, so
strip anything matching `normalize._NOISE`.

## Layout

- `conftest.py`   - the PTY/pyte `Term` (`poll` and its four predicates), the
  `spawn` / `spawn_module` factories, the `assert_screen` fixture.
- `normalize.py`  - the masks for volatile cells, by regex and by table geometry.
- `keys.py`       - input byte sequences matching `console/parser.cpp`.
- `helpers.py`    - `boot` / `goto` / `shell`, and `assert_editor`.
- `test_pages.py` - the per-page goldens, and the invariant above.
- `test_*.py`     - per-page behaviour: home reboot, log marks, monitor
  navigation, param editing, notes, shell commands, shell reconciliation and
  scrollback.
