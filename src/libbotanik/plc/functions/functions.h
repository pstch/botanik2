#ifndef PLC_FUNCTIONS_FUNCTIONS_H
#define PLC_FUNCTIONS_FUNCTIONS_H

/**
 * @file functions.h
 * @brief The stateless half of the POU vocabulary, in one include.
 *
 * IEC 61131-3 splits a program's vocabulary by whether it remembers anything.
 * A FUNCTION does not: same inputs, same output, every scan. A FUNCTION_BLOCK
 * does, and lives in blocks/. The mechanical form of that rule here: a function
 * is a single constexpr expression in a header, a block needs a .cpp.
 */

#include "libbotanik/plc/functions/math.h"
#include "libbotanik/plc/functions/psychro.h"
#include "libbotanik/plc/functions/time.h"

#endif // PLC_FUNCTIONS_FUNCTIONS_H
