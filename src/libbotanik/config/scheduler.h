#ifndef LIBBOTANIK_CONFIG_SCHEDULER_H
#define LIBBOTANIK_CONFIG_SCHEDULER_H

/** @file scheduler.h
 *  @brief The beat: its period, its budgets and its metrics. */

#include "libbotanik/config/base.h"

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Scheduling
     *
     *  The beat, the rate classes, and the watchdog that outlives them. A task's
     *  RATE is a group below; its PHASE is written out in code, not configured.
     *  @{ */

    /** @brief Main task execution period (ms), i.e. the loop rate everything
     *  else divides down from. */
#ifndef BOTANIK_TASK_PERIOD_MS
#  define BOTANIK_TASK_PERIOD_MS 10
#endif
    BOTANIK_CFG(uint32_t, TASK_PERIOD_MS);

    /** @brief Every beat: the field bus, the log drain, the serial pumps. This
     *  and the three below are the rate classes; each must be an integer
     *  multiple of TASK_PERIOD_MS. See docs/dev/scheduler.md. */
#ifndef BOTANIK_GROUP_FAST_MS
#  define BOTANIK_GROUP_FAST_MS BOTANIK_TASK_PERIOD_MS
#endif
    BOTANIK_CFG(uint32_t, GROUP_FAST_MS);

    /** @brief The operator-facing rate.
     *  @note Has NO members today - the console render runs in GROUP_SLOW - so
     *  tuning it affects nothing. Check tasks.h first. */
#ifndef BOTANIK_GROUP_MAIN_MS
#  define BOTANIK_GROUP_MAIN_MS 50
#endif
    BOTANIK_CFG(uint32_t, GROUP_MAIN_MS);

    /** @brief Dwell and config: alarm on_s/off_s are in SECONDS, and a parameter
     *  edit is bounded by how fast a human types. Also the console render, which
     *  is why TX_COLD_TARGET_MS must stay above this. */
#ifndef BOTANIK_GROUP_SLOW_MS
#  define BOTANIK_GROUP_SLOW_MS 100
#endif
    BOTANIK_CFG(uint32_t, GROUP_SLOW_MS);

    /** @brief Housekeeping measured in seconds - the input-expiry sweep, which
     *  is a full linear scan of the variable table run to resolve timeouts of
     *  3x a poll group's period. */
#ifndef BOTANIK_GROUP_BACKGROUND_MS
#  define BOTANIK_GROUP_BACKGROUND_MS 500
#endif
    BOTANIK_CFG(uint32_t, GROUP_BACKGROUND_MS);

    /** @brief Capacity of the scheduler's task table. Fixed, no allocation;
     *  registrations past it are refused and counted, never silently dropped.
     *  @note gh_mlb41 registers 31 and a site adds its own on top, so the
     *  headroom is the point. 80 bytes a slot. */
#ifndef BOTANIK_MAX_TASKS
#  define BOTANIK_MAX_TASKS 48
#endif
    BOTANIK_CFG(uint8_t, MAX_TASKS);

    /** @brief How far past its own period a control program may run before
     *  SCAN_LATE is raised, in ms. Measured against the PROGRAM's period, so one
     *  number serves every rate. Two beats' worth: one late activation is the
     *  stagger, two in a row is the cycle running out of room. */
#ifndef BOTANIK_SCAN_LATE_MS
#  define BOTANIK_SCAN_LATE_MS (BOTANIK_TASK_PERIOD_MS * 2)
#endif
    BOTANIK_CFG(uint32_t, SCAN_LATE_MS);

    /** @brief Hardware watchdog timeout. Clamped to what the IWDG can express. */
#ifndef BOTANIK_HW_WATCHDOG_MS
#  define BOTANIK_HW_WATCHDOG_MS 10000
#endif
    BOTANIK_CFG(uint32_t, HW_WATCHDOG_MS);

    /** @} */

    /** @name Performance metrics
     *
     *  How the self-instrumentation is smoothed before the health thresholds
     *  read it: window and buckets give the recent peak, the EMA pairs the
     *  displayed value.
     *  @{ */

    /** @brief Sliding window for the "recent peak" CPU/stack metrics, and the
     *  number of rotating buckets approximating it. The scheduler divides the
     *  window by TASK_PERIOD_MS to get the sample count the rolling-max filters
     *  are configured with. */
#ifndef BOTANIK_LOAD_METRIC_WINDOW_MS
#  define BOTANIK_LOAD_METRIC_WINDOW_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_WINDOW_MS);
#ifndef BOTANIK_LOAD_METRIC_BUCKETS
#  define BOTANIK_LOAD_METRIC_BUCKETS 10
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_BUCKETS);

    /** @brief CPU-load EMA time constants (ms), attack then decay. Asymmetric on
     *  purpose: a metric feeding an alarm threshold must rise at once and fall
     *  slowly, or a spike is smoothed away before ALARM_CPU_LOAD_PCT sees it.
     *  @note ema_alpha() collapses any constant <= TASK_PERIOD_MS to instant. */
#ifndef BOTANIK_CPU_LOAD_EMA_ATTACK_MS
#  define BOTANIK_CPU_LOAD_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_ATTACK_MS);
#ifndef BOTANIK_CPU_LOAD_EMA_DECAY_MS
#  define BOTANIK_CPU_LOAD_EMA_DECAY_MS 2000
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_DECAY_MS);

    /** @brief CPU-jitter EMA time constants (ms), attack then decay. Same
     *  asymmetry as the load pair, with a longer tail. */
#ifndef BOTANIK_CPU_JITTER_EMA_ATTACK_MS
#  define BOTANIK_CPU_JITTER_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_ATTACK_MS);
#ifndef BOTANIK_CPU_JITTER_EMA_DECAY_MS
#  define BOTANIK_CPU_JITTER_EMA_DECAY_MS 5000
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_DECAY_MS);

    /** @} */

} }

#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_SCHEDULER_H
