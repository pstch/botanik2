#ifndef MODBUS_RTU_ALARMS_H
#define MODBUS_RTU_ALARMS_H

/** @file modbus/rtu/alarms.h
 *  @brief The field bus's own rows, for a site's variable table.
 *
 *  Ordinary rows written with the same factories a site uses, spliced into every
 *  site's table through BOTANIK_LIBRARY_VARS (vars/library.h) - the same route
 *  the platform-health alarms take, and for the same reason: the code that
 *  raises a row and the declaration of that row belong in one place.
 *
 *  `RtuClient` drives both by name, resolved once at bring-up, exactly as
 *  HealthMonitor drives the platform alarms. There is no second table.
 */

#include "libbotanik/vars/declare.h"    // the A/SV factories these rows are written with

/** @brief The field bus alarm: a device has stopped answering.
 *
 *  Raised when a device has failed its retry and gone into backoff, not on the
 *  first missed answer - one lost frame on an RS-485 run is weather, and an
 *  alarm that fires on it teaches an operator to ignore alarms. The backoff IS
 *  the dwell, so `on_s` is 0.
 *
 *  `off_s` is not: a device that answers one probe and then goes quiet again is
 *  a loose connector, and announcing the recovery instantly would fill the log
 *  with a fault that never really cleared. Five seconds of an answering bus is
 *  the evidence that it came back.
 *
 *  Register 996: the top of the library's 900-999 reserve, beside the platform
 *  alarms at 990-995. */
#define BOTANIK_MODBUS_RTU_ALARM_VARS                                          \
    ::Botanik::Vars::A("alarm_fieldbus", "BUS_DEAD", 0, 5, 0, 0, 0, 996,       \
        "a field device is not answering",                                     \
        "check the RS-485 wiring, termination and the device's own power"),

/** @brief How many devices are in backoff right now.
 *
 *  A status value rather than a second alarm: the alarm answers "is the bus
 *  healthy", and an operator's next question is always "how much of it is
 *  gone" - one dead sensor and a whole dead trunk raise the same boolean and
 *  need different people. Published, so it reaches MQTT and a SCADA master
 *  without anyone opening the console.
 *
 *  Register 902, after the library's two UART-rate parameters. */
#define BOTANIK_MODBUS_RTU_STATUS_VARS                                         \
    ::Botanik::Vars::SV("sys_fieldbus_dead", ::Botanik::Vars::T_U16, 0, 0, "", \
        902, "field devices not answering",                                    \
        "0 when every planned device is talking"),

#endif // MODBUS_RTU_ALARMS_H
