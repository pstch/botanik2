// REVIEW_35: WIP
#ifndef ALARMS_PLATFORM_H
#define ALARMS_PLATFORM_H

/** @file platform.h
 *  @brief The library's own alarm rows, for a site's variable table.
 *
*  These are ordinary `SCOPE_A` rows - the same `A()` factory a site writes its
*  own alarms with - and they go in the same table. There is no second alarm
*  table, no merge and no reserved index range: `AlarmController` scans the bank
*  for `SCOPE_A` and finds site and platform conditions together, in table order.
*
*  HealthMonitor drives them the way anything drives a variable: it resolves the
*  names once at bring-up and writes the boolean. It has no alarm-model API of
*  its own any more.
*
*  These rows reach a site's table through BOTANIK_LIBRARY_VARS, the one splice
*  point for everything libbotanik declares - see vars/library.h, which is also
*  where the placement rules and the failure mode for leaving it out are written
*  down. This header only owns the rows. */

#include "libbotanik/vars/declare.h" // the A() factory these rows are written with

/** @brief The platform-health alarm rows. One group of BOTANIK_LIBRARY_VARS.
 *
 *  Dwell columns are the `A()` ones: on_s, off_s, recent_s, hold_on_s, min_on_s
 *  (-1 disables an announce, 0 takes the default). Fully qualified, because a
 *  site table may be in a namespace that has not pulled `Botanik::Vars` in; each
 *  row carries its own trailing comma, so an empty group vanishes cleanly. */
#define BOTANIK_PLATFORM_ALARM_VARS                                            \
    /* The window is Config::HEALTH_RESET_WINDOW_MS; the text says "recently"  \
       rather than repeating the number, which drifted the moment one moved. */\
    ::Botanik::Vars::A("alarm_system_reset", "SYS_RST", -1, 0, 0, 0, 0,        \
        "Unit reset recently", ""),                                            \
    ::Botanik::Vars::A("alarm_cpu_xrun", "CPU_XRUN", 0, -1, 0, 0, 0,           \
        "Scheduler overrun (skipped beat)", ""),                               \
    ::Botanik::Vars::A("alarm_cpu_load", "CPU_LOAD", 5, 5, 0, 300, 0,          \
        "CPU load over threshold", ""),                                        \
    ::Botanik::Vars::A("alarm_stack", "STACK", 5, 5, 0, 300, 0,                \
        "Stack usage over threshold", ""),                                     \
    ::Botanik::Vars::A("alarm_cpu_jitter", "CPU_JITT", 5, 5, 0, 300, 0,        \
        "CPU jitter over threshold", ""),                                      \
    /* Sticky: a store that refused a write does not heal by itself, and the   \
       consequence (parameters silently reverting at the next reboot) is       \
       invisible until it is far too late to connect cause and effect. */      \
    ::Botanik::Vars::A("alarm_flash_error", "FLASH_FLT", 0, -1, 0, 0, 0,       \
        "Persistent storage write failed",                                     \
        "parameters will not survive a reboot; check the KV region"),

#endif // ALARMS_PLATFORM_H
