// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/** @file config.h
 *  @brief gh_mlb41 overrides for libbotanik_config.h. */

#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "gh_mlb41"
#endif

// NOTE: precedence is command line > site > library default

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

#if !defined(BOTANIK_FEATURE_NETWORK) || BOTANIK_FEATURE_NETWORK
#  ifndef BOTANIK_FEATURE_MODBUS_TCP_SERVER
#    define BOTANIK_FEATURE_MODBUS_TCP_SERVER 1
#  endif
// The master is the simulator's counterpart to the slave: on hardware it is
// simply unused, and on the sim it is what proves the slave serves.
#  ifndef BOTANIK_FEATURE_MODBUS_TCP_CLIENT
#    define BOTANIK_FEATURE_MODBUS_TCP_CLIENT 1
#  endif
#endif
// The field devices the simulator answers as. Compiled on hardware too, where
// nothing instantiates one - the flag is what puts rtu/slave.cpp in the build.
#ifndef BOTANIK_FEATURE_MODBUS_RTU_SERVER
#  define BOTANIK_FEATURE_MODBUS_RTU_SERVER 1
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

// --- Field bus: the RS-485 Modbus RTU master ------------------------------
// This greenhouse IS a Modbus plant - 8 devices, 71 mapped signals - so the
// reference site is where the client belongs. Guarded on PLC_SCAN, which the
// dependency rule requires and which is what a headless cut-down build would
// have turned off.
//
// Only the protocol layer exists so far (modbus/: the codec and the framer).
// Nothing instantiates it and no task steps it, so turning this on today adds
// no work to the beat - what it adds is the two .cpp files and the two test
// files to the build. Off, they compile to nothing while still looking like
// coverage, which is the same argument the sun tracker's note above makes.
#if !defined(BOTANIK_FEATURE_PLC_SCAN) || BOTANIK_FEATURE_PLC_SCAN
#  ifndef BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#    define BOTANIK_FEATURE_MODBUS_RTU_CLIENT 1
#  endif
#endif

#endif // SITES_GH_MLB41_CONFIG_H
