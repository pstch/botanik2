# botanik_sim - Linux simulator {#dev_sim}

Runs `libbotanik` as an ordinary Linux process so the full terminal UI, logging,
scheduler, and scan cycle can be exercised without hardware. The hardware
counterpart is @ref dev_fw, which builds the same library
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

`BOTANIK_SIM_CLOCK` replaces the host clock with a simulated one. Two modes,
differing only in who moves it.

`manual` hands it to a harness: the sim runs one beat per `RUN n` command on
`BOTANIK_SIM_CONTROL_FD` and stands still between them, so a run is bounded in
beats rather than seconds and repeats identically. That is what the pyte suite
drives; see @ref dev_pyte and
`botanik_sim/clockctl.h`.

`fast` has the sim move it: one beat per iteration, held to `BOTANIK_SIM_SPEED`
simulated seconds per real second. It is for WATCHING the control system - a
greenhouse day is 24 hours of laws whose slowest inputs move over days, and it
is the only way to see an irrigation interval elapse.

```bash
# a July day, flat out: about 7 minutes for 24 simulated hours
BOTANIK_SIM_CLOCK=fast BOTANIK_SIM_EPOCH=$(date -u -d 2026-07-01 +%s) ./build/botanik_sim
```

The clock is not the host's SCALED, which is what makes this sound: uptime and
the wall clock are both derived from the one virtual reading, so the calendar
cannot sawtooth against uptime. What genuinely does not survive is the
platform-health modelling - a duty cycle, a jitter figure and a watchdog window
all measure the real passage of time, and under a virtual clock there is none to
measure. So `HAL::Clock::is_virtual()` defaults the watchdog and the CPU_* load
alarms off; both are still opt-in-able, and neither reads anything meaningful if
you do.

| Variable | Effect |
|---|---|
| `BOTANIK_SIM_CLOCK` | `manual` to hand the clock to a harness (with `BOTANIK_SIM_CONTROL_FD`), `fast` to have the sim drive it. Default: free-running on the host clock. |
| `BOTANIK_SIM_SPEED` | Under `fast`, simulated seconds per real second (`60` is a minute a second). Default `0`: as fast as the host manages. |
| `BOTANIK_SIM_SCENARIO` | The day the simulated field puts the plant through (`scripts/sim_scenario.sh` drives one per command): `fair`, `hot`, `cold`, `storm`, `damp`, `dry`, `probes`, `supply`, or `cycle` (the default, one per simulated day). See the site README. |
| `BOTANIK_SIM_EPOCH` | Under a virtual clock, the UNIX second the run starts at (`date -u -d 2026-07-01 +%s`). Default 2026-01-01. The sun tracker and the simulated weather both work from it, so this is what picks the SEASON. |
| `BOTANIK_NO_LOAD_ALARMS` | Holds the CPU_* load alarms (CPU_LOAD / CPU_JITT / CPU_XRUN) cleared, so the active-alarm set is deterministic for screen goldens. Set it to a non-zero value. Defaults ON under a virtual clock. No effect on OPTA (no environment). See `health/health.h`. |
| `BOTANIK_WATCHDOG` | `0` to run without it. Defaults ON, except under a virtual clock. |

See also the developer manual: @ref dev_pages, @ref dev_scheduler and
@ref dev_logging.
