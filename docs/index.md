# Botanik

@brief @c libbotanik - greenhouse-control firmware for the Arduino OPTA PLC
       (STM32H747, Cortex-M7).

Botanik is a complete controller: it reads the field over Modbus, runs the control
programs, drives the outputs, and presents the whole of it over a serial terminal -
pages, a shell, logging, alarms and health supervision. Its core, `libbotanik`, is a
platform-agnostic C++11 library; the same code builds as a Linux simulator or for the
OPTA hardware.

Everything specific to one physical greenhouse - its variables, alarms and control
programs - is a **site**: a directory of variable tables and control programs, in C++.
The library never knows which greenhouse it is running.

This documentation is in three parts, by who you are:

- @subpage manual - **User manual.** Operating a running unit: the console and its
  pages, the shell, parameters, alarms, logs, and control authority.
- @subpage integration - **Integration manual.** Building your own product on
  `libbotanik`: declaring a site, its tables, its control programs, and the config
  surface.
- @subpage development - **Developer manual.** Working on the library itself: the
  architecture, the console TX path, and the other mechanisms worth knowing before
  you change them.

The full API surface is in the **Namespaces** and **Classes** tabs.
