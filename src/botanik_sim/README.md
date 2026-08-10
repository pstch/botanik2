# botanik_sim - Linux simulator

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

`BOTANIK_SIM_CLOCK=manual` hands the clock to a harness: the sim runs one beat
per `RUN n` command on `BOTANIK_SIM_CONTROL_FD` and stands still between them, so
a run is bounded in beats rather than seconds and repeats identically. That is
what the pyte suite drives; see
[tests/pyte/README.md](../../tests/pyte/README.md) and `botanik_sim/clockctl.h`.

There is no clock-scaling knob: a firmware clock running faster than the host's
puts uptime and the wall clock in different time bases, which sawtooths the
calendar clock and forces the watchdog to be inflated out of the way. Driving the
beat gets the speed-up with neither.

| Variable | Effect |
|---|---|
| `BOTANIK_SIM_CLOCK` | `manual` to take the clock (with `BOTANIK_SIM_CONTROL_FD`). Default: free-running on the host clock. |
| `BOTANIK_NO_LOAD_ALARMS` | Holds the CPU_* load alarms (CPU_LOAD / CPU_JITT / CPU_XRUN) cleared, so the active-alarm set is deterministic for screen goldens. Set it to a non-zero value. No effect on OPTA (no environment). See `health/health.h`. |

See also the developer manual: [the page framework](../../docs/dev/pages.md),
[the scheduler](../../docs/dev/scheduler.md), and
[log flow](../../docs/dev/logging.md).
