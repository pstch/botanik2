// REVIEW_35: TODO
/**
 * @file platform.cpp
 * @brief The generic platform-health alarm table (see platform.h).
 *
 * Codes match those BotanikFirmware::loop() asserts via
 * AlarmModel::set_local().
 * Suffix: dwell thresholds (s; -1 disables), recent_s (0 = default), off_delay_s.
 */

#include "platform.h"

namespace Botanik { namespace Alarms {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const AlarmCondition PLATFORM_ALARMS[] = {
        // These name no variable: each is driven locally via set_local().
        // VAR_NONE, not {}: a bare index of 0 is a REAL row, so a zeroed ref
        // would quietly make a platform alarm track the first variable.
        // code        description                        var  on  off recent latch
        {"SYS_RST",  "Reset in the last five minutes",   {Vars::VAR_NONE},  -1,  0},
        {"CPU_XRUN", "Scheduler overrun (skipped beat)", {Vars::VAR_NONE},   0, -1,   0,     0},
        {"CPU_LOAD", "CPU load over threshold",          {Vars::VAR_NONE},   5,  5,   0,     300},
        {"STACK",    "Stack usage over threshold",       {Vars::VAR_NONE},   5,  5,   0,     300},
        {"CPU_JITT", "CPU jitter over threshold",        {Vars::VAR_NONE},   5,  5,   0,     300},
        // Sticky: a store that refused a write does not heal by itself, and
        // the consequence (settings silently reverting at the next reboot) is
        // invisible until it is far too late to connect cause and effect.
        {"FLASH_FLT","Persistent storage write failed", {Vars::VAR_NONE}, 0, -1,   0,     0,  0,
         "settings will not survive a reboot; check the KV region"},
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
