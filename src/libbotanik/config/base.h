#ifndef LIBBOTANIK_CONFIG_BASE_H
#define LIBBOTANIK_CONFIG_BASE_H

/** @file base.h
 *  @brief What every config header needs before it can declare a value: the
 *         site's overrides, then the feature flags.
 *
 *  Order is the whole point. A site's `config.h` must be read BEFORE any
 *  `#ifndef BOTANIK_*` default, or the default wins and the site's setting is
 *  silently ignored. Every `config/<concern>.h` includes this first, so that
 *  ordering holds no matter which one a translation unit reaches for.
 *
 *  This header declares no configuration, and deliberately does not define
 *  BOTANIK_CFG: it is include-guarded, so a second include would not restore a
 *  macro an earlier config header had undefined. Each config header defines and
 *  undefines BOTANIK_CFG for itself. */

#include <cstddef>
#include <cstdint>

// Site configuration

// Vendored builds (the library copied into an Arduino PLC IDE sketch) get no -D
// and no include path, so the vendoring step drops a generated
// botanik_site_config.h beside the library root: a quoted include resolves
// against the including file's own directory, reaching every library TU with no
// build-system cooperation. Skipped when the build already named a site config.
#if !defined(BOTANIK_SITE_CONFIG) && defined(__has_include)
#  if __has_include("botanik_site_config.h")
#    define BOTANIK_SITE_CONFIG "botanik_site_config.h"
#  endif
#endif

#include "libbotanik_site.h"

// The site's config.h, pulled in before the defaults so a site can override any
// of them build-wide. It is included with NONE of the library in scope, which is
// why it is its own file and not part of site.h: that one needs the library
// types, and this one cannot have them.
#if defined(BOTANIK_SITE_CONFIG)
#  include BOTANIK_SITE_CONFIG      // #define BOTANIK_* before the defaults
#endif

// Feature flags

#include "libbotanik_features.h"

#endif // LIBBOTANIK_CONFIG_BASE_H
