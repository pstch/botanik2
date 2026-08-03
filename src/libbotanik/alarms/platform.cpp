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
        // These read no variable: each is driven locally via set_local(),
        // and the model leaves their resolved ref at VAR_NONE. The name is
        // still their identity - it is what `alarms ack` and the pages key on.
        // code         variable                description                            on  off  rec latch
        {"SYS_RST",   "alarm_system_reset", "Reset in the last five minutes",    -1,  0},
        {"CPU_XRUN",  "alarm_cpu_xrun",     "Scheduler overrun (skipped beat)",   0, -1,   0,   0},
        {"CPU_LOAD",  "alarm_cpu_load",     "CPU load over threshold",            5,  5,   0, 300},
        {"STACK",     "alarm_stack",        "Stack usage over threshold",         5,  5,   0, 300},
        {"CPU_JITT",  "alarm_cpu_jitter",   "CPU jitter over threshold",          5,  5,   0, 300},
        // Sticky: a store that refused a write does not heal by itself, and
        // the consequence (settings silently reverting at the next reboot) is
        // invisible until it is far too late to connect cause and effect.
        {"FLASH_FLT", "alarm_flash_error",  "Persistent storage write failed",    0, -1,   0,   0, 0,
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
