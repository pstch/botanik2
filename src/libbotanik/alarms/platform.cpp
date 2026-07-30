// REVIEW_35: TODO
/**
 * @file platform.cpp
 * @brief The generic platform-health alarm table (see platform.h).
 *
 * Codes match those BotanikFirmware::loop() asserts via
 * AlarmModel::set_local().
 * Suffix: dwell thresholds (s; -1 disables), recent_s (0 = default), off_delay_s.
 */

#include "libbotanik/alarms/platform.h"

namespace Botanik { namespace Alarms {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const AlarmCondition PLATFORM_ALARMS[] = {
        // code        description                       mask  on  off recent latch
        {"SYS_RST",  "Reset in the last five minutes",   0,   -1,  0},
        {"CPU_XRUN", "Scheduler overrun (skipped beat)", 0,    0, -1,   0,     0},
        {"CPU_LOAD", "CPU load over threshold",          0,    5,  5,   0,     300},
        {"STACK",    "Stack usage over threshold",       0,    5,  5,   0,     300},
        {"CPU_JITT", "CPU jitter over threshold",        0,    5,  5,   0,     300},
        {"RX_TORN",  "PLC torn read (handshake violation)", 0, 0, -1,   0,     0},
    };
    const uint8_t PLATFORM_ALARM_COUNT =
        (uint8_t)(sizeof(PLATFORM_ALARMS) / sizeof(PLATFORM_ALARMS[0]));
#pragma GCC diagnostic pop

    // Config::SITE_ALARMS_MAX hands a site whatever this table does not use, so
    // the budget must actually cover it - otherwise the model would clamp a
    // legal site table and silently drop its tail.
    static_assert(sizeof(PLATFORM_ALARMS) / sizeof(PLATFORM_ALARMS[0])
                      <= Config::PLATFORM_ALARMS_RESERVED,
                  "PLATFORM_ALARMS outgrew Config::PLATFORM_ALARMS_RESERVED");

}}
