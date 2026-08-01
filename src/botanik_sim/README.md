# botanik_sim - Linux simulator

<!-- REVIEW_35: TODO -->

Runs `libbotanik` as an ordinary Linux process so the full terminal UI, logging,
scheduler, and scan cycle can be exercised without hardware. The hardware
counterpart is [botanik_fw](../botanik_fw/README.md), which builds the same library
into a flashable OPTA image.

## Layout

This directory is just a thin harness: `main.cpp` is the entry point that wires the
library together (the equivalent of an Arduino sketch's `setup()` + `loop()`) and owns
the main loop. The site's variable tables and the simulated field that animates
them live under `src/sites/<name>/`, shared verbatim by the simulator and the OPTA
firmware - the sim writes the same variables the Modbus driver will.

Where the OPTA reads real hardware, the simulator calls the site's `simulate()`
straight from the main loop each beat: it publishes meter readings, drives the
coils and raises alarms into the very same variable tables the field drivers
write on hardware, so nothing downstream can tell the difference.

## Building & running

The default debug tree (SIMULATOR target -> DEBUG build with ASan+UBSan and the
doctest suite wired in):

```bash
cmake -B build-debug -DBOTANIK_TARGET=SIMULATOR
make -C build-debug botanik_sim
./build-debug/botanik_sim
```

The simulator puts stdin into raw mode and bails unless attached to a real terminal
(`isatty()`), so run it in an actual TTY, not a piped/redirected context. The
diagnostic log goes to stderr; redirect it (`2> sim.log`) to keep it off the console
UI's screen.

## Fast / high-iteration testing

Two environment variables compress the wall-clock cost of a run so the simulator
can churn through many scheduler ticks quickly. Both are simulator-only and read
once at startup.

| Variable | Effect |
|---|---|
| `BOTANIK_TIME_SCALE` | Runs the sim clocks (and the baud throttle) N times faster. Default 1, clamped to >= 1. See `hal/simulator.h`. |
| `BOTANIK_NO_XRUN_WARNING` | Silences the scheduler's overrun (xrun) warning, whose count/placement are load-dependent. Set it to any value (presence is what matters). See `scheduler/scheduler.cpp`. |
| `BOTANIK_NO_LOAD_ALARMS` | Holds the CPU_* load alarms (CPU_LOAD / CPU_JITT / CPU_XRUN) cleared, so the active-alarm set is deterministic for screen goldens. Set it to a non-zero value. No effect on OPTA (no environment). See `health/health.h`. |

`BOTANIK_TIME_SCALE=N` dilates time uniformly: every byte/event ratio matches a normal
run, only wall time is divided by N, so you reach a given iteration count ~N times
sooner. Pushing it very high starves the per-tick budget (input/render race at the
~1 ms floor around scale >= 5), so raise it until ticks overrun, then back off. Dilated
time also provokes the load-dependent xrun warning; pair the two to run hot and quiet:

```bash
# Hammer the scheduler ~10x faster, without overrun-warning spam.
BOTANIK_TIME_SCALE=10 BOTANIK_NO_XRUN_WARNING=1 ./build-debug/botanik_sim 2> sim.log
```

For an unattended high-iteration soak, build the release (no-sanitizer) sim - a
debug+ASan build is too slow per dilated tick. The pyte test suite uses exactly
this mechanism; see [tests/pyte/README.md](../../tests/pyte/README.md).

See also the cross-cutting design notes: [PAGES.md](../../docs/PAGES.md),
[SCHEDULER_PERF.md](../../docs/SCHEDULER_PERF.md), and
[LOG_FLOW.md](../../docs/LOG_FLOW.md).
