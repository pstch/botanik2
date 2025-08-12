# botanik // small-scale horticultural greenhouse control system

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

Although the OPTA is nowhere near a real PLC, it can be a cost-effective way to 
implement a control system for small-scale projects with low budgets, but it does
require some compromises, adaptation and tinkering.

Of course, the control system available in this project should be relatively easy
to port to other platforms, especially if they offer equivalents to the features 
we implement ourselves in C (HSCs to replace interrupt-based pulse counters, better
RTC handling with NTP already handled, a logging/debugging system, a good collection 
of PLC functions & blocks, etc). This is why most of the PLC ST code used in this project, 
or at least the code that is not too specific to the target site, will be available in 
files extracted from the PLC project.

## The Arduino PLC environment

The project is mainly developed in the "Arduino PLC IDE", a very frustrating environment
that is a chore to use, lacks many basic features, and crashes quite often.

This project has two distinct parts that run on the same microcontroller :
 - the PLC programming, implemented using the **AxelPLC** runtime from 
  "Axel Software", (sometimes shortened to AlPlc), on which it's **very hard** 
  to find any information. The PLC programming is done in the PLC IDE, which is
  based on the **LogicLab** by that same company. This part contains most of the 
  actual control logic, implemented in standard IEC 61131-3 programming. The runtime
  provides Modbus RTU integration, and allows us to map variables to its Modbus TCP
  server and Modbus RTU client, so that we can expose parameters and status variables
  to other Modbus TCP clients, and so that we can access the input/outputs provided by
  the associated Modbus RTU servers (I/O modules, relay modules, devices with built-in 
  Modbus support, etc).
 - the C code, which uses a variation of the Arduino framework based on **Mbed OS** (version 5.1.15). 
   This part contains the logging system (used to receive logs from the PLC runtime, 
   and forward them to its serial port and to MQTT brokers), some time-handling code
   (NTP synchronization for the local RTC), the sunrise/sunset times calculation, and 
   it is also used to provide interrupt-based counters as the OPTA doesn't have integrated
   high-speed counters and the interrupts are not exposed to the PLC runtime. This part is also
   used to control the hardware watchdog, to reset both the whole microcontroller if the C
   code crashes or if the PLC runtime stops responding

## Hardware notes & useful links

The Arduino OPTA uses the **STM32H747XI** microcontroller with ARM's **Mbed OS** platform, 
which runs a precompiled version 1.2.0 of the **AxelPLC** runtime. The communication between the AxelPLC
runtime and our code is done using two shared memory regions : one for the "input" to the PLC (`PLCIn`)
and one for the "output" to the PLC (`PLCOut`). We have little to no information on how (and when) the
AxelPLC runs alongside our code : we just know that the `Run()` function of the runtime is called in 
Arduino's `setup()` function, and that the runtime is never called from `loop()`, so it must be executing
in the background, probably using an Mbed Thread assigned to one of the two cores, but even that is an 
assumption. Because of that, we'll use some defensive programming techniques to avoid interference, race 
conditions and potential crashes caused by the interaction of the PLC runtime and our code :
 - On the C side, implementing the equivalent of PLC set-period tasks to avoid putting too much load
   on the CPU and to leave some time for the PLC runtime. This might not be needed because our MCU has 
   two independent cores, but since we have zero information about the PLC runtime, it can still help us
   avoid significant problems.
 - On both sides, gating as much as we can of the control logic so that it only runs when the other side's
   gated logic is not currently executing, and even making each side's gated logic run in "lock-step" 
   with each other, by making the above-mentioned "gating" wait for the other side to send a new heartbeat
   before running a new cycle of the control logic.
 - On the C side, enabling a hardware watchdog timer that is reset after each PLC heartbeat, so that the
   whole system gets reset if either the C code or PLC runtime crashes/hangs for any reason
 - On the PLC side, entering a "safe state" if we don't receive heartbeats from the C code, to attempt to
   prepare the system from the impending hardware reset.
 - Monitoring the proper execution of this synchronization scheme (overruns, 
   lack of idle time, latency spikes, etc) and providing a robust logging systems that allows us to log
   messages from both the C side and the PLC programming.

 - [Arduino OPTA datasheet](https://docs.arduino.cc/resources/datasheets/AFX00001-AFX00002-AFX00003-datasheet.pdf)
   + The version we currently use is the **Arduino OPTA RS-485** (AFX00001)
 - [STM32H747XI datasheet](https://www.st.com/resource/en/datasheet/stm32h747ag.pdf)
 - [Arduino's framework implementation for Mbed-based devices](https://github.com/arduino/ArduinoCore-mbed)
   + The Arduino PLC IDE currently uses version 4.1.6 of this code to compile the firmware
 - [Axel Software's LogicLab software](https://www.axelsoftware.it/en/logiclab/)
   + Can be useful as it provides a standard library of PLC programming elements that is not
     provided in the Arduino PLC IDE, but that can still be imported to the Arduino PLC IDE.
     This is not documented anywhere, which is a shame.
 - [Mbed OS documentation, version 5.1.5](https://os.mbed.com/docs/mbed-os/v5.15/introduction/index.html)


## Associated devices

For more details (exact references, ModBus address/register mappings), you can look at the 'matos.ods' and 'registers.ods'
at the root of the repository.

### Modbus I/O

 - 2* Modbus RTU 32-ch relay modue (Waveshare 25140)
 - 1* Modbus RTU 8-ch analogic input module (Waveshare 25767)
 - 1* Modbus RTU 8-ch analogic output module (Waveshare 26211)
 - 1* Modbus RTU 8-ch isolated hub (Waveshare 24271) 
 - One heatpump system with Modbus, specs still unknown
 
### Sensors

 - 3* 4-20mA waterproof temperature sensor (to monitor exterior temperature) 
 - 2* 4-20mA temperature/humidity sensor with integrated display (interior temperature channel A)
 - 4* Modbus RTU temperature/humidity sensor (interior temperature channel B)
 - 1* Modbus RTU three-phase electricity meter (Eastron SDM630)
 - 1* Modbus RTU single-phase electricity meter (Enolec MCT1-140)
 - 5* voltage dividers to measure DC system/battery voltage (10kOhm+20kOhm)
 - (tachymeter from the intractor fans)

### Actuators

 - 2* air intractors (BullFilter EC Fan 315, with 0-10V control and tachymeter output)
 - 4* window openers (Linear actuators, 2-wire, 24VDC, set-delay)
 - 4* ceiling fans (Three-speed, 5-wire, 230VAC)
 - 8* motorized ball valves (Tray valves, 2-wire, 24VDC, set-delay)
 - 2* motorized ball valves (Input/output, 2-wire, 24VDC, set-delay)
 - 1* solenoid valve (Humidifier, 2-wire, 24VDC)

## Project status
If you have run out of energy or time for your project, put a note at the top of the README saying that development has slowed down or stopped completely. Someone may choose to fork your project or volunteer to step in as a maintainer or owner, allowing your project to keep going. You can also make an explicit request for maintainers.
