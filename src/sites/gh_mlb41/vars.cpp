/**
 * @file vars.cpp
 * @brief What the generated table needs the site to supply: where the home
 *        fields resolve to.
 *
 * `extern` in the header because a namespace-scope array defined there would be
 * a definition in every including TU, and vars.h reaches ~9 of them.
 *
 * The value store, the string pool and the name index used to be here too. They
 * are the bank's now - sized once by Config::VAR_MAX, rather than by every site
 * counting its own rows.
 */

#include "vars.h"

namespace Site {

VarRef home_refs[HOME_FIELDS] = {};

}  // namespace Site
