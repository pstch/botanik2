#ifndef LIBBOTANIK_SITE_H
#define LIBBOTANIK_SITE_H

/** @file libbotanik_site.h
 *  @brief Resolves the selected site's include paths from a single site name.
 *
 *  The build (and the IDE) sets one thing: -DBOTANIK_SITE=<name>. From it we
 *  derive BOTANIK_SITE_HEADER ("sites/<name>/site.h", the payload + firmware
 *  subclass the harness includes) and BOTANIK_SITE_CONFIG ("sites/<name>/config.h",
 *  the build-wide Config overrides libbotanik_config.h pulls in before its
 *  defaults). Both stay overridable (#ifndef) as an escape hatch. */

#ifndef BOTANIK_SITE
#  error "BOTANIK_SITE not set: pass -DBOTANIK_SITE=<name> (site under src/sites/)"
#endif

// Two-level stringize so BOTANIK_SITE expands before it's quoted.
#define BOTANIK_SITE_STR_(x) #x
#define BOTANIK_SITE_STR(x) BOTANIK_SITE_STR_(x)

#ifndef BOTANIK_SITE_HEADER
#  define BOTANIK_SITE_HEADER BOTANIK_SITE_STR(sites/BOTANIK_SITE/site.h)
#endif

#ifndef BOTANIK_SITE_CONFIG
#  define BOTANIK_SITE_CONFIG BOTANIK_SITE_STR(sites/BOTANIK_SITE/config.h)
#endif

#endif
