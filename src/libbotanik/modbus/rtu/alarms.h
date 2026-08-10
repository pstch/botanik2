#ifndef MODBUS_RTU_ALARMS_H
#define MODBUS_RTU_ALARMS_H

/** @file modbus/rtu/alarms.h
 *  @brief The field bus's own rows, for a site's variable table.
 *
 *  Ordinary rows written with the same factories a site uses, spliced into every
 *  site's table through BOTANIK_LIBRARY_VARS (library_vars.h): the code that
 *  raises a row and the declaration of that row belong in one place.
 *  `RtuClient` drives both by name, resolved once at bring-up.
 */

#include "libbotanik/vars/declare.h"    // the A/SV factories these rows are written with

/** @brief The field bus alarm: a device has stopped answering.
 *
 *  Raised when a device has failed its retry and gone into backoff, not on the
 *  first missed answer: one lost frame on an RS-485 run is weather. The backoff
 *  IS the dwell, so `on_s` is 0.
 *
 *  `off_s` is not: a device that answers one probe and then goes quiet again is
 *  a loose connector, so five seconds of an answering bus is the evidence that
 *  it came back.
 *
 *  Register 996: the top of the library's 900-999 reserve, beside the platform
 *  alarms at 990-995. */
/** @brief And the one that says it is the SEGMENT rather than a device.
 *
 *  Raised when EVERY planned device has stopped answering, which is a different
 *  fault with a different remedy: one dead sensor is a device, and twenty at
 *  once is the trunk, the termination, the master's own transceiver, or a
 *  second master talking over it. They cannot be told apart from the boolean
 *  above, and a babbling unit makes the distinction matter - its bytes corrupt
 *  whichever transaction is in flight, so the miss is charged to whoever was
 *  being polled and every healthy device is faulted in turn.
 *
 *  Register 998: 997 is the scan cycle's alarm_scan_late. */
#define BOTANIK_MODBUS_RTU_ALARM_VARS                                          \
    ::Botanik::Vars::A("alarm_fieldbus", "BUS_DEAD", 0, 5, 0, 0, 0, 996,       \
        "a field device is not answering",                                     \
        "check the RS-485 wiring, termination and the device's own power"),    \
    ::Botanik::Vars::A("alarm_fieldbus_line", "BUS_LINE", 0, 5, 0, 0, 0, 998,  \
        "the whole segment has gone quiet",                                    \
        "the trunk, the termination, this unit's transceiver, or a second "    \
        "master - not twenty devices that failed at once"),

/** @brief How many devices are in backoff right now.
 *
 *  A status value rather than a second alarm: the alarm says whether the bus is
 *  healthy, this says how much of it is gone (one dead sensor and a whole dead
 *  trunk raise the same boolean and need different people). Published, so it
 *  reaches MQTT and a SCADA master without anyone opening the console.
 *
 *  Register 902, after the library's two UART-rate parameters. */
/** @brief And the two counters that say WHAT KIND of quiet it is.
 *
 *  The framer has always kept these apart, precisely because they point at
 *  different physical faults, and nothing read them: the whole diagnosis
 *  reaching an operator was one boolean and one count. A bus failing on CRC
 *  errors is a cable, a missing terminator, or a baud or parity mismatch; one
 *  failing on clean timeouts is power or addressing; and bytes arriving out of
 *  turn are a second master, a duplicate address, or a device answering past
 *  its deadline. Published, so the answer reaches MQTT and a SCADA master
 *  without anyone opening the console.
 *
 *  Registers 903 and 904, after sys_fieldbus_dead. */
#define BOTANIK_MODBUS_RTU_STATUS_VARS                                         \
    ::Botanik::Vars::SV("sys_fieldbus_dead", ::Botanik::Vars::T_U16, 0, 0, "", \
        902, "field devices not answering",                                    \
        "0 when every planned device is talking"),                             \
    ::Botanik::Vars::SV("sys_fieldbus_crc", ::Botanik::Vars::T_U16, 0, 0, "",  \
        903, "frames that failed their checksum",                              \
        "rising means the line: termination, stub length, or the wrong baud"), \
    ::Botanik::Vars::SV("sys_fieldbus_stray", ::Botanik::Vars::T_U16, 0, 0, "",\
        904, "bytes that arrived out of turn",                                 \
        "rising means a second master, a duplicate address, or a device "      \
        "answering after its turn had passed"),

#endif // MODBUS_RTU_ALARMS_H
