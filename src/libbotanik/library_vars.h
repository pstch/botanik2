#ifndef LIBBOTANIK_LIBRARY_VARS_H
#define LIBBOTANIK_LIBRARY_VARS_H

/**
 * @file library_vars.h
 * @brief The variable rows libbotanik itself declares, for a site's table.
 *
 * A site's `g_vars[]` is not only the site's: the library declares rows of its
 * own (alarms it raises, readings it publishes, parameters it reads), and the
 * bank holds ONE contiguous descriptor array, so they must live in the site's
 * table. This header is the single splice point:
 *
 * @code
 * const Variable g_vars[] = {
 *     BOTANIK_LIBRARY_VARS          // the library's rows, first
 *
 *     ...the site's parameters...   // must come first among the site's own
 *     ...everything else the site declares...
 * };
 * @endcode
 *
 * One `#define`, no comma after it: every row inside carries its own trailing
 * comma, so an empty group expands to nothing and the rows around it still read
 * correctly.
 *
 * @section lib_order Why it goes first, and why the parameters are last
 *
 * `Bank` requires the `ROLE_PARAM` rows to be **one contiguous run**, which is
 * what lets the PARAMS page and the shell address them by a dense 0-based index.
 * Sites put their parameters at the top of their table, so the library block
 * goes **first** and its parameters **last within it**, and the two parameter
 * blocks abut.
 *
 * Nothing else in the model cares about position.
 *
 * @section lib_regs Register space
 *
 * A parameter, status value or alarm carries a hand-assigned Modbus TCP slave
 * register (`Binding::reg`), and an external master's map is a contract. The
 * library reserves **900-999** for its own rows: parameters and status values
 * grow upward from 900, its alarms take 990-999, and a site's own alarms live in
 * 800-899. `tests/test_site_names.cpp` fails on a collision and on a row outside
 * the block its scope belongs to.
 *
 * @section lib_missing Leaving it out
 *
 * A site that omits the splice still builds and boots, but loses every row the
 * library expects: today the health monitor's six alarms, so no platform-health
 * fault can be raised at all. POST names each missing row, the bank counts and
 * logs every unresolved lookup, and `tests/test_site_names.cpp` fails first.
 */

#include "libbotanik/alarms/platform.h" // BOTANIK_PLATFORM_ALARM_VARS
#include "libbotanik/config/alarms.h"   // the sys_ health-threshold defaults
#include "libbotanik/config/logging.h"  // BOTANIK_LOG_MIN_LEVEL - sys_log_level's default
#include "libbotanik/config/net.h"      // the net_/ntp_/mqtt_ row defaults
#include "libbotanik/config/serial.h"   // BOTANIK_CONSOLE_BAUD / MONITOR_BAUD
#include "libbotanik/config/sun.h"      // the geo_ row defaults
#include "libbotanik/config/vars.h"     // BOTANIK_RETAIN_SAVE_MS - sys_retain_sec's default
#include "libbotanik/logs/topics.h"     // Logs::log_level_label - sys_log_level's converter
#if BOTANIK_FEATURE_CONSOLE
#include "libbotanik/console/params.h"  // BOTANIK_CONSOLE_PARAM_VARS - the term_ rows
#else
#  define BOTANIK_CONSOLE_PARAM_VARS
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
#include "libbotanik/modbus/tcp/params.h" // BOTANIK_MODBUS_TCP_PARAM_VARS
#else
#  define BOTANIK_MODBUS_TCP_PARAM_VARS
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#include "libbotanik/modbus/rtu/alarms.h" // BOTANIK_MODBUS_RTU_ALARM_VARS / _STATUS_VARS
#else
// The rows exist only where the field bus does, but the GROUPS below are
// spliced unconditionally - so an absent subsystem contributes nothing rather
// than failing to expand.
#  define BOTANIK_MODBUS_RTU_ALARM_VARS
#  define BOTANIK_MODBUS_RTU_STATUS_VARS
#endif
#if BOTANIK_FEATURE_PLC_SCAN
#include "libbotanik/plc/alarms.h"       // BOTANIK_PLC_SCAN_ALARM_VARS
#else
#  define BOTANIK_PLC_SCAN_ALARM_VARS
#endif
#include "libbotanik/vars/declare.h"    // the A/P/G/SV factories the groups are written with

