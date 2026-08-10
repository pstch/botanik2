#ifndef LIBBOTANIK_CONFIG_SCHEDULER_H
#define LIBBOTANIK_CONFIG_SCHEDULER_H

/** @file scheduler.h
 *  @brief The beat: its period, its budgets and its metrics.
 *
 *  One concern of the library's configuration. Every value defaults from
 *  an #ifndef-guarded BOTANIK_* macro a site may override; see
 *  libbotanik_config.h, which aggregates these and holds the rules. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Defined and undefined per file, not in base.h: see base.h.
#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

namespace Botanik { namespace Config {

    /** @name Scheduling
     *
     *  The beat, the rate classes tasks are grouped into, and the watchdog that
     *  outlives them. A task's RATE is one of the groups below; its PHASE is its
     *  place in the data flow and is written out in code, not configured.
     *  @{ */

    /** @brief Main task execution period (ms), i.e. the loop rate everything
     *  else divides down from. */
#ifndef BOTANIK_TASK_PERIOD_MS
#  define BOTANIK_TASK_PERIOD_MS 10
#endif
    BOTANIK_CFG(uint32_t, TASK_PERIOD_MS);

    /** @brief Every beat: the field bus, the log drain, the serial pumps.
     *
     *  This and the three below are the scheduler's rate classes. A task names
     *  one instead of carrying a raw period, so a rate is retuned in one place
     *  for everything that shares it. Each must be an integer multiple of
     *  TASK_PERIOD_MS. See docs/dev/scheduler.md for what sets each one. */
#ifndef BOTANIK_GROUP_FAST_MS
#  define BOTANIK_GROUP_FAST_MS BOTANIK_TASK_PERIOD_MS
#endif
    BOTANIK_CFG(uint32_t, GROUP_FAST_MS);

    /** @brief The operator-facing rate.
     *
     *  @note Has NO members: the console render runs in GROUP_SLOW. Kept because
     *  a rate between "every beat" and "dwell" is the obvious home for the next
     *  interactive thing, but tuning it affects nothing today - check tasks.h
     *  first. */
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
     *  registrations past it are refused and counted, never silently dropped. */
#ifndef BOTANIK_MAX_TASKS
#  define BOTANIK_MAX_TASKS 32
#endif
    BOTANIK_CFG(uint8_t, MAX_TASKS);

    /** @brief Phase step between control programs, applied by table index, so
     *  the scan's own programs spread over beats the way the registry's tasks
     *  do. Program @e i is offset by `i * this` (wrapped into its period).
     *
     *  Set to 0 to disable and have every program stay phase-locked to boot -
     *  the escape hatch for a site whose programs must share a beat. */
#ifndef BOTANIK_SCAN_STAGGER_MS
#  define BOTANIK_SCAN_STAGGER_MS BOTANIK_TASK_PERIOD_MS
#endif
    BOTANIK_CFG(uint32_t, SCAN_STAGGER_MS);

    /** @brief How far past its own period a control program may run before
     *  SCAN_LATE is raised, in ms.
     *
     *  Measured against the PROGRAM's period, not the beat, so one number serves
     *  every rate. Two beats' worth: one late activation is the stagger or a
     *  flash write, two in a row is the cycle running out of room. The alarm's
     *  own dwell stops a burst becoming an annunciation; this is only the
     *  threshold. */
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
     *  How the scheduler's self-instrumentation is smoothed before the health
     *  thresholds below read it. Window and buckets give the "recent peak";
     *  the EMA pairs give the displayed value.
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

    /** @brief CPU-load EMA time constants (ms), attack then decay.
     *
     *  Asymmetric on purpose: a load metric feeding an alarm threshold must
     *  rise at once and fall slowly, so a spike cannot be smoothed away before
     *  ALARM_CPU_LOAD_PCT sees it. Utils::ema_alpha() collapses any constant at
     *  or below TASK_PERIOD_MS to 1.0 (instant), which is what an attack of 0
     *  asks for; a larger value is a real time constant. */
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
