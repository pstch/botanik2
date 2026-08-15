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
 * because a control-authority withdrawal resets each program on that
 * withdrawal.
 */

#include "libbotanik/plc/blocks/actuator.h"
#include "libbotanik/plc/blocks/analog.h"
#include "libbotanik/plc/blocks/filters.h"
#include "libbotanik/plc/blocks/logic.h"
#include "libbotanik/plc/blocks/meter.h"
#include "libbotanik/plc/blocks/pid.h"
#include "libbotanik/plc/blocks/timers.h"
#include "libbotanik/plc/functions/functions.h"

#endif // PLC_BLOCKS_BLOCKS_H