/** @name The four groups
 *
 *  One per kind of row the library may declare, each a concatenation of whatever
 *  subsystem headers contribute. A subsystem defines its own macro beside the
 *  code that owns the rows (as `alarms/platform.h` does) and is added to the
 *  group here, so this file stays the one place that says what the library puts
 *  in a site's table.
 *
 *  Every row must end with its own comma, so an empty group vanishes cleanly.
 *  @{ */

/** @brief Alarms the library raises. Its rows live beside the code that raises
 *  them, in `alarms/platform.h`; this is only where the group is assembled. */
#define BOTANIK_LIBRARY_ALARM_VARS   BOTANIK_PLATFORM_ALARM_VARS             \
                                     BOTANIK_MODBUS_RTU_ALARM_VARS           \
                                     BOTANIK_PLC_SCAN_ALARM_VARS

/** @brief Status values the library publishes (MQTT telemetry and the Modbus
 *  TCP slave), so a name here is an external contract.
 *
 *  Rows must be fully qualified and end with their own comma: a site's table may
 *  be in a namespace that has not pulled Vars in.
 *
 *  //                    name            type   def  dgt unit reg  description      notes
 *  // ::Botanik::Vars::SV("sys_cpu_load", ::Botanik::Vars::T_F32, 0.0, 1, "%", 900,
 *  //                     "loop CPU load", ""), */
#define BOTANIK_LIBRARY_STATUS_VARS  BOTANIK_MODBUS_RTU_STATUS_VARS

/** @brief Computed values the library exposes to the HMI only. Not published,
 *  so unlike a status value one may be renamed freely.
 *
 *  Empty.
 *
 *  //                   name            type   def         dgt unit description  notes
 *  // ::Botanik::Vars::G("sys_beat_ms", ::Botanik::Vars::T_U16,
 *  //                    ::Botanik::Vars::SU(0), 0, "ms", "beat period", ""), */
#define BOTANIK_LIBRARY_GLOBAL_VARS

/** @brief Parameters the library reads.
 *
 *  What the library reads by name, the library declares. The two UART rates and
 *  the `term_` geometry are operator-editable, so a swapped terminal - a
 *  different rate, a different size, a different make - needs no firmware build.
 *  The `Config::` values behind them hold the DEFAULT, so a wiped store comes
 *  back at what the hardware ships with.
 *
 *  @warning A parameter's declared bounds are the whole validation a `set` gets,
 *  so a band must stay to what the hardware can really do: a UART rate no
 *  divisor can hit lands as the nearest the driver manages, and a geometry the
 *  terminal does not have is simply a garbled screen that survives a reboot.
 *
 *  LAST in the aggregate below, and that is load-bearing: see @ref lib_order.
 *
 *  //                   name                 type  def   min  max   dgt unit reg description
 *  // ::Botanik::Vars::P("sys_watchdog_ms", ::Botanik::Vars::T_U32,
 *  //                    10000, 100, 60000, 0, "ms", 975, "HW watchdog", ""), */
