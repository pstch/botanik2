// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/** @file config.h
 *  @brief gh_mlb41 overrides for libbotanik_config.h. */

#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "gh_mlb41"
#endif

// NOTE: precedence is command line > site > library default

// Factory values for the library's own parameter rows (declared in
// vars/library.h, since libbotanik reads them by name). These are what a wiped
// store comes back as; an operator retunes them from the PARAMS page.
#ifndef BOTANIK_NET_IP
#  define BOTANIK_NET_IP        "192.168.1.42"
#endif
#ifndef BOTANIK_NET_MASK
#  define BOTANIK_NET_MASK      "255.255.255.0"
#endif
#ifndef BOTANIK_NET_GW
#  define BOTANIK_NET_GW        "192.168.1.1"
#endif
#ifndef BOTANIK_NET_DNS1
#  define BOTANIK_NET_DNS1      "8.8.8.8"
#endif
#ifndef BOTANIK_NET_DNS2
#  define BOTANIK_NET_DNS2      "4.4.4.4"
#endif
#ifndef BOTANIK_NTP_SRV1
#  define BOTANIK_NTP_SRV1      "0.fr.pool.ntp.org"
#endif
#ifndef BOTANIK_NTP_SRV2
#  define BOTANIK_NTP_SRV2      "1.fr.pool.ntp.org"
#endif
#ifndef BOTANIK_MQTT_HOST
#  define BOTANIK_MQTT_HOST     "192.168.1.43"
#endif
#ifndef BOTANIK_MQTT_PORT
#  define BOTANIK_MQTT_PORT     58443
#endif
#ifndef BOTANIK_GEO_LAT_DEG
#  define BOTANIK_GEO_LAT_DEG   48.07
#endif
#ifndef BOTANIK_GEO_LON_DEG
#  define BOTANIK_GEO_LON_DEG   1.32
#endif

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

#ifndef BOTANIK_FEATURE_PLC_BLK_BASE
#  define BOTANIK_FEATURE_PLC_BLK_BASE    1
#endif
#ifndef BOTANIK_FEATURE_PLC_BLK_FILTERS
#  define BOTANIK_FEATURE_PLC_BLK_FILTERS 1
#endif

#ifndef BOTANIK_FEATURE_SUN_TRACKER
#  define BOTANIK_FEATURE_SUN_TRACKER     1
#endif

#endif // SITES_GH_MLB41_CONFIG_H
