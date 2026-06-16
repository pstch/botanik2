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

See also the cross-cutting design notes: [PAGES.md](../../docs/PAGES.md),
[PLC_SYNC.md](../../docs/PLC_SYNC.md), [SCHEDULER_PERF.md](../../docs/SCHEDULER_PERF.md),
and [LOG_FLOW.md](../../docs/LOG_FLOW.md).
