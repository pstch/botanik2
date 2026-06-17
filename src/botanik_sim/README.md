# botanik_sim - Linux simulator

Runs `libbotanik` as an ordinary Linux process so the full terminal UI, logging,
scheduler, and PLC sync layer can be exercised without hardware. The hardware
counterpart is [botanik_fw](../botanik_fw/README.md), which builds the same library
into a flashable OPTA image.

## Layout

| File | Role |
|---|---|
| `main.cpp` | Entry point: wires the library together (the equivalent of an Arduino sketch's `setup()` + `loop()`) and owns the main loop. |
| `simulator.h` | Site PLC payload layout (`PlcInputData_t` / `PlcOutputData_t`) and the mock coprocessor's public interface. |
| `simulator.cpp` | The mock PLC: a worker thread that animates the greenhouse payload with plausible, time-varying values. |

Unlike the OPTA, the simulator owns the PLC shared memory: `main()` allocates it as
local statics and the emulator thread writes to it, standing in for the M4
coprocessor. The payload here is a fully populated greenhouse model; on hardware the
[botanik_fw](../botanik_fw/README.md) payload mirrors the PLC IDE's generated type
instead.

## Building & running

The default debug tree (SIMULATOR target -> DEBUG build with ASan+UBSan and the
doctest suite wired in):

```bash
cmake -B build-debug -DBOTANIK_TARGET=SIMULATOR
make -C build-debug botanik_sim
./build-debug/botanik_sim
```

The simulator puts stdin into raw mode and bails unless it is attached to a real
terminal (`isatty()`), so run it in an actual TTY, not a piped/redirected context.
The diagnostic log is written to stderr; redirect it (`2> sim.log`) to keep it from
sharing the console UI's screen.

## Fast / high-iteration testing

Two environment variables compress the wall-clock cost of a run so the simulator
can churn through a very large number of scheduler ticks quickly. Both are
simulator-only and read once at startup.

| Variable | Effect |
|---|---|
| `BOTANIK_TIME_SCALE` | Runs the sim clocks (and the baud throttle) N times faster. Default 1, clamped to >= 1. See `hal/simulator.h`. |
| `BOTANIK_NO_XRUN_WARNING` | Silences the scheduler's overrun (xrun) warning, whose count/placement are load-dependent. Set it to any value (presence is what matters). See `scheduler/scheduler.cpp`. |

`BOTANIK_TIME_SCALE=N` dilates time uniformly: every byte/event ratio is
identical to a normal run, only wall time is divided by N. A 10 ms tick at scale
10 costs ~1 ms of wall-clock, so you reach a given iteration count ~N times
sooner without changing what the run actually exercises. Pushing the scale very
high eventually starves the per-tick budget (input/render start racing at the
~1 ms floor around scale >= 5), so raise it until ticks begin to overrun, then
back off.

Dilated time provokes the load-dependent xrun warning, which floods the log
pages and is just noise in a soak run. Pair the two to run hot and quiet:

```bash
# Hammer the scheduler ~10x faster, without overrun-warning spam.
BOTANIK_TIME_SCALE=10 BOTANIK_NO_XRUN_WARNING=1 ./build-debug/botanik_sim 2> sim.log
```

For an unattended high-iteration soak, build the release (no-sanitizer) sim - a
debug+ASan build is too slow per dilated tick. The pyte test suite uses exactly
this mechanism; see [tests/pyte/README.md](../../tests/pyte/README.md).

See also the cross-cutting design notes: [PAGES.md](../../docs/PAGES.md),
[PLC_SYNC.md](../../docs/PLC_SYNC.md), [SCHEDULER_PERF.md](../../docs/SCHEDULER_PERF.md),
and [LOG_FLOW.md](../../docs/LOG_FLOW.md).
