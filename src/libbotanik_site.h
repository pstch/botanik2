#ifndef LIBBOTANIK_SITE_H
#define LIBBOTANIK_SITE_H

/** @file libbotanik_site.h
 *  @brief Resolves the selected site's two include paths.
 *
 *  A site lives in one directory, in this tree or in the consumer's project (the
 *  PLC IDE's LLSketch dir). Two paths are derived from it:
 *  BOTANIK_SITE_HEADER (its `site.h`, the payload + firmware subclass a harness
 *  includes) and BOTANIK_SITE_CONFIG (its `config.h`, the build-wide Config
 *  overrides libbotanik_config.h pulls in before its own defaults).
 *
 *  Resolution order:
 *   1. Either macro already defined - used as is. CMake takes this path: it
 *      turns BOTANIK_SITE_DIR into two absolute paths, so a site directory
 *      anywhere on disk works and nothing has to sit on the include path.
 *   2. BOTANIK_SITE=<name> - derive `sites/<name>/{site.h,config.h}`, resolved
 *      against this tree's `src/`. The in-tree convenience form.
 *   3. Neither - no site. The library compiles standalone against its own
 *      Config defaults, which is what an IDE that cannot pass -D does when it
 *      builds the imported Arduino library (see docs/quickstart). Only the
 *      site's own TUs need the site paths, and those are compiled by the
 *      integrator's project, which does define them.
 *
 *  A build that needs the site header but has no way to name it is a build
 *  error at the point of use, not here: BOTANIK_SITE_HEADER simply stays
 *  undefined, and #include BOTANIK_SITE_HEADER fails loudly.
 */

// Two-level stringize so BOTANIK_SITE expands before it's quoted.
#define BOTANIK_SITE_STR_(x) #x
#define BOTANIK_SITE_STR(x) BOTANIK_SITE_STR_(x)

#ifdef BOTANIK_SITE
#  ifndef BOTANIK_SITE_HEADER
#    define BOTANIK_SITE_HEADER BOTANIK_SITE_STR(sites/BOTANIK_SITE/site.h)
#  endif
#  ifndef BOTANIK_SITE_CONFIG
#    define BOTANIK_SITE_CONFIG BOTANIK_SITE_STR(sites/BOTANIK_SITE/config.h)
#  endif
#endif

#endif
