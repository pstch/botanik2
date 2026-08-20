#ifndef LIBBOTANIK_CONFIG_ALARMS_H
#define LIBBOTANIK_CONFIG_ALARMS_H

/** @file alarms.h
 *  @brief Alarm dwell policy and the platform-health thresholds. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Health and alarms
     *
     *  The platform-health thresholds HealthMonitor asserts from the metrics
     *  above, and the sizing of the model those alarms land in.
     *  @{ */

    /** @brief Platform-health alarm thresholds: CPU_LOAD / CPU_JITT fire when
     *  the rolling-peak load / jitter exceeds this percent of the task period;
     *  STACK fires above this percent stack usage. Per-alarm latch is in the
     *  table (hold_s).
     *
     *  The load and jitter thresholds agree at 50% today but threshold
     *  different quantities (duty cycle vs scheduling lateness), so they stay
     *  two knobs. */
#ifndef BOTANIK_ALARM_CPU_LOAD_PCT
#  define BOTANIK_ALARM_CPU_LOAD_PCT 50
#endif
    BOTANIK_CFG(uint8_t, ALARM_CPU_LOAD_PCT);
#ifndef BOTANIK_ALARM_CPU_JITTER_PCT
#  define BOTANIK_ALARM_CPU_JITTER_PCT 50
#endif
    BOTANIK_CFG(uint8_t, ALARM_CPU_JITTER_PCT);
#ifndef BOTANIK_ALARM_STACK_PCT
#  define BOTANIK_ALARM_STACK_PCT 80
#endif
    BOTANIK_CFG(uint8_t, ALARM_STACK_PCT);

    /** @brief How long after boot the SYS_RST alarm stays asserted, i.e. the
     *  window in which "this unit has recently reset" is still news. Read by
     *  HealthMonitor and by the SYS_RST description text, so the duration is
     *  stated once rather than in the code and the operator's string both. */
#ifndef BOTANIK_HEALTH_RESET_WINDOW_MS
#  define BOTANIK_HEALTH_RESET_WINDOW_MS 300000
#endif
    BOTANIK_CFG(uint32_t, HEALTH_RESET_WINDOW_MS);

    /** @brief How long after boot a CHANGE of quality or of program health is
     *  bring-up rather than news, and so is adopted without a log line.
     *
     *  Everything settles in the first seconds - every field row goes valid as
     *  its device first answers, every program leaves INHIBITED on its first
     *  activation - and announcing all of it is several hundred debug lines in
     *  one breath, which the rate limiter then sheds along with whatever else
     *  was being said. Long enough to cover the slowest poll rung, so a row is
     *  adopted at its first real reading and not at its default.
     *  @note Alarms are NOT quiet here. This is the change log; a condition
     *  that matters raises its own row and annunciates on its own dwell. */
#ifndef BOTANIK_HEALTH_QUIET_MS
#  define BOTANIK_HEALTH_QUIET_MS 15000
#endif
    BOTANIK_CFG(uint32_t, HEALTH_QUIET_MS);

    /** @brief Maximum number of alarm notifiers the model fans out to. */
#ifndef BOTANIK_ALARM_NOTIFIERS_MAX
#  define BOTANIK_ALARM_NOTIFIERS_MAX 4
#endif
    BOTANIK_CFG(uint8_t, ALARM_NOTIFIERS_MAX);

    /** @brief How long a cleared alarm stays in the ALARMS page's RECENT view
     *  (s). A condition's own recent_s overrides it (0 = this default, < 0 =
     *  never expires); the platform alarms in vars.cpp all set their own. */
#ifndef BOTANIK_ALARM_RECENT_S
#  define BOTANIK_ALARM_RECENT_S (7 * 24 * 3600)
#endif
    BOTANIK_CFG(int32_t, ALARM_RECENT_S);

    /** @brief SCOPE_A rows the alarm model has room for - the site's own and the
     *  library's (alarms/platform.h) together, since they share one table. What
     *  AlarmController sizes itself on; a table with more loses its tail, which
     *  BOTANIK_ASSERT catches. One number, because there is one table.
     *
     *  @warning **64 is a ceiling, not a default.** It may be lowered to save
     *  RAM, but it cannot be raised: `Alarms::alarm_bit()` shifts into a
     *  `uint64_t` to build the alarm bar's change signature, so a 65th slot
     *  would make that shift undefined. A `static_assert` in alarms/alarms.h
     *  enforces it. Widen the mask there first if a site ever needs more. */
#ifndef BOTANIK_ALARM_SLOTS
#  define BOTANIK_ALARM_SLOTS 64
#endif
    BOTANIK_CFG(uint8_t, ALARM_SLOTS);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_ALARMS_H
