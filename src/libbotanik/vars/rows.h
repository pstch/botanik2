#ifndef VARS_ROWS_H
#define VARS_ROWS_H

/**
 * @file rows.h
 * @brief `Vars::Row::<name>` - the generated index of every row, library and
 *        site, in one namespace.
 *
 * The site's generator emits it and the build points BOTANIK_SITE_ROWS at it,
 * exactly as BOTANIK_SITE_CONFIG carries the config. So a library consumer
 * naming a row it needs is a COMPILE error when no table declares it, rather
 * than a reference that resolves to the sink for the life of the run.
 */

#include <cstdint>

#include "libbotanik/vars/ref.h"

#if defined(BOTANIK_SITE_ROWS)
#  include BOTANIK_SITE_ROWS
#else
// No site: every library row resolves to the bank's inert sink. A consumer
// reusing a component without a variable table still compiles.
#  include "libbotanik/vars/rows_none.gen.h"
#endif

#endif // VARS_ROWS_H
