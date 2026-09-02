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

    /** @brief Capacity of the scheduler's task table. Fixed, no allocation;
     *  registrations past it are refused and counted, never silently dropped.
     *  @note gh_mlb41 registers 49 - the library's components plus 28 control
     *  programs - and it is the reference site, not a large one. 80 bytes a
     *  slot, so the headroom is cheap and running out is not. */
#ifndef BOTANIK_MAX_TASKS
#  define BOTANIK_MAX_TASKS 64
#endif
    BOTANIK_CFG(uint8_t, MAX_TASKS);

    /** @brief How many of a program's input rows are remembered, for the
     *  "which programs read this row" query alone. A program binding more than
     *  this still binds them all; only the answer to that query is a prefix,
     *  and input_overflow() says so. 2 bytes each, per program.
     *  @note gh_mlb41's widest program binds 24. */
#ifndef BOTANIK_PROGRAM_MAX_INPUTS
#  define BOTANIK_PROGRAM_MAX_INPUTS 24
#endif
    BOTANIK_CFG(uint8_t, PROGRAM_MAX_INPUTS);

    /** @brief How far past its own period a control program may run before
     *  SCAN_LATE is raised, in ms. Measured against the PROGRAM's period, so one
     *  number serves every rate. Two beats' worth: one late activation is the
     *  stagger, two in a row is the cycle running out of room. */
#ifndef BOTANIK_SCAN_LATE_MS
#  define BOTANIK_SCAN_LATE_MS (BOTANIK_TASK_PERIOD_MS * 2)
#endif
    BOTANIK_CFG(uint32_t, SCAN_LATE_MS);

    /** @brief How long after boot the control programs are held, in ms.
     *
     *  DEFENCE IN DEPTH. A program that runs before the field has answered
     *  reads every row's DECLARED value, and the damage is not the wrong
     *  output for one beat - it is that a momentary bring-up condition gets
     *  latched by something that outlives it. That has cost two bugs: a rot
     *  lockout held for five minutes, and a zone quorum alarm on every boot.
     *
     *  The operator's knob is `sys_startup_sec`; this is the floor a build
     *  with no variable table falls back to. Five seconds is about one RS-485
     *  sweep of a full site. */
#ifndef BOTANIK_STARTUP_HOLD_MS
#  define BOTANIK_STARTUP_HOLD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, STARTUP_HOLD_MS);

    /** @brief How long the unit sits with its laws stopped before it takes
     *  its ending, in ms. The mirror of BOTANIK_STARTUP_HOLD_MS: one is the
     *  field arriving, this is the record leaving. Retained rows and owed
     *  parameters reach flash at the top of it, and flash is tens of ms per
     *  row - a reset that races that write is what loses a watering date. */
#ifndef BOTANIK_SHUTDOWN_HOLD_MS
#  define BOTANIK_SHUTDOWN_HOLD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, SHUTDOWN_HOLD_MS);

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
