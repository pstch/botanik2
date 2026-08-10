#ifndef PLC_ALARMS_H
#define PLC_ALARMS_H

/**
 * @file plc/alarms.h
 * @brief The scan cycle's own alarm row, spliced into the library's group.
 *
 * An ordinary `SCOPE_A` row, like the platform ones and the field bus's - the
 * library declares whatever the library raises (@ref library_vars.h).
 */

#include "libbotanik/vars/declare.h"    // the A() factory this row is written with

/** @brief A control program is not keeping its declared period.
 *
 *  Not the same as the beat being late: `CPU_XRUN` says the scheduler missed a
 *  beat, this says a PROGRAM ran later than the period it asked for - a long
 *  run pushing the ones after it, or a site asking for more work per second
 *  than the cycle holds.
 *
 *  `on_s` is 5: lateness comes in bursts (a flash write, a DNS retry) and one
 *  late activation is not worth an alarm. `off_s` is 30, because "it caught up
 *  once" is not evidence the cycle has room again.
 *
 *  Register 997, after the field bus at 996 in the library's 900-999 reserve. */
#define BOTANIK_PLC_SCAN_ALARM_VARS                                            \
    ::Botanik::Vars::A("alarm_scan_late", "SCAN_LATE", 5, 30, 0, 0, 0, 997,    \
        "a control program is missing its period",                             \
        "check the TASKS page and the per-program cost; something is "         \
        "overrunning or the site asks for more than the beat holds"),

#endif // PLC_ALARMS_H
