# botanik_fw - reference OPTA entry point

<!-- REVIEW_35: TODO -->

Hardware counterpart of [botanik_sim](../botanik_sim/README.md). Where the simulator
runs the library as a Linux process, this is the **reference for the project-root
entry point** you write inside an Axel PLC IDE project: it includes `<libbotanik.h>`,
wires the components, and exposes the hooks the sketch calls.

## How the pieces map to the Axel PLC IDE

The IDE owns the project and only accepts **imported Arduino libraries** - no sketch
folder or arbitrary file tree. So two things live on the IDE side and one is
delivered as a library:

| Axel side | Reference here | Role |
|---|---|---|
| The "sketch" window | `botanik_fw.ino` | Thin: `setup()` calls `botanik_setup()`, `loop()` calls `botanik_step()`. |
| A C++ file at the project root | `botanik_arduino.cpp` + `firmware.{h,cpp}` | Includes `<libbotanik.h>`, constructs the components, and *defines* `botanik_setup()`/`botanik_step()`. |
| Imported library | `libbotanik` (delivered as a zip) | The building blocks. Carries no entry point of its own. |

`botanik_setup()`/`botanik_step()` (see `botanik_arduino.h`) are **not** library API -
defined by the project-root file, called by the sketch. The PLC shared memory is bound
from `HAL::get_plc_input()/get_plc_output()` rather than allocated locally.

## Memory map

The OPTA is a dual-core STM32H747: the **M7** runs Botanik (with Mbed OS), the **M4**
runs the Axel PLC runtime, and the two communicate only through a shared RAM region.
Botanik names exactly one address - the shared PLC vars, via the
`m_PLCSharedVarsInputBuf` / `m_PLCSharedVarsOutputBuf` symbols (`hal/opta.h`); every
other placement is the Axel PLC IDE's linker config, not this repo. The map below is
the **typical** STM32H747 + Axel arrangement for orientation, not a guarantee:

| Address | Region | Use |
|---|---|---|
| `0x0800_0000` | FLASH bank 1 (1 MB) | M7 - Botanik app + Mbed OS |
| `0x0810_0000` | FLASH bank 2 (1 MB) | M4 - Axel PLC runtime + program |
| `0x2000_0000` | DTCM (128 KB) | M7 stack / hot data |
| `0x2400_0000` | AXI SRAM, D1 (512 KB) | M7 heap / general |
| `0x3000_0000` | SRAM1-3, D2 (288 KB) | **M4 data + shared PLC vars** |
| `0x3800_0000` | SRAM4, D3 (64 KB) | low-power / cross-core |
| `0x3880_0000` | Backup SRAM (4 KB) | retained |

The shared PLC vars (i.e. `PlcInput<>` / `PlcOutput<>`) sit in the D2 SRAM row above;
the Axel linker decides where they land.

## Delivering the library

```bash
make -C build arduino-lib      # -> build/arduino/libbotanik.zip
```

Import it via the IDE's *Sketch -> Include Library -> Add .ZIP Library*. Packaging just
copies + zips text, so it runs from the default SIMULATOR tree - no OPTA toolchain
needed; the IDE compiles the zip with the OPTA board's own flags.

## Field IO

The firmware owns its IO outright - there is no coprocessor to agree a payload
with. The site declares its signals in `sites/<name>/io.h`, and the Modbus RTU
master fills them. Until that driver lands, every input reads as its declared
default and the console shows it as "no data" rather than as a measurement.

## ARM build for ABI/size checks

`cmake -B build-opta -DBOTANIK_TARGET=OPTA` compiles `libbotanik` (plus this reference
wiring) for ARM - size, LTO, and the footprint `static_assert`s - without the full
Arduino link. Use `arduino-lib` + the IDE to produce a flashable image.
