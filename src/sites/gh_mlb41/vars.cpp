/**
 * @file vars.cpp
 * @brief The site's value store: the one definition of the RAM that sits
 *        parallel to the (const, flash-resident) variable table in vars.h.
 *
 * Four arrays and nothing else. They are declared `extern` in the header
 * because a namespace-scope array defined there would be a definition in every
 * including TU, and vars.h reaches ~9 of them.
 */

#include "vars.h"

namespace Site {

Value    values[VAR_TOTAL]     = {};
char     pool[STR_POOL_BYTES]  = {};
uint16_t name_index[VAR_TOTAL] = {};
VarRef   home_refs[HOME_FIELDS] = {};

}  // namespace Site
