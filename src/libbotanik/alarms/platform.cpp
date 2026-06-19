/**
 * @file platform.cpp
 * @brief The generic platform-health alarm table (see platform.h).
 *
 * Codes match those BotanikFirmware::loop() asserts via AlarmModel::set_local().
 * They latch active for 5 min (300 s) after they clear; the static suffix is the
 * dwell thresholds (seconds; -1 disables), then recent_s (0 = default) and latch_s.
 * The dynamic suffix is left zeroed (model-owned).
 */

#include "libbotanik/alarms/platform.h"

namespace Botanik { namespace Alarms {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const AlarmCondition PLATFORM_ALARMS[] = {
        // code        description                       mask logOn logOff mqttOn mqttOff recent latch
        {"SYS_RST",  "Reset in the last five minutes",   0,   -1,   0,     -1,    -1},
        // CPU_XRUN / RX_TORN: HealthMonitor holds these momentary events active for
        // ALARM_EVENT_HOLD_MS in the source (a proper latched state), so their table
        // latch_s is 0 - no double-latching. RX_TORN logs a START at once (it should
        // never fire: a cross-core torn read is a handshake violation).
        {"CPU_XRUN", "Scheduler overrun (skipped beat)", 0,   0,    -1,    -1,    -1,     0,     0},
        {"CPU_LOAD", "CPU load over threshold",          0,   5,    5,     -1,    -1,     0,     300},
        {"STACK",    "Stack usage over threshold",       0,   5,    5,     -1,    -1,     0,     300},
        {"CPU_JITT", "CPU jitter over threshold",        0,   5,    5,     -1,    -1,     0,     300},
        {"RX_TORN",  "PLC torn read (handshake violation)", 0, 0,   -1,    -1,    -1,     0,     0},
    };
    const uint8_t PLATFORM_ALARM_COUNT =
        (uint8_t)(sizeof(PLATFORM_ALARMS) / sizeof(PLATFORM_ALARMS[0]));
#pragma GCC diagnostic pop

}}
