// REVIEW_35: DONE
#ifndef LIBBOTANIK_FEATURES_H
#define LIBBOTANIK_FEATURES_H

/** @file libbotanik_features.h
 *  @brief Compile-time feature flags for the Botanik library.
 *
 *  Each `BOTANIK_FEATURE_<X>` toggles one optional subsystem. Precedence is
 *  command line > site `config.h` > the default here: CMake puts a `-D` on the
 *  compile line before this header is read, and a site guards its own `#define`
 *  with `#ifndef`.
 *
 *  Gate code with `#if BOTANIK_FEATURE_<X>`, never `#ifdef` - every flag is
 *  defined by the time this header ends, so `#ifdef` is always true. The
 *  preprocessor rather than `constexpr`, because a flag must be able to drop a
 *  member declaration and a constructor-init entry, and C++11 has no
 *  `if constexpr`.
 *
 *  BOTANIK_FEATURE_LIST is the single source of truth for which flags EXIST: the
 *  `features` shell command expands it, and CMake discovers `-D`s by prefix
 *  rather than from a list of its own, which would silently ignore the `-D` of
 *  any flag missing from it.
 *
 *  Adding a flag: one `X()` entry, one `#ifndef` default, any dependency rule,
 *  and a row in docs/integration/config.md.
 */

// The flag roster
// ===========================================================================

/** @brief Every flag, for anything that needs to enumerate them. */
#define BOTANIK_FEATURE_LIST(X) \
    X(CONSOLE)            \
    X(LOGGING)            \
    X(NETWORK)            \
    X(NTP)                \
    X(MQTT)               \
    X(SUN_TRACKER)        \
    X(HOME_PAGE)          \
    X(SHELL_PAGE)         \
    X(LOGS_PAGE)          \
    X(ALARMS_PAGE)        \
    X(PARAMS_PAGE)        \
    X(MONITOR_PAGE)       \
    X(TASKS_PAGE)         \
    X(NOTES_PAGE)         \
    X(LOGS_PUBLISH)       \
    X(ALARMS_PUBLISH)     \
    X(LOGS_MONITOR)       \
    X(PLC_SCAN)           \
    X(PLC_BLK_BASE)       \
    X(PLC_BLK_FILTERS)    \
    X(MODBUS_TCP_SERVER)  \
    X(MODBUS_TCP_CLIENT)  \
    X(MODBUS_RTU_CLIENT)  \
    X(MODBUS_RTU_SERVER)

// Defaults
// ===========================================================================
// Minimal core, opt in: a build that sets nothing gets a console with a home
// screen and a shell, logging, and the scan cycle. Everything else - the other
// pages, the network, the field bus, the block library - is something a site
// asks for. src/sites/gh_mlb41/config.h is the worked full profile.

/** @brief Interactive ANSI console: the terminal, the console UART, and the
 *  register_pages() / register_commands() hooks. Off -> headless. */
#ifndef BOTANIK_FEATURE_CONSOLE
#  define BOTANIK_FEATURE_CONSOLE 1
#endif

/** @brief Log writers, the rate limiter and the queue.
 *  @note Off leaves `Logs::Logger` in place as an inert shell, so the headers
 *  that take a `Logger*` need no change and the emit sites - all already
 *  null-guarded - simply reach nothing. */
#ifndef BOTANIK_FEATURE_LOGGING
#  define BOTANIK_FEATURE_LOGGING 1
#endif

/** @brief Network stack (NetworkHandler / NameResolver). */
#ifndef BOTANIK_FEATURE_NETWORK
#  define BOTANIK_FEATURE_NETWORK 0
#endif

/** @brief NTP time sync. */
#ifndef BOTANIK_FEATURE_NTP
#  define BOTANIK_FEATURE_NTP 0
#endif

/** @brief MQTT client. */
#ifndef BOTANIK_FEATURE_MQTT
#  define BOTANIK_FEATURE_MQTT 0
#endif

/** @brief Solar-position tracker. Standalone: BotanikFirmware does not
 *  instantiate it, so a site that wants it turns it on and owns the instance. */
#ifndef BOTANIK_FEATURE_SUN_TRACKER
#  define BOTANIK_FEATURE_SUN_TRACKER 0
#endif

// --- Console pages -------------------------------------------------------
// HOME and SHELL default to CONSOLE's value rather than a literal, so a headless
// build drops them without writing a contradiction the dependency rules would
// reject. The rest default off: they are what a site chooses.

