#ifndef PLC_BLOCKS_BLOCKS_H
#define PLC_BLOCKS_BLOCKS_H

/**
 * @file blocks.h
 * @brief Every function block, in one include.
 *
 * A block REMEMBERS - that is what separates it from a function (see
 * functions.h). Two consequences the whole family obeys: every timed block
 * takes @c dt_ms rather than reading a clock, so it is testable without one;
 * and every block provides reset(), which block.h enforces at compile time
 * because the scan driver resets each program on a control-authority
 * withdrawal.
 */

#include "libbotanik/plc/blocks/actuator.h" // Actuator
#include "libbotanik/plc/blocks/analog.h"   // Hysteresis / Hysteresis3 / Ramp / Deadband
#include "libbotanik/plc/blocks/filters.h"  // Ewma / MedianOf3 / MovingAverage
#include "libbotanik/plc/blocks/logic.h"    // RTrig / FTrig / Sr / Rs / Interlock
#include "libbotanik/plc/blocks/meter.h"    // Integral / Derivative
#include "libbotanik/plc/blocks/pid.h"      // Pid / PidTuning
#include "libbotanik/plc/blocks/timers.h"   // Ton / Tof / Tp
#include "libbotanik/plc/functions/functions.h"

#endif // PLC_BLOCKS_BLOCKS_H
