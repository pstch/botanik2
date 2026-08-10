# pyte screen-assertion tests

These drive the real `botanik_sim` firmware over a **PTY** and replay its ANSI
output through [pyte](https://github.com/selectel/pyte) to reconstruct the 80x24
screen, then assert on that screen. They catch rendering regressions (cursor
moves, IL/DL, the line editor's reconciliation) that a source diff cannot.

## The setup

- **PTY, not a pipe:** `RawSerial::availableForWrite()` uses `ioctl(TIOCOUTQ)`,
  which only answers on a tty. A pipe would bypass the baud throttle and the TX
  back-pressure the tests exist to exercise.
- **The tests drive the clock.** The sim advances one beat per command over a
  control pipe (`BOTANIK_SIM_CLOCK=manual`), so a wait is bounded in BEATS
  rather than host seconds: the same run gives the same screen whatever else the
  box is doing, and a failure costs the beats instead of waiting 15 s to see
  whether more output was coming.
- **Never a higher baud.** Console baud is
  compile-time (`Config::CONSOLE_BAUD`, 4800) and the terminal derives its
  per-render byte budget from it, so raising the wire speed alone would just
  leave the renderer pacing for 4800. Dilating the clock keeps every byte/event
  ratio identical to a real run and compresses only wall time. Default 50;
  override with `PYTE_TIME_SCALE=N` (honored through pytest, ctest and make).
- **The scale no longer buys or costs wall time.** Measured over the whole suite:
  50 → 4.64 s, 200 → 4.66 s, 400 → 4.65 s. Nothing free-runs any more, so the sim
  cannot compete with the harness for CPU and the dilation only decides how much
  plant-time a beat represents. What still helps is doing less: fewer boots,
  content sync instead of `settle()`, and not normalizing the same screen twice.
  (Under the old wall-clock harness raising the scale actively hurt - 50 → 6.3 s
  against 400 → 10.5-14.1 s - which is why the knob reads as a tuning lever.)
- **The tree's own sim** (`build/botanik_sim`), built by a plain `make -C build`.
  Under `check-full` that sim is instrumented like everything else, which costs
  wall time per dilated tick: raise `--time-scale` there rather than lowering it.
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

That replaced a pair of wall-clock bounds - an idle heuristic and a hard cap -
and the reasoning against them is worth keeping, because the obvious fix was the
wrong one twice. Denominating the budget in the sim's own clock reads as if it
would scale with load by itself; it does not, because the status-bar clock is
`boot_epoch + uptime` and the sim re-derives `boot_epoch` from the host clock,
which cancels the dilation exactly (measured: the bar advances 21 s in 20 s of
wall time while the uptime beside it advances 200 s in 4). Tightening the idle
window instead false-failed four runs in six against a sim known to be good.
Neither survives a sim that is starved rather than stuck. Driving the clock is
what removes the question.

**Prefer content sync to `settle()`.** `settle()` has to wait out the whole
4800-baud dribble, where a content wait stops at the first frame that says yes.
Reach for `settle()` only when the test cannot name what it is waiting to see -
capturing a golden, mostly.

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
pytest tests/pyte --sim build/botanik_sim            # or set BOTANIK_SIM
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
make -C build                 # goldens capture a STALE sim otherwise
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