#define BOTANIK_LIBRARY_PARAM_VARS                                             \
    /* F_STAGED, and the one row where it really earns its keep: a rate the   \
       UART cannot reach leaves nothing to type into. See console/params.h. */ \
    ::Botanik::Vars::P("serial_console_baud", ::Botanik::Vars::T_U32,          \
        BOTANIK_CONSOLE_BAUD, 1200, 921600, 0, "Bd", 900,                      \
        "console UART rate",                                                   \
        "reconnect at the new rate, then `console confirm`",                   \
        ::Botanik::Vars::F_STAGED),                                            \
    ::Botanik::Vars::P("serial_monitor_baud", ::Botanik::Vars::T_U32,          \
        BOTANIK_MONITOR_BAUD, 1200, 921600, 0, "Bd", 901,                      \
        "monitor UART rate",                                                   \
        "log-only port; ignored on the OPTA, whose MONITOR is USB CDC"),       \
    /* net_: NetworkHandler reads these by name, so the library declares them */\
    ::Botanik::Vars::P("net_serial", ::Botanik::Vars::T_U32,                   \
        1, 0, 9999, 0, "", 910,                                                \
        "bump to apply staged net_ edits", ""),                                \
    ::Botanik::Vars::P("net_dhcp", ::Botanik::Vars::T_BOOL,                    \
        BOTANIK_NET_DHCP, 0, 1, 0, "", 911,                                    \
        "enable DHCP IP configuration", "ignores the static ip/mask/gw rows"), \
    ::Botanik::Vars::PS("net_ip", BOTANIK_NET_IP,                              \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_IPV4, 912,                  \
        "static IP address", ""),                                              \
    ::Botanik::Vars::PS("net_mask", BOTANIK_NET_MASK,                          \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_IPV4, 913,                  \
        "static IP netmask", ""),                                              \
    ::Botanik::Vars::PS("net_gw", BOTANIK_NET_GW,                              \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_IPV4, 914,                  \
        "static IP gateway", ""),                                              \
    ::Botanik::Vars::PS("net_dns1", BOTANIK_NET_DNS1,                          \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_IPV4, 915,                  \
        "static DNS resolver 1", ""),                                          \
    ::Botanik::Vars::PS("net_dns2", BOTANIK_NET_DNS2,                          \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_IPV4_OPT, 916,              \
        "static DNS resolver 2", "empty = use dns1 alone"),                    \
    /* ntp_: NtpClient reads these by name */                                  \
    ::Botanik::Vars::P("ntp_serial", ::Botanik::Vars::T_U32,                   \
        1, 0, 9999, 0, "", 920,                                                \
        "bump to apply staged ntp_ edits", ""),                                \
    ::Botanik::Vars::P("ntp_enable", ::Botanik::Vars::T_BOOL,                  \
        BOTANIK_NTP_ENABLE, 0, 1, 0, "", 921,                                  \
        "enable NTP clock sync", ""),                                          \
    ::Botanik::Vars::P("ntp_poll_sec", ::Botanik::Vars::T_U32,                 \
        BOTANIK_NTP_POLL_SEC, 10, 86400, 0, "sec", 922,                        \
        "NTP poll interval, in seconds", ""),                                  \
    ::Botanik::Vars::P("ntp_port", ::Botanik::Vars::T_U16,                     \
        BOTANIK_NTP_PORT, 1, 65535, 0, "", 923,                                \
        "NTP server port", "standard is 123"),                                 \
    ::Botanik::Vars::PS("ntp_srv1", BOTANIK_NTP_SRV1,                          \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_HOST, 924,                  \
        "NTP server 1", ""),                                                   \
    ::Botanik::Vars::PS("ntp_srv2", BOTANIK_NTP_SRV2,                          \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_HOST_OPT, 925,              \
        "NTP server 2", "empty = no fallback"),                                \
    /* mqtt_: MqttClient reads these by name */                                \
    ::Botanik::Vars::P("mqtt_serial", ::Botanik::Vars::T_U32,                  \
        1, 0, 9999, 0, "", 930,                                                \
        "bump to apply staged mqtt_ edits", ""),                               \
    ::Botanik::Vars::P("mqtt_enable", ::Botanik::Vars::T_BOOL,                 \
        BOTANIK_MQTT_ENABLE, 0, 1, 0, "", 931,                                 \
        "enable MQTT", ""),                                                    \
    ::Botanik::Vars::PS("mqtt_host", BOTANIK_MQTT_HOST,                        \
        BOTANIK_HOST_STR_LEN, ::Botanik::Vars::STR_HOST, 932,                  \
        "MQTT broker host", ""),                                               \
    ::Botanik::Vars::P("mqtt_port", ::Botanik::Vars::T_U16,                    \
        BOTANIK_MQTT_PORT, 1, 65535, 0, "", 933,                               \
        "MQTT broker port", "standard is 1883"),                               \
    ::Botanik::Vars::P("mqtt_ka_sec", ::Botanik::Vars::T_U16,                  \
        BOTANIK_MQTT_KA_SEC, 10, 1800, 0, "sec", 934,                          \
        "MQTT keep-alive interval", "also the dead-broker detection"),         \
    ::Botanik::Vars::P("mqtt_tlm_sec", ::Botanik::Vars::T_U32,                 \
        (BOTANIK_MQTT_TELEMETRY_PERIOD_MS / 1000), 0, 86400, 0, "sec", 935,    \
        "telemetry publish cadence", "0 stops publishing telemetry"),          \
    ::Botanik::Vars::P("mqtt_retain", ::Botanik::Vars::T_BOOL,                 \
        BOTANIK_MQTT_TELEMETRY_RETAIN, 0, 1, 0, "", 936,                       \
        "publish telemetry retained",                                          \
        "on, the broker keeps serving a dead sensor's last good value"),       \
    /* geo_: SunPositionTracker reads these by name */                         \
    ::Botanik::Vars::P("geo_serial", ::Botanik::Vars::T_U32,                   \
        1, 0, 9999, 0, "", 940,                                                \
        "bump to apply staged geo_ edits", ""),                                \
    ::Botanik::Vars::P("geo_lat_deg", ::Botanik::Vars::T_F32,                  \
        BOTANIK_GEO_LAT_DEG, -90.0, 90.0, 2, "deg", 941,                       \
        "geographical latitude, in degrees", "per site location"),             \
    ::Botanik::Vars::P("geo_lon_deg", ::Botanik::Vars::T_F32,                  \
        BOTANIK_GEO_LON_DEG, -180.0, 180.0, 2, "deg", 942,                     \
        "geographical longitude, in degrees", "per site location"),  \
    BOTANIK_MODBUS_TCP_PARAM_VARS                                              \
    BOTANIK_CONSOLE_PARAM_VARS                                                 \
    /* sys_: the platform's own runtime policy. NO commit serial, unlike every  \
       group above it: each row is read where it is used, either live on the    \
       beat that needs it or on the edge of a change, and none of them has a    \
       peer to reconnect or a half-applied state to avoid. */                   \
    ::Botanik::Vars::P("sys_cpu_load_pct", ::Botanik::Vars::T_U8,              \
        BOTANIK_ALARM_CPU_LOAD_PCT, 1, 100, 0, "%", 970,                       \
        "CPU_LOAD alarm threshold", "percent of the task period"),             \
    ::Botanik::Vars::P("sys_jitter_pct", ::Botanik::Vars::T_U8,                \
        BOTANIK_ALARM_CPU_JITTER_PCT, 1, 100, 0, "%", 971,                     \
        "CPU_JITT alarm threshold", "percent of the task period"),             \
    ::Botanik::Vars::P("sys_stack_pct", ::Botanik::Vars::T_U8,                 \
        BOTANIK_ALARM_STACK_PCT, 1, 100, 0, "%", 972,                          \
        "STACK alarm threshold", "percent of the stack in use"),               \
    ::Botanik::Vars::PE("sys_log_level", BOTANIK_LOG_MIN_LEVEL,                \
        &::Botanik::Logs::log_level_label, 973,                                \
        "lowest level the logger forwards",                                    \
        "the floor for EVERY sink; each page filters further"),                \
    ::Botanik::Vars::P("sys_retain_sec", ::Botanik::Vars::T_U32,               \
        (BOTANIK_RETAIN_SAVE_MS / 1000), 0, 86400, 0, "sec", 974,              \
        "how often retained values reach flash",                               \
        "0 leaves only `save` and the way down; wear vs loss"),

/** @brief Pool bytes the library's own text parameters need: eight rows (net_ip,
 *  net_mask, net_gw, net_dns1, net_dns2, ntp_srv1, ntp_srv2, mqtt_host), each
 *  BOTANIK_HOST_STR_LEN. A site sizes its string pool as this plus its own text
 *  rows; `Bank::init()` lays the slots out, so a site says how much room there
 *  is and never where anything goes. */
#define BOTANIK_LIBRARY_STR_BYTES   (8u * BOTANIK_HOST_STR_LEN)

/** @} */

/** @brief Every row the library declares. Splice this into the front of a
 *  site's `g_vars[]`, without a trailing comma. */
#define BOTANIK_LIBRARY_VARS        \
    BOTANIK_LIBRARY_ALARM_VARS      \
    BOTANIK_LIBRARY_STATUS_VARS     \
    BOTANIK_LIBRARY_GLOBAL_VARS     \
    BOTANIK_LIBRARY_PARAM_VARS

#endif // LIBBOTANIK_LIBRARY_VARS_H
