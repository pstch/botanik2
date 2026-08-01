# botanik_fw - OPTA entry point

Hardware counterpart of [botanik_sim](../botanik_sim/README.md). Where the simulator
runs the library as a Linux process, this builds the same library and the same site
into a flashable image for the OPTA's M7 core.

## Layout

A thin harness, like the simulator's:

| File | Role |
|---|---|
| `firmware.h` / `firmware.cpp` | the `BotanikSite` singleton - a function-local static, so it is constructed on first use rather than before `main()` |
| `main.cpp` | `instance().setup()` then `instance().run()` |
| `mbed_hooks.cpp` | the Mbed OS integration points |

The singleton is deliberate: the firmware must not be constructed until the Arduino
core's `init()` has brought up the UARTs. Any project embedding this should do the
same.

The site is selected at configure time (`-DBOTANIK_SITE` / `-DBOTANIK_SITE_DIR`);
`libbotanik_site.h` resolves it to `BOTANIK_SITE_HEADER`, which exposes `BotanikSite`,
so nothing here names a greenhouse.

## Building

```bash
arduino-cli core install arduino:mbed_opta     # one-time
cmake -B build-opta -DBOTANIK_TARGET=OPTA
make -C build-opta                             # -> build-opta/botanik_opta.bin
```

Three targets result: `botanik_fw` (this wiring, plus the site sources),
`botanik_ldscript` (the Mbed linker script, preprocessed - it references macros that
must be expanded first), and `botanik_opta` (the linked M7 ELF, then `objcopy`'d to a
raw binary).

Only three things are taken from the Arduino Mbed core package: the prebuilt
`libmbed.a`, that linker script, and the target's `PinNames`. The package's own 2017
toolchain is deliberately not used - it is roughly 40x slower on the Mbed headers.

See [Building and testing](../../docs/dev/build.md) for the constraints on the link
(the float ABI in particular) and the full build options.

## Field IO

The firmware owns its IO outright. The site declares its signals in
`sites/<name>/vars.h` and the Modbus RTU master fills them. Until that driver lands,
every input reads as its declared default and the console reports it as "no data"
rather than as a measurement.

## Memory map

The OPTA is a dual-core STM32H747. Botanik runs entirely on the **M7**, with Mbed OS,
and shares no memory with the M4: field I/O reaches it over Modbus and the OPTA's own
channels. The map below is the typical STM32H747 arrangement, for orientation only:

| Address | Region | Use |
|---|---|---|
| `0x0800_0000` | FLASH bank 1 (1 MB) | M7 - Botanik + Mbed OS |
| `0x0810_0000` | FLASH bank 2 (1 MB) | M4 - unused by Botanik |
| `0x2000_0000` | DTCM (128 KB) | M7 stack / hot data |
| `0x2400_0000` | AXI SRAM, D1 (512 KB) | M7 heap / general |
| `0x3000_0000` | SRAM1-3, D2 (288 KB) | M4 data |
| `0x3800_0000` | SRAM4, D3 (64 KB) | low-power |
| `0x3880_0000` | Backup SRAM (4 KB) | retained |