/** @brief HOME: logo, identity, uptime, log tail, site home fields. */
#ifndef BOTANIK_FEATURE_HOME_PAGE
#  define BOTANIK_FEATURE_HOME_PAGE BOTANIK_FEATURE_CONSOLE
#endif

/** @brief SHELL - and with it the whole command interpreter. `ShellPage` owns
 *  `Shell::CommandShell`, so this also gates `register_commands()`,
 *  `set_diagnostics()` and `set_ntp()`. Off means no shell at all, not a shell
 *  without a page. */
#ifndef BOTANIK_FEATURE_SHELL_PAGE
#  define BOTANIK_FEATURE_SHELL_PAGE BOTANIK_FEATURE_CONSOLE
#endif

/** @brief LOGS: the scrollback page. */
#ifndef BOTANIK_FEATURE_LOGS_PAGE
#  define BOTANIK_FEATURE_LOGS_PAGE 0
#endif

/** @brief ALARMS: the condition table. The alarm CONTROLLER is unconditional -
 *  the bottom alarm bar reads it whether or not this page exists. */
#ifndef BOTANIK_FEATURE_ALARMS_PAGE
#  define BOTANIK_FEATURE_ALARMS_PAGE 0
#endif

/** @brief PARAMS: the editable parameter table. */
#ifndef BOTANIK_FEATURE_PARAMS_PAGE
#  define BOTANIK_FEATURE_PARAMS_PAGE 0
#endif

/** @brief MONITOR: every published variable. Not to be confused with
 *  LOGS_MONITOR, which is a serial log sink and a different subsystem. */
#ifndef BOTANIK_FEATURE_MONITOR_PAGE
#  define BOTANIK_FEATURE_MONITOR_PAGE 0
#endif

/** @brief TASKS: the scheduler's task table and its per-task cost. */
#ifndef BOTANIK_FEATURE_TASKS_PAGE
#  define BOTANIK_FEATURE_TASKS_PAGE 0
#endif

/** @brief NOTES: a full-screen editor over the operator's note, persisted to
 *  flash. Costs NOTES_LEN + 2 * NOTES_MAX_ROWS row slots of RAM. */
#ifndef BOTANIK_FEATURE_NOTES_PAGE
#  define BOTANIK_FEATURE_NOTES_PAGE 0
#endif

// --- Log and alarm transports --------------------------------------------

/** @brief Publish log entries to the MQTT broker (`MqttLogWriter`). */
#ifndef BOTANIK_FEATURE_LOGS_PUBLISH
#  define BOTANIK_FEATURE_LOGS_PUBLISH 0
#endif

/** @brief Publish alarm transitions to the MQTT broker
 *  (`Alarms::MqttAlarmNotifier`). */
#ifndef BOTANIK_FEATURE_ALARMS_PUBLISH
#  define BOTANIK_FEATURE_ALARMS_PUBLISH 0
#endif

/** @brief Raw log output on the secondary MONITOR serial port. */
#ifndef BOTANIK_FEATURE_LOGS_MONITOR
#  define BOTANIK_FEATURE_LOGS_MONITOR 0
#endif

// --- PLC -----------------------------------------------------------------

/** @brief The scan cycle: the site's control programs, registered and run at
 *  their declared rates. Off does NOT remove control authority - the Bank's
 *  enable/disable latch has consumers of its own (the `control` command, the
 *  health withdrawal) that outlive the scan. */
#ifndef BOTANIK_FEATURE_PLC_SCAN
#  define BOTANIK_FEATURE_PLC_SCAN 1
#endif

/** @brief The base function blocks: timers, logic, analog, meter, pid,
 *  actuator. Header-only `plc/functions/` is never gated - it costs nothing
 *  unless included. */
#ifndef BOTANIK_FEATURE_PLC_BLK_BASE
#  define BOTANIK_FEATURE_PLC_BLK_BASE 0
#endif

/** @brief The filter blocks: Ewma, MedianOf3, MovingAverage. Distinct from
 *  `utils/filters.h`, the scheduler's metric filter, which is unconditional. */
#ifndef BOTANIK_FEATURE_PLC_BLK_FILTERS
#  define BOTANIK_FEATURE_PLC_BLK_FILTERS 0
#endif

// --- Field bus -----------------------------------------------------------

/** @brief Modbus TCP slave, exposing parameters and status values. */
#ifndef BOTANIK_FEATURE_MODBUS_TCP_SERVER
#  define BOTANIK_FEATURE_MODBUS_TCP_SERVER 0
#endif

/** @brief Modbus TCP master: this unit polling ANOTHER device over Ethernet.
 *  The simulator's counterpart to the slave, and equally a way for a site to
 *  read a second PLC.
 *  @note Requires BOTANIK_FEATURE_NETWORK */
