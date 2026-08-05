// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/** @file config.h
 *  @brief gh_mlb41 overrides for libbotanik_config.h. */

#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "gh_mlb41"
#endif

// Guarded, so the precedence is command line > site > library default: a -D is
// already defined by the time this file is read, and an unguarded #define here
// would redefine it (warning and all) and quietly win. The site says what this
// greenhouse needs; it does not get to overrule someone building a cut-down
// image on purpose.
// The library defaults are a minimal core (console + home + shell + logging +
// the scan cycle); everything below is what THIS greenhouse additionally needs.
// gh_mlb41 is the reference site, so it is also the worked example of a full
// profile - and, because the test and pyte builds compile the selected site,
// it is what keeps those suites exercising the whole feature surface.

// Each block is conditional on its substrate not having been switched OFF on
// the command line. An `#ifndef` alone is not enough: with
// -DBOTANIK_FEATURE_CONSOLE=OFF the page flags are still undefined here, so the
// site would define them to 1 and the dependency rules would then reject a
// contradiction the BUILDER never wrote. The site asks for pages if there is a
// console to put them on - it does not overrule someone building headless.
//
// `!defined(X) || X` reads as "unless explicitly turned off", which is the whole
// rule: only the command line can have defined it by now.

// --- Console: the full page set ------------------------------------------
#if !defined(BOTANIK_FEATURE_CONSOLE) || BOTANIK_FEATURE_CONSOLE
#  if !defined(BOTANIK_FEATURE_LOGGING) || BOTANIK_FEATURE_LOGGING
#    ifndef BOTANIK_FEATURE_LOGS_PAGE
#      define BOTANIK_FEATURE_LOGS_PAGE  1
#    endif
#  endif
#  ifndef BOTANIK_FEATURE_ALARMS_PAGE
#    define BOTANIK_FEATURE_ALARMS_PAGE  1
#  endif
#  ifndef BOTANIK_FEATURE_PARAMS_PAGE
#    define BOTANIK_FEATURE_PARAMS_PAGE  1
#  endif
#  ifndef BOTANIK_FEATURE_MONITOR_PAGE
#    define BOTANIK_FEATURE_MONITOR_PAGE 1
#  endif
#  ifndef BOTANIK_FEATURE_TASKS_PAGE
#    define BOTANIK_FEATURE_TASKS_PAGE   1
#  endif
#  ifndef BOTANIK_FEATURE_NOTES_PAGE
#    define BOTANIK_FEATURE_NOTES_PAGE   1
#  endif
#endif

// --- Diagnostics: raw logs on the second serial port ---------------------
#if !defined(BOTANIK_FEATURE_LOGGING) || BOTANIK_FEATURE_LOGGING
#  ifndef BOTANIK_FEATURE_LOGS_MONITOR
#    define BOTANIK_FEATURE_LOGS_MONITOR 1
#  endif
#endif

// --- Network, and what rides on it ---------------------------------------
#if !defined(BOTANIK_FEATURE_NETWORK) || BOTANIK_FEATURE_NETWORK
#  ifndef BOTANIK_FEATURE_NETWORK
#    define BOTANIK_FEATURE_NETWORK 1
#  endif
#  ifndef BOTANIK_FEATURE_NTP
#    define BOTANIK_FEATURE_NTP     1
#  endif
#  ifndef BOTANIK_FEATURE_MQTT
#    define BOTANIK_FEATURE_MQTT    1
#  endif
#  if (!defined(BOTANIK_FEATURE_LOGGING) || BOTANIK_FEATURE_LOGGING) \
      && BOTANIK_FEATURE_MQTT
#    ifndef BOTANIK_FEATURE_LOGS_PUBLISH
#      define BOTANIK_FEATURE_LOGS_PUBLISH   1
#    endif
#    ifndef BOTANIK_FEATURE_ALARMS_PUBLISH
#      define BOTANIK_FEATURE_ALARMS_PUBLISH 1
#    endif
#  endif
#endif

// --- PLC: the control programs use Ton / Sr / Deadband -------------------
#ifndef BOTANIK_FEATURE_PLC_BLK_BASE
#  define BOTANIK_FEATURE_PLC_BLK_BASE    1
#endif
#ifndef BOTANIK_FEATURE_PLC_BLK_FILTERS
#  define BOTANIK_FEATURE_PLC_BLK_FILTERS 1
#endif

#endif // SITES_GH_MLB41_CONFIG_H
