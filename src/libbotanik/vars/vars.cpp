/**
 * @file vars/vars.cpp
 * @brief The library's own variable rows - the one definition.
 *
 * Ordinary rows, written with the same factories a site writes its own with and
 * unqualified because this IS `Botanik::Vars`. Constant-initialised, so the
 * whole array lands in flash beside a site's.
 *
 * ORDER IS LOAD-BEARING at one point only: the parameters go last, so a site's
 * parameter segment abuts them. @see lib_order
 *
 * A row belongs here when the LIBRARY reads or raises it by name. Feature-gated
 * groups are gated where they sit, so a subsystem that is compiled out simply
 * contributes no rows - no empty macro to define, and no count to adjust.
 */
#include "libbotanik/vars/vars.h"

#include "libbotanik/vars/model.h"    // Vars::Bank::register_vars

#include "libbotanik/config/alarms.h"   // the sys_ health-threshold defaults
#include "libbotanik/config/logging.h"  // BOTANIK_LOG_MIN_LEVEL - sys_log_level's default
#include "libbotanik/config/modbus.h"   // the mb_ / rtu_ defaults
#include "libbotanik/config/serial.h"   // BOTANIK_CONSOLE_BAUD / MONITOR_BAUD
#include "libbotanik/config/sun.h"      // the geo_ row defaults
#include "libbotanik/config/terminal.h" // the term_ row defaults and bands
#include "libbotanik/config/vars.h"     // BOTANIK_RETAIN_SAVE_MS - sys_retain_sec's default
#include "libbotanik/logs/topics.h"     // Logs::log_level_label - sys_log_level's converter
#include "libbotanik/vars/declare.h"    // the A/P/PS/PE/SV factories

namespace Botanik { namespace Vars {

    namespace {

