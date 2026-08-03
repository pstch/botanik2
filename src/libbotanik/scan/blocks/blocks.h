#ifndef SCAN_BLOCKS_H
#define SCAN_BLOCKS_H

/**
 * @file blocks.h
 * @brief Function blocks: the small stateful primitives control programs are
 *        built from. Umbrella header for the whole library.
 *
 * These are the PLC idioms - on-delay, edge, latch, hysteresis, PID - as
 * ordinary C++ objects. A program declares them as statics or members and calls
 * them each scan; they hold their own state and nothing else needs to know
 * about it.
 *
 * Every timed block takes @p dt_ms (ScanContext::dt_ms), the ACTUAL elapsed
 * time since that task last ran, rather than reading a clock itself. Two
 * reasons: a task that ran late must not lose the time it was late by, and a
 * block driven purely by its inputs is trivially testable without a clock.
 *
 * All state is RAM; unlike the descriptor tables there is no flash-residency
 * constraint here, so these are ordinary classes.
 *
 * | Header | Blocks |
 * |---|---|
 * | @ref timers.h   | `Ton` `Tof` `Tp` |
 * | @ref logic.h    | `RTrig` `FTrig` `Sr` `Rs` `Interlock` |
 * | @ref analog.h   | `Hysteresis` `Hysteresis3` `Ramp` `Deadband` |
 * | @ref filters.h  | `Ewma` `MedianOf3` `MovingAverage<N>` |
 * | @ref meter.h    | `Integral` `Derivative` |
 * | @ref pid.h      | `PidTuning` `Pid` |
 * | @ref actuator.h | `ActuatorConfig` `Actuator` |
 *
 * Include this header for all of them, or one of the above when a translation
 * unit only needs its corner.
 */

#include "common.h"

#include "timers.h"
#include "logic.h"
#include "analog.h"
#include "filters.h"
#include "meter.h"
#include "pid.h"
#include "actuator.h"

#endif // SCAN_BLOCKS_H
