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
- **Do not raise the scale to go faster - it is slower.** Measured, three runs
  each: 50 → 6.3 s, 100 → 7.3-9.5 s, 200 → 9.5-10.9 s, 400 → 10.5-14.1 s. At 50
  the suite is already off the sim-time critical path; past it the sim only burns
  CPU competing with the harness, while the costs that remain are host-time and
  do not shrink. The suite is CPU-bound (wall ~= user+sys), so what helps is
  doing less: fewer boots, content sync instead of quiet windows, and not
  normalizing the same screen twice.
- **Release sim only** (`build/pyte-sim/`, RELEASE, no sanitizers). A debug+ASAN
  build is too slow per dilated tick. `make -C build` does **not** rebuild it -
  run `make -C build pyte-sim`, or the suite silently tests a stale binary.
- **A fast-living greenhouse is the point, and the hazard.** At scale 50 the mock
  plant lives 50x faster, so alarms cycle and the log tail floods while a test
  looks at the screen. `normalize.py` masks the cells that move, `drop_log_lines`
  takes the live tails out of a golden compare, and a `rows=`/`clip=` band
  narrows an assertion to the region it is about.

## Waiting

Every wait is the same loop, `Term.poll(done, idle_s, cap_s)`: pump the PTY until
the predicate says yes, the normalized screen has been **still** for `idle_s`, or
`cap_s` has passed outright. `settle()`, `wait_for()`, `wait_screen()` and the
golden compare are each one predicate over it.

| call | question | idle | cap |
|---|---|---|---|
| `wait_for(needles)` | has this string **ever** been on screen (latched as it went by, so a scrolled-away log line still counts) | 15 s | 30 s |
| `wait_screen(needle, present)` | is it on screen **now**, or has it left | 15 s | 30 s |
| `assert_screen(term, golden)` | does the screen match the golden | 4 s | 20 s |
| `settle(quiet)` | has the repaint I just triggered finished | 0.2 s | 5 s |

**Both bounds are load-bearing, and they are not interchangeable.** Idle is a
heuristic - "nothing has happened for a while, so probably nothing will" - and it
cannot tell a stuck firmware from a starved one, so it has to stay generous;
tightening it from 15 s to 3 s false-failed four runs in six against a sim known
to be good. The cap is what actually bounds a wait, and without it the suite can
**hang outright**: a page with a live log tail repaints forever, so on a firmware
that never reaches the awaited state the idle rule never fires. That is not
hypothetical - it is how a merge to a branch with a chatty modbus probe turned a
4-second suite into one that blew a 900-second timeout.

Budgets are in **host seconds on `time.monotonic()`**. No passing test reaches
one; they only bound how long a stuck wait takes to report.

That is a correction of an earlier design, and the reasoning is worth keeping
because it reads so plausibly. The budget used to be denominated in the sim's own
clock, read off the status bar, on the theory that a budget measured in the
observed thing scales with load by itself. It does not - that clock is not a
measure of the firmware's progress at all. It reads `boot_epoch + uptime`, and
the sim re-derives `boot_epoch` from the host clock (`utils/time.cpp`), which
cancels the dilation exactly: measured, the status bar advances 21 s in 20 s of
wall time while the uptime field beside it advances 200 s in 4. So the budget was
a wall clock in fancy dress, with a 2-second sawtooth on top from the epoch
re-latch, firing at unpredictable moments - which is what kept failing a
*different* screen test on every loaded run. It also reset under a test's feet on
a reboot.

If you want a true measure of firmware progress, the uptime field is dilated and
the status-bar clock is not. Neither survives a sim that is starved rather than
stuck, which is why the bounds above are honest wall clocks with a cap.

**Prefer content sync to `settle()`.** Measured, `settle()` costs ~200 ms against
~20 ms for a `wait_screen()`: it can only wait out the 4800-baud dribble, where a
content wait stops at the first frame that says yes. Reach for `settle()` only
when the test cannot name what it is waiting to see.

**`wait_for` is latched**, and that is a trap as well as a feature: a marker seen
earlier answers instantly, which silently makes an assertion vacuous
(`wait_for("saved")` after `^O` passed because the untouched note already read
"saved"; three `wait_for("[all]")` calls in the monitor test could not fail).
When the question is about the screen *now*, use `wait_screen()`.

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
pytest tests/pyte --sim build/pyte-sim/botanik_sim   # or set BOTANIK_SIM
```

One CTest entry per test **file** (a glob at configure time). Per-function
entries each paid ~1.5 s importing pytest to run ~0.4 s of test; per-file keeps
failures attributable and the `-j` slots busy, measured 2.5x faster on 4 cores.
`.envrc` exports `CTEST_PARALLEL_LEVEL=$(nproc)`.

Because a CTest entry is a whole file, a file may share one booted sim across its
tests (`spawn_module`). `test_shell_reconcile` and `test_shell_scroll` do; each
resets state between tests (`^U`+`^L`, or a freshly submitted line). Anything that **mutates** - an edit, a reset, a persisted
override or note - keeps its own `spawn`. It also pays to keep a one-assertion
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
The MARK check in `test_page_behaviour.py` is the one legitimate log-tail
assertion, and LOGS is the page whose entire content is the log.

## Goldens

`page_home` is the one settled-screen golden left, asserted by
`test_page_behaviour.py`. There used to be one per page, in a `test_pages.py`
that also checked the status bar's tab strip so a new page could not arrive
without a display test. It was removed: it cost ~10 s of the gate, and a
full-screen golden per page is an expensive way to notice a renamed column.
Per-page BEHAVIOUR is still covered by the files below.

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
  need a knob the firmware does not have; content is asserted in `test_page_behaviour.py`.
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
- `test_*.py`     - per-page behaviour: home reboot, log marks, monitor
  navigation, param editing, notes, shell commands, shell reconciliation and
  scrollback.