    const Variable library_vars[] = {

    /* The window is Config::HEALTH_RESET_WINDOW_MS; the text says "recently"
       rather than repeating a number that would drift from it. */
    // Alarms
    // ------------------------------------------------------------------------
    // Ordinary SCOPE_A rows - the same A() factory a site writes its own with,
    // in the same table. There is no second alarm table, no merge and no
    // reserved index range: AlarmController scans the bank for SCOPE_A and finds
    // site and platform conditions together, in registration order.
    //
    // HealthMonitor drives these the way anything drives a variable: it resolves
    // the names once at bring-up and writes the boolean. Dwell columns are
    // A()'s - on_s, off_s, recent_s, hold_on_s, min_on_s (-1 disables an
    // announce, 0 takes the default) - then the Modbus register. 990-995 is the
    // TOP of the 900-999 reserve, so a status row added later still grows upward
    // from 900 without meeting them.

    A("alarm_system_reset", "SYS_RST", -1, 0, 0, 0, 0,        990,
        "unit reset recently", ""),
    A("alarm_cpu_xrun", "CPU_XRUN", 0, -1, 0, 0, 0,           991,
        "scheduler overrun (skipped beat)", ""),
    A("alarm_cpu_load", "CPU_LOAD", 5, 5, 0, 300, 0,          992,
        "CPU load over threshold", ""),
    A("alarm_stack", "STACK", 5, 5, 0, 300, 0,                993,
        "stack usage over threshold", ""),
    A("alarm_cpu_jitter", "CPU_JITT", 5, 5, 0, 300, 0,        994,
        "CPU jitter over threshold", ""),
    /* Sticky: a store that refused a write does not heal by itself, and the
       consequence (parameters silently reverting at the next reboot) is
       invisible until it is far too late to connect cause and effect. */
    A("alarm_flash_error", "FLASH_FLT", 0, -1, 0, 0, 0,       995,
        "persistent storage write failed",
        "parameters will not survive a reboot; check the KV region"),

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    // BUS_DEAD is raised when a device has failed its retry and gone into
    // backoff, not on the first missed answer: one lost frame on an RS-485 run
    // is weather, and the backoff IS the dwell. off_s is not 0, because a device
    // that answers one probe and goes quiet again is a loose connector.
    //
    // BUS_LINE says it is the SEGMENT rather than a device, which is a different
    // fault with a different remedy: one dead sensor is a device, twenty at once
    // is the trunk, the termination, this unit's transceiver, or a second master
    // talking over it. A babbling unit makes the distinction matter - its bytes
    // corrupt whichever transaction is in flight, so the miss is charged to
    // whoever was being polled and every healthy device is faulted in turn.
    A("alarm_fieldbus", "BUS_DEAD", 0, 5, 0, 0, 0, 996,
        "a field device is not answering",
        "check the RS-485 wiring, termination and the device's own power"),
    A("alarm_fieldbus_line", "BUS_LINE", 0, 5, 0, 0, 0, 998,
        "the whole segment has gone quiet",
        "the trunk, the termination, this unit's transceiver, or a second "
        "master - not twenty devices that failed at once"),
#endif

#if BOTANIK_FEATURE_PLC_SCAN
    // Not the same as the beat being late: CPU_XRUN says the scheduler missed a
    // beat, this says a PROGRAM ran later than the period it asked for. on_s 5
    // because lateness comes in bursts (a flash write, a DNS retry); off_s 30,
    // because "it caught up once" is not evidence the cycle has room again.
    A("alarm_scan_late", "SCAN_LATE", 5, 30, 0, 0, 0, 997,
        "a control program is missing its period",
        "check the TASKS page and the per-program cost; something is "
        "overrunning or the site asks for more than the beat holds"),
#endif

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    // Status values
    // ------------------------------------------------------------------------
    // Published, so a name here is an external contract (MQTT and the Modbus TCP
    // slave). sys_fieldbus_dead is a status value rather than a second alarm:
    // the alarm says whether the bus is healthy, this says how much of it is
    // gone. The two counters beside it say WHAT KIND of quiet it is, which the
    // framer has always kept apart because they point at different physical
    // faults: CRC errors are a cable, a missing terminator, or a baud or parity
    // mismatch; clean timeouts are power or addressing; bytes out of turn are a
    // second master, a duplicate address, or a device answering past its
    // deadline.

    SV("sys_fieldbus_dead", T_U16, 0, 0, "",
        902, "field devices not answering",
        "0 when every planned device is talking"),
    SV("sys_fieldbus_crc", T_U16, 0, 0, "",
        903, "frames that failed their checksum",
        "rising means the line: termination, stub length, or the wrong baud"),
    SV("sys_fieldbus_stray", T_U16, 0, 0, "",
        904, "bytes that arrived out of turn",
        "rising means a second master, a duplicate address, or a device "
        "answering after its turn had passed"),
#endif

    // Parameters - LAST, and that is load-bearing (@see lib_order)
    // ------------------------------------------------------------------------
    // What the library reads by name, the library declares. The Config:: values
    // behind the defaults hold what a wiped store comes back at, and stay
    // site-tunable as BOTANIK_* macros.
    //
    // A parameter's declared bounds are the WHOLE validation a `set` gets, so a
    // band must stay to what the hardware can really do: a UART rate no divisor
    // can hit lands as the nearest the driver manages.

    P("serial_console_baud", T_U32,
        BOTANIK_CONSOLE_BAUD, 1200, 921600, 0, "Bd", 900,
        "console UART rate",
        "reconnect at the new rate; unsaved, so a power cycle undoes it"),
    P("serial_monitor_baud", T_U32,
        BOTANIK_MONITOR_BAUD, 1200, 921600, 0, "Bd", 901,
        "monitor UART rate",
        "log-only port; ignored on the OPTA, whose MONITOR is USB CDC"),
    /* net_: NetworkHandler reads these by name, so the library declares them */
    P("net_serial", T_U32,
        1, 0, 9999, 0, "", 910,
        "bump to apply staged net_ edits", ""),
    P("net_dhcp", T_BOOL,
        BOTANIK_NET_DHCP, 0, 1, 0, "", 911,
        "enable DHCP IP configuration", "ignores the static ip/mask/gw rows"),
    PS("net_ip", BOTANIK_NET_IP,
        BOTANIK_HOST_STR_LEN, STR_IPV4, 912,
        "static IP address", ""),
    PS("net_mask", BOTANIK_NET_MASK,
        BOTANIK_HOST_STR_LEN, STR_IPV4, 913,
        "static IP netmask", ""),
    PS("net_gw", BOTANIK_NET_GW,
        BOTANIK_HOST_STR_LEN, STR_IPV4, 914,
        "static IP gateway", ""),
    PS("net_dns1", BOTANIK_NET_DNS1,
        BOTANIK_HOST_STR_LEN, STR_IPV4, 915,
        "static DNS resolver 1", ""),
    PS("net_dns2", BOTANIK_NET_DNS2,
        BOTANIK_HOST_STR_LEN, STR_IPV4_OPT, 916,
        "static DNS resolver 2", "empty = use dns1 alone"),
    /* ntp_: NtpClient reads these by name */
    P("ntp_serial", T_U32,
        1, 0, 9999, 0, "", 920,
        "bump to apply staged ntp_ edits", ""),
    P("ntp_enable", T_BOOL,
        BOTANIK_NTP_ENABLE, 0, 1, 0, "", 921,
        "enable NTP clock sync", ""),
    P("ntp_poll_sec", T_U32,
        BOTANIK_NTP_POLL_SEC, 10, 86400, 0, "sec", 922,
        "NTP poll interval, in seconds", ""),
    P("ntp_port", T_U16,
        BOTANIK_NTP_PORT, 1, 65535, 0, "", 923,
        "NTP server port", "standard is 123"),
    PS("ntp_srv1", BOTANIK_NTP_SRV1,
        BOTANIK_HOST_STR_LEN, STR_HOST, 924,
        "NTP server 1", ""),
    PS("ntp_srv2", BOTANIK_NTP_SRV2,
        BOTANIK_HOST_STR_LEN, STR_HOST_OPT, 925,
        "NTP server 2", "empty = no fallback"),
    /* mqtt_: MqttClient reads these by name */
    P("mqtt_serial", T_U32,
        1, 0, 9999, 0, "", 930,
        "bump to apply staged mqtt_ edits", ""),
    P("mqtt_enable", T_BOOL,
        BOTANIK_MQTT_ENABLE, 0, 1, 0, "", 931,
        "enable MQTT", ""),
    PS("mqtt_host", BOTANIK_MQTT_HOST,
        BOTANIK_HOST_STR_LEN, STR_HOST, 932,
        "MQTT broker host", ""),
    P("mqtt_port", T_U16,
        BOTANIK_MQTT_PORT, 1, 65535, 0, "", 933,
        "MQTT broker port", "standard is 1883"),
    P("mqtt_ka_sec", T_U16,
        BOTANIK_MQTT_KA_SEC, 10, 1800, 0, "sec", 934,
        "MQTT keep-alive interval", "also the dead-broker detection"),
    P("mqtt_tlm_sec", T_U32,
        (BOTANIK_MQTT_TELEMETRY_PERIOD_MS / 1000.0), 0, 86400, 0, "sec", 935,
        "telemetry publish cadence", "0 stops publishing telemetry"),
    P("mqtt_retain", T_BOOL,
        BOTANIK_MQTT_TELEMETRY_RETAIN, 0, 1, 0, "", 936,
        "publish telemetry retained",
        "on, the broker keeps serving a dead sensor's last good value"),
    /* geo_: SunPositionTracker reads these by name */
    P("geo_serial", T_U32,
        1, 0, 9999, 0, "", 940,
        "bump to apply staged geo_ edits", ""),
    P("geo_lat_deg", T_F32,
        BOTANIK_GEO_LAT_DEG, -90.0, 90.0, 2, "deg", 941,
        "geographical latitude, in degrees", "per site location"),
    P("geo_lon_deg", T_F32,
        BOTANIK_GEO_LON_DEG, -180.0, 180.0, 2, "deg", 942,
        "geographical longitude, in degrees", "per site location"),


#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
    /* mb_: TcpSlave reads these by name, so the library declares them */
    // mb_: TcpSlave reads these by name, so the library declares them rather
    // than leaving every site to spell them correctly - a misnamed row gets a
    // slave silently running on its defaults, which looks exactly like a slave
    // that is working.
    P("mb_serial", T_U32,
        1, 0, 9999, 0, "", 950,
        "bump to apply staged mb_ edits", ""),
    P("mb_enable", T_BOOL,
        1, 0, 1, 0, "", 951,
        "serve the Modbus TCP slave", ""),
    P("mb_port", T_U16,
        BOTANIK_MODBUS_TCP_PORT, 1, 65535, 0, "", 952,
        "Modbus TCP slave port", "standard is 502"),
    P("mb_write_enable", T_BOOL,
        1, 0, 1, 0, "", 953,
        "let a master write parameters", "off makes the slave read-only"),
#endif

#if BOTANIK_FEATURE_CONSOLE
    /* term_: BotanikFirmware reads these by name, so the library declares them */
    // term_: which terminal is on the wire and how big it is used to be three
    // compile-time constants, so swapping a VT102 for a Minitel - or 80x24 for
    // 132x32 - was a firmware build.
    //
    // The commit serial earns its place here more than most: the rows describe
    // ONE physical terminal, and applying them one at a time walks the screen
    // through geometries that match nothing on the wire. Nothing here can be
    // verified from this end either - there is no resize handshake and no rate
    // negotiation - so what makes a wrong answer survivable is the rule the
    // whole table follows: an edit is LIVE when set and DURABLE when saved, so a
    // console nobody can read was never written and a power cycle brings the
    // last saved one back. Getting the SIZE wrong is milder than the RATE: the
    // screen mangles but input is untouched, so the rows can be typed back
    // blind.
    //
    // Both banded at SCREEN_MIN_*: the tables are laid out for 80x25 and a
    // smaller screen loses cells rather than reflowing.
    P("term_serial", T_U32,
        1, 0, 9999, 0, "", 960,
        "bump to apply staged term_ edits", ""),
    P("term_type", T_U8,
        BOTANIK_TERMINAL_TYPE, 0, 3, 0, "", 961,
        "which terminal is on the console",
        "0=VT100 1=VT102 2=Minitel 1B 3=Minitel 2"),
    P("term_cols", T_U16,
        BOTANIK_CONSOLE_COLS, BOTANIK_SCREEN_MIN_COLS,
        BOTANIK_TERMINAL_MAX_COLS, 0, "col", 962,
        "console width, in columns", "the screen buffer bounds the maximum"),
    P("term_rows", T_U16,
        BOTANIK_CONSOLE_ROWS, BOTANIK_SCREEN_MIN_ROWS,
        BOTANIK_TERMINAL_MAX_ROWS, 0, "row", 963,
        "console height, in rows", "the screen buffer bounds the maximum"),
#endif

    /* sys_: the platform's own runtime policy. NO commit serial, unlike every
       group above it: each row is read where it is used, either live on the
       beat that needs it or on the edge of a change, and none of them has a
       peer to reconnect or a half-applied state to avoid. */
    P("sys_cpu_load_pct", T_U8,
        BOTANIK_ALARM_CPU_LOAD_PCT, 1, 100, 0, "%", 970,
        "CPU_LOAD alarm threshold", "percent of the task period"),
    P("sys_jitter_pct", T_U8,
        BOTANIK_ALARM_CPU_JITTER_PCT, 1, 100, 0, "%", 971,
        "CPU_JITT alarm threshold", "percent of the task period"),
    P("sys_stack_pct", T_U8,
        BOTANIK_ALARM_STACK_PCT, 1, 100, 0, "%", 972,
        "STACK alarm threshold", "percent of the stack in use"),
    PE("sys_log_level", BOTANIK_LOG_MIN_LEVEL,
        &Logs::log_level_label, 973,
        "lowest level the logger forwards",
        "the floor for EVERY sink; each page filters further"),
    P("sys_retain_sec", T_U32,
        (BOTANIK_RETAIN_SAVE_MS / 1000.0), 0, 86400, 0, "sec", 974,
        "how often retained values reach flash",
        "0 leaves only `save` and the way down; wear vs loss"),


#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    /* rtu_: RtuClient reads these by name, so the library declares them */
    // rtu_: what an INSTALLATION varies - a cable run, a device mix, a
    // slave-side failsafe timer somebody else configured. What is NOT here is
    // what the bus itself fixes: anything sizing an array (there is nothing to
    // reallocate, and two of them are load-bearing in a static_assert that a
    // full block fits one UART write), MODBUS_GAP_MS (the protocol's own 3.5
    // character silence, with DeviceModel::min_gap_ms overriding per model), and
    // anything the PLANNER reads (those run once at bring-up, so moving one
    // would mean re-planning a running bus and resetting every block's place in
    // the poll ladder - a reboot's worth of disruption for something a reboot
    // already does).
    //
    // No commit serial: every row is read where it is used, on the beat that
    // needs it, and none has a peer to reconnect or a half-applied state to
    // avoid. Bands exclude what is actively harmful rather than merely unusual:
    // an interval of 0 would put a write on the bus every beat, and a timeout
    // under a slave's own turnaround declares a healthy device dead.
    P("rtu_timeout_ms", T_U32,
        BOTANIK_MODBUS_TIMEOUT_MS, 20, 5000, 0, "ms", 980,
        "how long a slave has to answer",
        "the bus default; a device model may declare its own"),
    P("rtu_retries", T_U8,
        BOTANIK_MODBUS_RETRIES, 0, 5, 0, "", 981,
        "immediate retries before a device is backed off",
        "each costs a full timeout on a device that is really gone"),
    P("rtu_backoff_ms", T_U32,
        BOTANIK_MODBUS_BACKOFF_MS, 100, 60000, 0, "ms", 982,
        "first backoff after a device fails its retries",
        "doubles per further failure, up to rtu_backoff_max_ms"),
    P("rtu_backoff_max_ms", T_U32,
        BOTANIK_MODBUS_BACKOFF_MAX_MS, 1000, 600000, 0, "ms", 983,
        "longest a backed-off device waits between probes",
        "what decides whether one dead device costs the whole bus"),
    P("rtu_write_min_ms", T_U32,
        BOTANIK_MODBUS_WRITE_MIN_MS, 20, 60000, 0, "ms", 984,
        "shortest interval between writes to one block",
        "what stops a chattering demand monopolising the bus"),
    P("rtu_rewrite_ms", T_U32,
        BOTANIK_MODBUS_REWRITE_MS, 1000, 600000, 0, "ms", 985,
        "how often an output block is written anyway",
        "MATCH the slave's own failsafe timer; it is watching for this"),
#endif
    };

    }  // namespace

    void register_library_vars(Bank& bank) {
        (void)bank.register_vars(library_vars,
                                 (uint16_t)(sizeof(library_vars) / sizeof(library_vars[0])));
    }

}}  // namespace Botanik::Vars
