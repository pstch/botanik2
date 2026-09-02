/**
 * @file vars_table.h
 * @brief The library's OWN rows: how many there are, and what they are
 *        numbered. Registered ahead of the site's, into the same table.
 */

#ifndef LIBBOTANIK_VARS_TABLE_H
#define LIBBOTANIK_VARS_TABLE_H

// Included by firmware/vars_wiring.cpp, AFTER the feature-gated includes
// its row defaults read. Not a standalone header.

namespace Botanik { namespace Vars {

/** @brief The library's own rows. Its PARAMETERS come last, so a site's
 *  parameter segment abuts them. @see lib_order */
const Descriptor library_vars[] = {
#if BOTANIK_FEATURE_NETWORK
    A("alarm_net_fault", "NET_FLT", 5, 5, 0, 0, 986, "the network link is down", "bring-up failed or the link was lost; check the cable, the switch and net_ip"),
#endif
#if BOTANIK_FEATURE_NTP
    A("alarm_ntp_timeout", "NTP_TIM", 5, 5, 0, 0, 987, "no NTP sync", "the wall clock is free-running: anything dated, scheduled or solar is adrift"),
#endif
#if BOTANIK_FEATURE_LOGGING
    A("alarm_log_flood", "LOG_FLD", 2, 2, 0, 0, 988, "log messages are being dropped", "the rate limiter is shedding; something is repeating itself and the record is incomplete"),
#endif
    A("alarm_system_reset", "SYS_RST"  , -1, 0 , 30*24*3600, 0  , 990, "unit reset recently", ""),
    A("alarm_cpu_xrun"    , "CPU_XRUN" , 0 , -1, 24*3600   , 300, 991, "scheduler overrun (skipped beat)", ""),
    A("alarm_cpu_load"    , "CPU_LOAD" , 5 , 5 , 24*3600   , 300, 992, "CPU load over threshold", ""),
    A("alarm_stack"       , "STACK"    , 5 , 5 , 30*24*3600, 300, 993, "stack usage over threshold", ""),
    A("alarm_cpu_jitter"  , "CPU_JITT" , 5 , 5 , 24*3600   , 300, 994, "CPU jitter over threshold", ""),
    A("alarm_flash_error" , "FLASH_FLT", 0 , -1, 30*24*3600, 0  , 995, "persistent storage write failed", "parameters will not survive a reboot; check the KV region"),
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    A("alarm_fieldbus", "BUS_DEAD", 0, 5, 0, 0, 996, "a field device is not answering", "check the RS-485 wiring, termination and the device's own power"),
#endif
#if BOTANIK_FEATURE_PLC
    A("alarm_scan_late", "SCAN_LATE", 5, 30, 0, 0, 997, "a control program is missing its period", "check the TASKS page and the per-program cost; something is overrunning or the site asks for more than the beat holds"),
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    A("alarm_fieldbus_line", "BUS_LINE", 0, 5 , 0         , 0, 998, "the whole segment has gone quiet", "the trunk, the termination, this unit's transceiver, or a second master - not twenty devices that failed at once"),
    A("alarm_io_drop"      , "IO_DROP" , 0, -1, 30*24*3600, 0, 999, "a field row is never scanned", "the planner could not place it: the device model's limits, or more rows than the plan holds. It is a signal that will never be read"),
    SV("sys_fieldbus_dead"  , T_U16, 0, 0, "", 902, "field devices not answering", "0 when every planned device is talking"),
    SV("sys_fieldbus_crc"   , T_U16, 0, 0, "", 903, "frames that failed their checksum", "rising means the line: termination, stub length, or the wrong baud"),
    SV("sys_fieldbus_stray" , T_U16, 0, 0, "", 904, "bytes that arrived out of turn", "rising means a second master, a duplicate address, or a device answering after its turn had passed"),
#endif
#if BOTANIK_FEATURE_PLC
    /* The scan CLOCK, not an elapsed time. A C++ law is handed dt through
     * ProgramContext; ST has no argument to be handed anything, so a block
     * reads this and keeps its own last value. That makes it rate-independent
     * - the same block works on any task - and it is the simulator's VIRTUAL
     * clock, so accelerated time works unchanged.
     * Wraps at 2^32 ms, about 49 days: unsigned subtraction carries it, and
     * no block goes that long between scans.
     * Gated on PLC, which is what reads it. It sat under the RS-485 client's
     * guard, where a build with programs and no field bus lost the clock and
     * every ST timer resolved to the inert sink. */
    SV("sys_now_ms"         , T_U32, 0, 0, "ms", 906, "the scan clock, milliseconds since boot", "ST blocks difference it themselves; wraps at ~49 days and unsigned subtraction carries it"),
    /* The DATE, for a law that counts in days rather than in milliseconds.
     * Straight off the wall clock and NOT through cal_time_ms(), which takes
     * an UPTIME instant and projects it onto the wall: handing it a wall time
     * adds the boot epoch twice, and the date came out around 2083 and gained
     * a day every twelve hours. 0 is "the RTC has never been set", which is
     * a real answer and the one a schedule refuses to run against. */
    SV("sys_today"          , T_U32, 0, 0, "days", 907, "days since 1 Jan 1970, 0 before the clock is set", "the wall clock as a DATE; a schedule counts in these and cannot run on 0"),
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
    /* The tracker's own outputs. It publishes them itself, so a site gets the
     * sun by turning the feature on rather than by writing a program to copy
     * the instrument onto rows. Its INPUTS (geo_*) are the library's already. */
    SV("sol_ready"       , T_BOOL, 0, 0, ""   , 943, "the solar tracker has a location and a clock", "one row, not one per solar output: they all go together, and every boot passes through this until NTP lands"),
    SV("sol_fromRise_min", T_I16 , 0, 0, "min", 944, "minutes since sunrise; negative before it", "what the sol_ phase offsets are measured against"),
    SV("sol_fromSet_min" , T_I16 , 0, 0, "min", 945, "minutes since sunset; negative before it", "held until the tracker has a location and a wall clock"),
    SV("sol_elev_deg"    , T_F32 , 0, 1, "deg", 946, "the sun's elevation above the horizon", "negative below it; what the day's shape is anticipated from"),
#endif
#if BOTANIK_FEATURE_LOGS_MONITOR
    P("monitor_baud" , T_U32, 115200         , 1200, 921600, 0, "Bd", 901, "monitor UART rate", "log-only port; ignored on the OPTA, whose MONITOR is USB CDC", true),
    PE("monitor_log_level", Logs::LOG_DEBUG, &Logs::level_label, 905, "lowest level the monitor port carries", "open by default: this port exists to be the full record", true),
#endif
#if BOTANIK_FEATURE_NETWORK
    P("net_serial", T_U32 , 1              , 0, 9999, 0, "", 910, "bump to apply staged net_ edits", "", true),
    P("net_enable", T_BOOL, 1              , 0, 1   , 0, "", 917, "bring the network up", "off takes the link down; the field bus is unaffected", true),
    P("net_dhcp"  , T_BOOL, 0              , 0, 1   , 0, "", 911, "enable DHCP IP configuration", "ignores the static ip/mask/gw rows", true),
    PS("net_ip"    , "192.168.1.10" , BOTANIK_HOST_STR_LEN, STR_IPV4    , 912, "static IP address", "", true),
    PS("net_mask"  , "255.255.255.0", BOTANIK_HOST_STR_LEN, STR_IPV4    , 913, "static IP netmask", "", true),
    PS("net_gw"    , "192.168.1.1"  , BOTANIK_HOST_STR_LEN, STR_IPV4    , 914, "static IP gateway", "", true),
    PS("net_dns1"  , "8.8.8.8"      , BOTANIK_HOST_STR_LEN, STR_IPV4    , 915, "static DNS resolver 1", "", true),
    PS("net_dns2"  , "4.4.4.4"      , BOTANIK_HOST_STR_LEN, STR_IPV4_OPT, 916, "static DNS resolver 2", "empty = use dns1 alone", true),
#endif
#if BOTANIK_FEATURE_NTP
    P("ntp_serial"  , T_U32 , 1   , 0 , 9999 , 0, ""   , 920, "bump to apply staged ntp_ edits", "", true),
    P("ntp_enable"  , T_BOOL, 1   , 0 , 1    , 0, ""   , 921, "enable NTP clock sync", "", true),
    P("ntp_poll_sec", T_U32 , 3600, 10, 86400, 0, "sec", 922, "NTP poll interval, in seconds", "", true),
    P("ntp_port"    , T_U16 , 123 , 1 , 65535, 0, ""   , 923, "NTP server port", "standard is 123", true),
    PS("ntp_srv1"    , ""  , BOTANIK_HOST_STR_LEN, STR_HOST    , 924, "NTP server 1", "blank: falls back to net_gw. A default here would guess at somebody else's network", true),
    PS("ntp_srv2"    , ""  , BOTANIK_HOST_STR_LEN, STR_HOST_OPT, 925, "NTP server 2", "empty = no fallback", true),
#endif
#if BOTANIK_FEATURE_MQTT
    P("mqtt_serial" , T_U32 , 1   , 0 , 9999 , 0, ""   , 930, "bump to apply staged mqtt_ edits", "", true),
    P("mqtt_enable" , T_BOOL, 1   , 0 , 1    , 0, ""   , 931, "enable MQTT", "", true),
    PS("mqtt_host"   , ""  , BOTANIK_HOST_STR_LEN, STR_HOST, 932, "MQTT broker host", "blank: falls back to net_gw. A default here would guess at somebody else's network", true),
    P("mqtt_port"   , T_U16 , 1883, 1 , 65535, 0, ""   , 933, "MQTT broker port", "standard is 1883", true),
    P("mqtt_ka_sec" , T_U16 , 20  , 10, 1800 , 0, "sec", 934, "MQTT keep-alive interval", "also the dead-broker detection", true),
    P("mqtt_tlm_sec", T_U32 , 30  , 0 , 86400, 0, "sec", 935, "telemetry publish cadence", "0 stops publishing telemetry", true),
    P("mqtt_retain" , T_BOOL, 1   , 0 , 1    , 0, ""   , 936, "publish telemetry retained", "on, the broker keeps serving a dead sensor's last good value", true),
#endif
#if BOTANIK_FEATURE_LOGS_PUBLISH
    PE("mqtt_level", Logs::LOG_DEBUG, &Logs::level_label, 937, "lowest level published to the broker", "every entry costs a publish, so this is the floor to raise first", true),
#endif
#if BOTANIK_FEATURE_SUN_TRACKER
    P("geo_serial" , T_U32, 1  , 0     , 9999 , 0, ""   , 940, "bump to apply staged geo_ edits", "", true),
    P("geo_lat_deg", T_F32, 0.0, -90.0 , 90.0 , 2, "deg", 941, "geographical latitude, in degrees", "per site location", true),
    P("geo_lon_deg", T_F32, 0.0, -180.0, 180.0, 2, "deg", 942, "geographical longitude, in degrees", "per site location", true),
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
    P("mb_serial"      , T_U32 , 1  , 0, 9999 , 0, "", 950, "bump to apply staged mb_ edits", "", true),
    P("mb_enable"      , T_BOOL, 1  , 0, 1    , 0, "", 951, "serve the Modbus TCP slave", "", true),
    P("mb_port"        , T_U16 , 502, 1, 65535, 0, "", 952, "Modbus TCP slave port", "standard is 502", true),
    P("mb_write_enable", T_BOOL, 1  , 0, 1    , 0, "", 953, "let a master write parameters", "off makes the slave read-only", true),
#endif
#if BOTANIK_FEATURE_CONSOLE
    P("console_serial", T_U32, 1   , 0   , 9999  , 0, ""   , 960, "bump to apply staged console_ edits", "", true),
    P("console_baud"  , T_U32, 4800, 1200, 921600, 0, "Bd" , 900, "console UART rate", "reconnect at the new rate; unsaved, so a power cycle undoes it", true),
    P("console_type"  , T_U8 , 1   , 0   , 3     , 0, ""   , 961, "which terminal is on the console", "0=VT100 1=VT102 2=Minitel 1B 3=Minitel 2", true),
    P("console_cols"  , T_U16, 80  , 80  , 132   , 0, "col", 962, "console width, in columns", "the screen buffer bounds the maximum; a site needing another restates the row", true),
    P("console_rows"  , T_U16, 24  , 20  , 32    , 0, "row", 963, "console height, in rows", "the screen buffer bounds the maximum; a site needing another restates the row", true),
#endif
#if BOTANIK_FEATURE_CONSOLE && BOTANIK_FEATURE_HOME_PAGE
    PE("home_log_level", Logs::LOG_INFO, &Logs::level_label, 964, "lowest level HOME's log tail shows", "a landing screen: only what is worth looking up for", true),
#endif
#if BOTANIK_FEATURE_CONSOLE && BOTANIK_FEATURE_SHELL_PAGE
    PE("shell_log_level", Logs::LOG_NOTICE, &Logs::level_label, 965, "lowest level the SHELL page interleaves", "`logs off` silences it entirely without moving this", true),
#endif
#if BOTANIK_FEATURE_CONSOLE && BOTANIK_FEATURE_LOGS_PAGE
    PE("logs_log_level", Logs::LOG_DEBUG, &Logs::level_label, 966, "lowest level the LOGS page keeps", "this page is the record; raising it loses the record", true),
#endif
    P("sys_cpu_load_pct", T_U8 , 50             , 1, 100  , 0, "%"  , 970, "CPU_LOAD alarm threshold", "percent of the task period", true),
    P("sys_jitter_pct"  , T_U8 , 50             , 1, 100  , 0, "%"  , 971, "CPU_JITT alarm threshold", "percent of the task period", true),
    P("sys_stack_pct"   , T_U8 , 75             , 1, 100  , 0, "%"  , 972, "STACK alarm threshold", "percent of the stack in use", true),
    P("sys_startup_sec" , T_U16, BOTANIK_STARTUP_HOLD_MS / 1000.0, 0, 600, 0, "sec", 973, "control held after boot, seconds", "nothing runs a law on a field that has not answered yet; 0 runs from the first beat", true),
    P("sys_control"     , T_BOOL, 1, 0, 1, 0, "", 976, "let the control programs drive", "off stops every law and drops what they were driving to its declared safe state; per-program is `programs disable`", true),
    P("sys_shutdown_sec", T_U16, BOTANIK_SHUTDOWN_HOLD_MS / 1000.0, 0, 600, 0, "sec", 975, "laws stopped before the ending, seconds", "the mirror of sys_startup_sec: time for the record to reach flash before a reset", true),
    P("sys_retain_sec"  , T_U32, 300            , 0, 86400, 0, "sec", 974, "how often retained values reach flash", "0 leaves only `save` and the way down; wear vs loss", true),
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    P("rtu_timeout_ms"    , T_U32, 300  , 20  , 5000  , 0, "ms", 980, "how long a slave has to answer", "the bus default; a device model may declare its own", true),
    P("rtu_retries"       , T_U8 , 1    , 0   , 5     , 0, ""  , 981, "immediate retries before a device is backed off", "each costs a full timeout on a device that is really gone", true),
    P("rtu_backoff_ms"    , T_U32, 1000 , 100 , 60000 , 0, "ms", 982, "first backoff after a device fails its retries", "doubles per further failure, up to rtu_backoff_max_ms", true),
    P("rtu_backoff_max_ms", T_U32, 60000, 1000, 600000, 0, "ms", 983, "longest a backed-off device waits between probes", "what decides whether one dead device costs the whole bus", true),
    P("rtu_write_min_ms"  , T_U32, 200  , 20  , 60000 , 0, "ms", 984, "shortest interval between writes to one block", "what stops a chattering demand monopolising the bus", true),
    P("rtu_rewrite_ms"    , T_U32, 5000 , 1000, 600000, 0, "ms", 985, "how often an output block is written anyway", "MATCH the slave's own failsafe timer; it is watching for this", true),
#endif
};

const uint16_t LIBRARY_VARS_COUNT =
    (uint16_t)(sizeof(library_vars) / sizeof(library_vars[0]));

}}  // namespace Botanik::Vars

#endif  // LIBBOTANIK_VARS_TABLE_H
