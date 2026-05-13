# botanik // small-scale horticultural greenhouse control system

![Botanik logo](images/logo.png "Botanik")

This project is used to host the programming of a control system for a
small-scale horticultural greenhouse. This control system is used for
irrigation (at fixed schedules) and ventilation (with air blowers,
intractors, window openers, thermal screens and an evapo-transpiration
cooling system). The system is used in combination with other autonomous
systems : an emergency heat blower and a heatpump, both working with their
own thermostats, independently of the main control system. The control
system is also used to monitor the correct operation of these independent
systems and to monitor the power supply and emergency generator system.

This control system is currently implemented with an Arduino OPTA PLC and a few
Modbus RTU input/output modules, which allows us to run custom C code alongside
the PLC programming. This is useful to overcome the many shortcomings of
Arduino's PLC platform. The Arduino's PLC environment is frustrating to use,
quite undocumented, and lacks many features standard in other environments,
this repository is also intended to host the knowledge we acquired of this platform.

## the `libbotanik` C++ library

TODO

## Building

The default build target is the Linux simulator (`SIMULATOR`), which compiles to a
native executable and is the only target that actually works end-to-end today:

```bash
cmake -B build -DBOTANIK_TARGET=SIMULATOR
make -C build
```

### OPTA target

The `OPTA` build target cross-compiles for the Arduino OPTA PLC (STM32H747,
`arm-none-eabi-gcc`). **This target does not build out of the box from this
repository.** It requires a large set of Arduino MBED / PLC IDE headers that live
under `includes/` and are **not** distributed here. To obtain them, perform a
successful build of the project in the Arduino PLC IDE; the required headers are
then found in the IDE's build cache and must be copied into `includes/` by hand.
And right now, even with this large set of headers, we're still not able to build,
but we can make IntelliSense work.

## Hardware notes & useful links

TODO