#ifndef BOTANIK_FEATURE_MODBUS_TCP_CLIENT
#  define BOTANIK_FEATURE_MODBUS_TCP_CLIENT 0
#endif

/** @brief Modbus RTU master, filling the process image from the field.
 *  @note Requires BOTANIK_FEATURE_PLC_SCAN */
#ifndef BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#  define BOTANIK_FEATURE_MODBUS_RTU_CLIENT 0
#endif

/** @brief Modbus RTU slave: this unit ANSWERING on a field bus, as a device
 *  does. The simulator's field devices are these; on hardware it is how a unit
 *  appears to somebody else's master. */
#ifndef BOTANIK_FEATURE_MODBUS_RTU_SERVER
#  define BOTANIK_FEATURE_MODBUS_RTU_SERVER 0
#endif

// Dependency rules
// ===========================================================================
// A contradiction is a build error, not a silent no-op: the alternative is a
// binary quietly missing what was requested.

#if BOTANIK_FEATURE_NTP && !BOTANIK_FEATURE_NETWORK
#  error "BOTANIK_FEATURE_NTP requires BOTANIK_FEATURE_NETWORK"
#endif
#if BOTANIK_FEATURE_MQTT && !BOTANIK_FEATURE_NETWORK
#  error "BOTANIK_FEATURE_MQTT requires BOTANIK_FEATURE_NETWORK"
#endif

#if BOTANIK_FEATURE_HOME_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_HOME_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_SHELL_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_SHELL_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_LOGS_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_LOGS_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_LOGS_PAGE && !BOTANIK_FEATURE_LOGGING
#  error "BOTANIK_FEATURE_LOGS_PAGE requires BOTANIK_FEATURE_LOGGING"
#endif
#if BOTANIK_FEATURE_ALARMS_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_ALARMS_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_PARAMS_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_PARAMS_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_MONITOR_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_MONITOR_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_TASKS_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_TASKS_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif
#if BOTANIK_FEATURE_NOTES_PAGE && !BOTANIK_FEATURE_CONSOLE
#  error "BOTANIK_FEATURE_NOTES_PAGE requires BOTANIK_FEATURE_CONSOLE"
#endif

#if BOTANIK_FEATURE_LOGS_PUBLISH && !BOTANIK_FEATURE_LOGGING
#  error "BOTANIK_FEATURE_LOGS_PUBLISH requires BOTANIK_FEATURE_LOGGING"
#endif
#if BOTANIK_FEATURE_LOGS_PUBLISH && !BOTANIK_FEATURE_MQTT
#  error "BOTANIK_FEATURE_LOGS_PUBLISH requires BOTANIK_FEATURE_MQTT"
#endif
#if BOTANIK_FEATURE_ALARMS_PUBLISH && !BOTANIK_FEATURE_LOGGING
#  error "BOTANIK_FEATURE_ALARMS_PUBLISH requires BOTANIK_FEATURE_LOGGING"
#endif
#if BOTANIK_FEATURE_ALARMS_PUBLISH && !BOTANIK_FEATURE_MQTT
#  error "BOTANIK_FEATURE_ALARMS_PUBLISH requires BOTANIK_FEATURE_MQTT"
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR && !BOTANIK_FEATURE_LOGGING
#  error "BOTANIK_FEATURE_LOGS_MONITOR requires BOTANIK_FEATURE_LOGGING"
#endif

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER && !BOTANIK_FEATURE_NETWORK
#  error "BOTANIK_FEATURE_MODBUS_TCP_SERVER requires BOTANIK_FEATURE_NETWORK"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER && !BOTANIK_FEATURE_PLC_SCAN
#  error "BOTANIK_FEATURE_MODBUS_TCP_SERVER requires BOTANIK_FEATURE_PLC_SCAN"
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT && !BOTANIK_FEATURE_NETWORK
#  error "BOTANIK_FEATURE_MODBUS_TCP_CLIENT requires BOTANIK_FEATURE_NETWORK"
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT && !BOTANIK_FEATURE_PLC_SCAN
#  error "BOTANIK_FEATURE_MODBUS_RTU_CLIENT requires BOTANIK_FEATURE_PLC_SCAN"
#endif
// MODBUS_RTU_SERVER depends on nothing, deliberately: it is a device model over
// a serial port and a register image, with no variable table and no scan behind
// it. Stated because an absent rule reads like a forgotten one.

#endif // LIBBOTANIK_FEATURES_H
