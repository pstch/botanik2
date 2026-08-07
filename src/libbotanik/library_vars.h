#ifndef LIBBOTANIK_LIBRARY_VARS_H
#define LIBBOTANIK_LIBRARY_VARS_H

/**
 * @file library.h
 * @brief The variable rows libbotanik itself declares, for a site's table.
 *
 * A site's `g_vars[]` is not only the site's. The library has state of its own
 * that belongs in the variable model rather than beside it - alarms it raises,
 * readings it publishes, parameters it reads - and the bank holds ONE contiguous
 * descriptor array, so those rows have to live in the site's table. This header
 * is the single splice point:
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
 * comma, so a group with nothing in it expands to nothing at all and the ones
 * around it still read correctly. Three of the four groups are empty today; the
 * plumbing is here so that filling one is an edit to this file and nothing else.
 *
 * @section lib_order Why it goes first, and why the parameters are last
 *
 * Two ordering rules, and both come from the same place - `Bank` requires the
 * `ROLE_PARAM` rows to be **one contiguous run**, because that is what lets the
 * PARAMS page and the shell address them by a dense 0-based index.
 *
 * So the library's parameters have to sit immediately beside the site's. Sites
 * put their parameters at the top of their table (it reads well and it is the
 * convention), which pins both rules: the library block goes **first**, and its
 * parameters go **last within it**, so the two parameter blocks abut.
 *
 * Nothing else in the model cares about position. Alarm conditions are numbered
 * by table order, so the library's alarms head the ALARMS page - which is fine,
 * they are the ones every unit has.
 *
 * @section lib_regs Register space
 *
 * A parameter, status value or alarm carries a hand-assigned Modbus TCP slave
 * register (`Binding::reg`), and an external master's map is a contract. The
 * library reserves **900-999** for its own rows so a site never has to guess
 * which numbers are safe: its parameters and status values grow upward from 900,
 * its alarms take the top (990-999), and a site's own alarms live in 800-899.
 * `tests/test_site_names.cpp` fails on a collision and on a row outside the
 * block its scope belongs to.
 *
 * @section lib_missing Leaving it out
 *
 * A site that omits the splice still builds and boots. What it loses is
 * everything the library expected to find: today, the health monitor's six alarm
 * rows, so no platform-health fault can be raised at all. POST reports each
 * missing row by name, the bank counts and logs every unresolved lookup, and
 * `tests/test_site_names.cpp` fails before either of those is reached.
 */

#include "libbotanik/alarms/platform.h" // BOTANIK_PLATFORM_ALARM_VARS
#include "libbotanik/config/net.h"      // the net_/ntp_/mqtt_ row defaults
#include "libbotanik/config/serial.h"   // BOTANIK_CONSOLE_BAUD / MONITOR_BAUD
#include "libbotanik/config/sun.h"      // the geo_ row defaults
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
#include "libbotanik/vars/declare.h"    // the A/P/G/SV factories the groups are written with

/** @name The four groups
 *
 *  One per kind of row the library may declare, each a concatenation of whatever
 *  subsystem headers contribute to it. A subsystem defines its own macro next to
 *  the code that owns the rows (as `alarms/platform.h` does) and gets added to
 *  the group here - so this file stays the one place that says what the library
 *  puts in a site's table.
 *
 *  Every row must end with a comma, its own, so an empty group vanishes cleanly.
 *  @{ */

/** @brief Alarms the library raises. Its rows live beside the code that raises
 *  them, in `alarms/platform.h`; this is only where the group is assembled. */
#define BOTANIK_LIBRARY_ALARM_VARS   BOTANIK_PLATFORM_ALARM_VARS             \
                                     BOTANIK_MODBUS_RTU_ALARM_VARS

/** @brief Status values the library publishes - MQTT telemetry and the Modbus
 *  TCP slave - so a name here is an external contract.
 *
 *  The field bus's dead-device count is the first tenant (modbus/rtu/alarms.h).
 *  The scheduler's per-beat metrics are the obvious next ones: they are read off
 *  `SchedulerContext` by three separate consumers today, none of which can be
 *  reached by `get`, MQTT or a Modbus master.
 *
 *  Rows must be fully qualified and end with their own comma, as the alarm ones
 *  are - a site's table may be in a namespace that has not pulled Vars in:
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
 *  The two UART rates: what an installation runs its console at is the
 *  operator's business, and a unit whose terminal was swapped should not need a
 *  firmware build. `Config::CONSOLE_BAUD` / `MONITOR_BAUD` are still where the
 *  DEFAULT lives, so a site pins what its hardware ships with and a wiped store
 *  comes back at a known rate.
 *
 *  The band is the standard rates either UART can actually be driven at. It is
 *  a band and not an enum because a parameter's declared bounds are the whole
 *  validation a `set` gets: a number inside it that no divisor can hit lands as
 *  the nearest the driver manages, which is a wrong rate rather than a refused
 *  one - so keep it to rates that are real.
 *
 *  LAST in the aggregate below, and that is load-bearing: see @ref lib_order.
 *
 *  //                   name                 type  def   min  max   dgt unit reg description
 *  // ::Botanik::Vars::P("sys_watchdog_ms", ::Botanik::Vars::T_U32,
 *  //                    10000, 100, 60000, 0, "ms", 950, "HW watchdog", ""), */
#define BOTANIK_LIBRARY_PARAM_VARS                                             \
    ::Botanik::Vars::P("serial_console_baud", ::Botanik::Vars::T_U32,          \
        BOTANIK_CONSOLE_BAUD, 1200, 921600, 0, "Bd", 900,                      \
        "console UART rate",                                                   \
        "retunes the wire AND the render budget; reconnect at the new rate"),  \
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
    BOTANIK_MODBUS_TCP_PARAM_VARS

/** @brief Pool bytes the library's own text parameters need: net_ip, net_mask,
 *  net_gw, net_dns1, net_dns2, ntp_srv1, ntp_srv2 and mqtt_host - eight rows,
 *  each BOTANIK_HOST_STR_LEN. A site sizes its string pool as this plus whatever
 *  its own text rows take; `Bank::init()` lays the slots out, so a site says how
 *  much room there is and never where anything goes. */
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
