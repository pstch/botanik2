#ifndef SITES_BARE_CONFIG_H
#define SITES_BARE_CONFIG_H

/** @file config.h
 *  @brief The bare site: every optional feature off.
 *
 *  This site exists to be COMPILED, not run. `gh_mlb41` is the full profile and
 *  keeps the test suites exercising the whole surface; this is the other end,
 *  and it is what stops the OFF path of a feature flag from rotting. Two of them
 *  had already rotted when it was added: MODBUS_RTU_CLIENT and MODBUS_TCP_SERVER
 *  both closed their guard before the end of the file, so neither had ever
 *  built with the flag off.
 *
 *  Guarded `#if !defined(X) || X` throughout, so the command line still wins:
 *  turning one back ON for a build must not be a dependency error the builder
 *  never wrote.
 */

#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "bare"
#endif

// The core the header turns on by default, off: what is left is a scheduler, a
// task table, a logger with no sink, and the HAL.
#if !defined(BOTANIK_FEATURE_CONSOLE) || !BOTANIK_FEATURE_CONSOLE
#  define BOTANIK_FEATURE_CONSOLE 0
#endif
#if !defined(BOTANIK_FEATURE_LOGGING) || !BOTANIK_FEATURE_LOGGING
#  define BOTANIK_FEATURE_LOGGING 0
#endif
#if !defined(BOTANIK_FEATURE_HOME_PAGE) || !BOTANIK_FEATURE_HOME_PAGE
#  define BOTANIK_FEATURE_HOME_PAGE 0
#endif
#if !defined(BOTANIK_FEATURE_SHELL_PAGE) || !BOTANIK_FEATURE_SHELL_PAGE
#  define BOTANIK_FEATURE_SHELL_PAGE 0
#endif
#if !defined(BOTANIK_FEATURE_VARS) || !BOTANIK_FEATURE_VARS
#  define BOTANIK_FEATURE_VARS 0
#endif
#if !defined(BOTANIK_FEATURE_PLC) || !BOTANIK_FEATURE_PLC
#  define BOTANIK_FEATURE_PLC 0
#endif

// A table this small needs no headroom; the bank is compiled out anyway, but
// the constants are read by config asserts that are not.
#ifndef BOTANIK_VAR_MAX
#  define BOTANIK_VAR_MAX 8
#endif
#ifndef BOTANIK_STR_POOL_BYTES
#  define BOTANIK_STR_POOL_BYTES 16
#endif

#endif // SITES_BARE_CONFIG_H
