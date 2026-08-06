#ifndef PLC_FUNCTIONS_H
#define PLC_FUNCTIONS_H

/**
 * @file functions.h
 * @brief The FUNCTION half of the POU library. Umbrella header.
 *
 * @section plc_functions What counts as a function here
 *
 * IEC 61131-3 splits program organization units three ways, and so does this
 * directory tree:
 *
 * | POU | Here | Has |
 * |---|---|---|
 * | FUNCTION | `plc/functions/` | nothing. Same inputs, same output, always |
 * | FUNCTION_BLOCK | `plc/blocks/` | instance state, carried between scans |
 * | PROGRAM | the site's control programs | blocks, and the field |
 *
 * The rule that keeps the first two apart is mechanical rather than a
 * judgement call: **a function is a single expression in a header, and a block
 * needs a .cpp.** If what you are writing has to remember anything, it is a
 * block; if it does not, it belongs here, `constexpr`, so the compiler can fold
 * it into the caller and a test can check it in one line.
 *
 * | Header | Functions |
 * |---|---|
 * | @ref math.h | `limit` `in_range` `deadzone` `scale` `lerp` `sel` |
 * | @ref time.h | `seconds` `accumulate` `countdown` |
 */

// This header is the convenience roll-up: it exists only to pull in both
// function families, so every include here is re-exported rather than used.
#include "libbotanik/plc/functions/math.h" // limit / in_range / deadzone / scale / lerp / sel
#include "libbotanik/plc/functions/time.h" // seconds / accumulate / countdown

#endif // PLC_FUNCTIONS_H
