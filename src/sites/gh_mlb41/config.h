// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_CONFIG_H
#define SITES_GH_MLB41_CONFIG_H

/** @file config.h
 *  @brief gh_mlb41 overrides for libbotanik_config.h. */

#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "gh_mlb41"
#endif

// NOTE: precedence is command line > site > library default

// Factory values for the library's own parameter rows (library_vars.h): what a
// wiped store comes back as, retuned from the PARAMS page.

/** @brief How many rows the bank must hold: 269 of this site's plus the
 *  library's own (56 on this profile). The bank's value store and name index
 *  are sized off this, so trimming it to what is declared is what stops a
 *  512-row default costing 6 KB of RAM nobody uses. Headroom for a few rows;
 *  past it, registration is refused and POST says so. */
#ifndef BOTANIK_VAR_MAX
#  define BOTANIK_VAR_MAX 544
#endif

/** @brief The TCP map holds every PARAMETER, STATUS and ALARM row, which is
 *  well past the 256 default on this profile. Two bytes each, and a site that
 *  outgrows it serves a SHORT map: the rows past the end answer nothing, which
 *  an integrator reads as a device that does not have them.
 *  @note No number quoted here - it rots a row at a time, and it did. The gate
 *  fails on the real one ("site: every mapped row fits the TCP map"). */
#ifndef BOTANIK_MODBUS_TCP_MAX_ROWS
#  define BOTANIK_MODBUS_TCP_MAX_ROWS 448
#endif

/** @brief String pool: this site declares no text rows of its own, so it is
 *  exactly what the library's eight need (Vars::LIBRARY_STR_BYTES). */
#ifndef BOTANIK_STR_POOL_BYTES
#  define BOTANIK_STR_POOL_BYTES 512
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

#if !defined(BOTANIK_FEATURE_NETWORK) || BOTANIK_FEATURE_NETWORK
#  ifndef BOTANIK_FEATURE_MODBUS_TCP_SERVER
#    define BOTANIK_FEATURE_MODBUS_TCP_SERVER 1
#  endif
// The master is the simulator's counterpart to the slave: unused on hardware,
// and on the sim it is what proves the slave serves.
#  ifndef BOTANIK_FEATURE_MODBUS_TCP_CLIENT
#    define BOTANIK_FEATURE_MODBUS_TCP_CLIENT 1
#  endif
#endif
// --- The field bus, as this installation actually runs it -----------------
// The site's device map says 38400 baud, none/8/1, on COM2 (vars.h's header).
// The library defaults to 19200, so without this the master talks at half rate
// and nothing on the wire answers.
#ifndef BOTANIK_MODBUS_BAUD
#  define BOTANIK_MODBUS_BAUD 38400
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
// This greenhouse IS a Modbus plant (vars.h has the device map, site.h what
// each model answers). Guarded on PLC, which the dependency rule requires.
#if !defined(BOTANIK_FEATURE_PLC) || BOTANIK_FEATURE_PLC
#  ifndef BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#    define BOTANIK_FEATURE_MODBUS_RTU_CLIENT 1
#  endif
#endif

// The segment's baud rate again; see above.
#ifndef BOTANIK_MODBUS_BAUD
#  define BOTANIK_MODBUS_BAUD 38400
#endif

// The reference site carries the footprint contract: libbotanik_sizes.h pins
// what each long-lived object costs, and every one of those numbers is a
// function of the settings above. A site with different settings has different
// sizes, so this is opt-in rather than a library default.
#ifndef BOTANIK_PIN_SIZES
#  define BOTANIK_PIN_SIZES 1
#endif

#endif // SITES_GH_MLB41_CONFIG_H
