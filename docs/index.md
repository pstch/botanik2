# Botanik

This documentation contains the manual of the Botanik PLC firmware and its reference site ("gh_mlb41").

Botanik is a library that supports the writing of PLC control firmwares on STM32 platforms (the reference site uses an Arduino OTPA).
It was written because the Arduino PLC IDE and the Axel PLC runtime we planned to use initially are not sufficient for a real PLC design :
 - the shared memory between the runtime and the user-provided C code is only 512 bytes
 - interaction between the C side and the runtime is generally not easy and requires a lot of boilerplate even for simple things
 - the Axel PLC runtime is proprietary, and the Arduino PLC IDE only runs on Windows

 Botanik combines a featureful serial HMI and a PLC runtime able to run standard ST programs. Sites define their variable mappings (input, output, status, params, alarms, etc) using CSV tables, programs are written in ST, so there is generally very little C++ code that needs to be written for a specific site. The ST programs and CSV tables are compiled to C and C++ using Python scripts and the [MatIEC compiler](https://github.com/beremiz/matiec).

 The built-in serial HMI provides a home page with customizable fields, a extensible shell, logs, alarms, parameter control, variable monitoring, tasks control, a system status page, and a page to store notes. It is written for low-bandwidth X3.64 terminals, such as the Minitel 1B, whose serial port runs at 4800bps.

 The documentation is split into 4 main groups :

- @subpage user : the operator's manual, useful for interacting with the HMI
- @subpage gh_mlb41 : information about the reference site, `gh_mlb41`, an Hawortia greenhousee in France
- @subpage int : the integrator's manual, useful for porting to other sites and other platforms
- @subpage dev : the developer manual, useful when working on the base `libbotanik` library
