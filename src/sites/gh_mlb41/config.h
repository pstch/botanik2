// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/** @file config.h
 *  @brief gh_mlb41 overrides for libbotanik_config.h. */

#define BOTANIK_SITE_NAME "gh_mlb41"

// Guarded, so the precedence is command line > site > library default: a -D is
// already defined by the time this file is read, and an unguarded #define here
// would redefine it (warning and all) and quietly win. The site says what this
// greenhouse needs; it does not get to overrule someone building a cut-down
// image on purpose.
#ifndef BOTANIK_FEATURE_NETWORK
#  define BOTANIK_FEATURE_NETWORK 1
#endif
#ifndef BOTANIK_FEATURE_NTP
#  define BOTANIK_FEATURE_NTP     1
#endif
#ifndef BOTANIK_FEATURE_MQTT
#  define BOTANIK_FEATURE_MQTT    1
#endif

#endif // SITES_GH_MLB41_CONFIG_H
