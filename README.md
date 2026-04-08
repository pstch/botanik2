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

## Hardware notes & useful links

TODO
