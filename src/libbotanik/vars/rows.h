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
#  include "libbotanik/codegen_abi.h"
#  if !defined(BOTANIK_SITE_CODEGEN_ABI) || BOTANIK_SITE_CODEGEN_ABI != BOTANIK_CODEGEN_ABI
#    error "the site's generated sources are from another generator version: regenerate them (make codegen)"
#  endif
#endif
// No site: Vars::Row is not declared at all, and the handful of library sites
// that read a row of their own guard on BOTANIK_SITE_ROWS. Binding a name to
// the inert sink instead would make every one of them read a defined nothing
// for the life of the run, which is the failure this namespace exists to
// turn into a compile error.

#endif // VARS_ROWS_H
