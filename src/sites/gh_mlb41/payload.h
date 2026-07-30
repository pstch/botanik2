#ifndef SITE_PAYLOAD_H
#define SITE_PAYLOAD_H

/**
 * @file payload.h
 * @brief GENERATED - the gh_mlb41 PLC payload: the two shared-memory structs
 *        exchanged with the coprocessor, their field-domain enums, and the
 *        layout asserts.
 *
 * Generated from the Arduino PLC IDE project. Regenerating overwrites this
 * file: hand-written site code belongs in site.h / commands.h / sim.h.
 *
 * The payload types stay at GLOBAL scope, matching the PLC IDE's generated
 * types, and are the byte-for-byte wire contract: both the simulator's mock PLC
 * and the OPTA firmware use them verbatim. Every declarative table addresses
 * these structs by byte offset (offsetof), so field ORDER here is load-bearing.
 *
 * PLC-style conventions: real-valued quantities are floats in native units
 * (V / Hz / degC / %), counters are sized integers, discrete states are u8
 * enums, and each alarm is its own u8 boolean (there is no packed bitset).
 * Members are ordered widest-first and explicitly padded so the combined
 * PlcInput/PlcOutput<> layout carries no implicit padding.
 */

#include <cstdint>

#include "botanik.h"

/** @name Field-domain enums
 *  Each discrete status wraps an `enum Value` plus a static str() converter,
 *  giving a namespace (UpsStatus::ONLINE) and the str() a C++11 enum can't
 *  carry. Payload fields stay raw uint8_t for the wire; str() takes that raw
 *  value (UpsStatus::str(d->ups_status)).
 *  @{ */

/** @brief UPS operating state. */
struct UpsStatus {
    enum Value : uint8_t {
        OFFLINE    = 0,
        ONLINE     = 1,
        ON_BATTERY = 2,
        FAULT      = 3,
    };
    /** @brief Short display string for a raw ups_status payload value. */
    static const char* str(uint8_t s) {
        switch (s) {
            case ONLINE:     return "ONLINE";
            case ON_BATTERY: return "BATTERY";
            case FAULT:      return "FAULT";
            default:         return "OFFLINE";
        }
    }
};

/** @brief Irrigation tray cycle phase. */
struct TrayStatus {
    enum Value : uint8_t {
        IDLE     = 0,
        FILLING  = 1,
        SOAKING  = 2,
        DRAINING = 3,
    };
    /** @brief Short display string for a raw tray_status payload value. */
    static const char* str(uint8_t s) {
        switch (s) {
            case FILLING:  return "FILLING";
            case SOAKING:  return "SOAKING";
            case DRAINING: return "DRAINING";
            default:       return "IDLE";
        }
    }
};

/** @brief Network link state. */
struct NetStatus {
    enum Value : uint8_t {
        DOWN   = 0,   ///< no link
        LINK   = 1,   ///< L2 up, no address yet
        ONLINE = 2,   ///< up with an address
    };
    /** @brief Short display string for a raw net_status payload value. */
    static const char* str(uint8_t s) {
        switch (s) {
            case ONLINE: return "ONLINE";
            case LINK:   return "LINK";
            default:     return "DOWN";
        }
    }
};

/** @} */

/** @name PLC payload
 *  @{ */

/** @brief Config parameters (network / NTP / MQTT / geo), embedded verbatim in
 *  both the input payload (host stages edits at in_offset) and the output
 *  payload (PLC reflects the live values at out_offset), so a parameter's
 *  in_offset and out_offset are genuinely distinct. Each group's commit serial
 *  (@c net / @c ntp / @c mqtt / @c geo) is an ordinary u32 field the user bumps
 *  on the params page to commit that group's staged edits; the PLC latches the
 *  group and reflects the serial back, which the consuming Component watches.
 *  Ordered widest-first (and _pad'd) so the sub-struct - and both payloads that
 *  embed it - stay 4-byte-aligned and padding-free. */
struct GreenhouseConfig_t {
    static const int STRLEN = (int)Botanik::Config::PLC_HOST_STR_LEN;
    // 4-byte: geo values + the per-group commit serials
    float    geo_latitude;       ///< site latitude, decimal degrees
    float    geo_longitude;      ///< site longitude, decimal degrees
    uint32_t net;                ///< network group commit serial (bump to apply)
    uint32_t ntp;                ///< NTP group commit serial
    uint32_t mqtt;               ///< MQTT group commit serial
    uint32_t geo;                ///< geo group commit serial
    // 2-byte
    uint16_t ntp_poll_s;         ///< NTP poll interval, s (0 = firmware default)
    uint16_t mqtt_port;          ///< broker TCP port (0 = firmware default)
    uint16_t mqtt_keepalive_s;   ///< MQTT keepalive, s (0 = firmware default)
    // 1-byte flags
    uint8_t  net_dhcp;           ///< DHCP (1) or static IP (0)
    uint8_t  ntp_enabled;        ///< enable NTP time sync
    uint8_t  mqtt_enabled;       ///< enable MQTT publishing
    uint8_t  _pad[3];            ///< align the string block (and total) to 4
    // strings (each PLC_HOST_STR_LEN, a 4-byte multiple)
    char     net_ip[STRLEN];     ///< static IPv4, used when net_dhcp == 0
    char     net_netmask[STRLEN];
    char     net_gateway[STRLEN];
    char     net_dns1[STRLEN];
    char     net_dns2[STRLEN];   ///< empty = none
    char     ntp_server1[STRLEN];
    char     ntp_server2[STRLEN];///< empty = none
    char     mqtt_broker[STRLEN];
};

/** @brief Host -> PLC payload: the config write-back mirror (host stages edited
 *  config values here; the PLC latches a group when its serial field bumps). */
struct PlcInputData_t {
    GreenhouseConfig_t config;
};

/**
 * @brief Greenhouse PLC output payload (sensors + actuator feedback).
 */
struct PlcOutputData_t {
    // Power (float volts / Hz)
    float    grid_ac_v;          ///< mains AC RMS voltage, V
    float    dc_bus_v;           ///< DC system bus voltage, V
    float    battery_v;          ///< battery bank voltage, V
    float    generator_v;        ///< generator output voltage, V
    float    grid_freq_hz;       ///< mains frequency, Hz
    // Climate (float, one reading per height zone)
    float    ext_temp_c;         ///< exterior air temp, degC
    float    floor_temp_c;       ///< floor-level temp, degC
    float    ceil_temp_c;        ///< ceiling-level temp, degC
    float    ext_rh_pct;         ///< exterior relative humidity, %
    float    floor_rh_pct;       ///< floor relative humidity, %
    float    ceil_rh_pct;        ///< ceiling relative humidity, %
    /** Seconds since rain was last detected (uint32: well past the uint16 ~18h
     *  ceiling, up to ~136 years). */
    uint32_t rain_age_s;
    // 2-byte counters
    uint16_t wind_peak_kmh;      ///< 15-minute peak wind speed, km/h
    uint16_t water_in_l;         ///< cumulative water input, liters
    uint16_t water_out_lpm;      ///< current drain flow, liters/minute
    uint16_t sunrise_min;        ///< sunrise, minutes since local midnight
    uint16_t sunset_min;         ///< sunset, minutes since local midnight
    int16_t  ntp_offset_ms;      ///< NTP clock offset, milliseconds (signed)
    // Irrigation / actuators / status (u8)
    uint8_t  current_tray;       ///< active tray (1..N), 0 = none
    uint8_t  tray_status;        ///< TrayStatus
    // Actuators (levels in percent)
    uint8_t  window_a;           ///< roof window A opening, percent
    uint8_t  window_b;           ///< roof window B opening, percent
    uint8_t  window_c;           ///< roof window C opening, percent
    uint8_t  window_d;           ///< roof window D opening, percent
    uint8_t  blower_a;           ///< blower A level, percent
    uint8_t  blower_b;           ///< blower B level, percent
    uint8_t  blower_c;           ///< blower C level, percent
    uint8_t  blower_d;           ///< blower D level, percent
    uint8_t  extractor_a;        ///< air extractor A level, percent
    uint8_t  extractor_b;        ///< air extractor B level, percent
    uint8_t  thermal_screen;     ///< thermal screen closure, percent
    // Discrete status / monitored autonomous systems
    uint8_t  ups_status;         ///< UpsStatus
    uint8_t  dc_bus_a_ok;        ///< DC rectifier A healthy, boolean
    uint8_t  dc_bus_b_ok;        ///< DC rectifier B healthy, boolean
    uint8_t  rain_detected;      ///< rain sensor, boolean
    uint8_t  cooling_on;         ///< evaporative cooling (humidifier) on, bool
    uint8_t  generator_running;  ///< emergency generator, boolean (monitored)
    uint8_t  heat_blower_on;     ///< emergency heat blower, boolean (monitored)
    uint8_t  heatpump_on;        ///< heatpump, boolean (monitored)
    uint8_t  net_status;         ///< NetStatus (down / link / online)
    uint8_t  ntp_synced;         ///< NTP disciplined the RTC, boolean
    // Per-alarm booleans (1 = active), one per alarms.h row in table order; the
    // row index is the alarm bit, and the table carries each field's offset so
    // AlarmReader folds them into the raw bitset. On OPTA these mirror the PLC
    // program's alarm outputs.
    uint8_t  alarm_ac_grid;      ///< AC_GRID
    uint8_t  alarm_dc_ps1;       ///< DC_PS1
    uint8_t  alarm_gen_flt;      ///< GEN_FLT
    uint8_t  alarm_ac_local;     ///< AC_LOCAL
    uint8_t  alarm_dc_ps2;       ///< DC_PS2
    uint8_t  alarm_dc_ups;       ///< DC_UPS
    uint8_t  alarm_dc_local;     ///< DC_LOCAL
    uint8_t  alarm_gen_batt;     ///< GEN_BATT
    uint8_t  alarm_plc_flt;      ///< PLC_FLT
    uint8_t  alarm_net_flt;      ///< NET_FLT
    uint8_t  alarm_ntp_tim;      ///< NTP_TIM
    uint8_t  alarm_act_flt;      ///< ACT_FLT
    uint8_t  alarm_sns_flt;      ///< SNS_FLT
    uint8_t  alarm_too_hot;      ///< TOO_HOT
    uint8_t  alarm_too_cold;     ///< TOO_COLD
    uint8_t  alarm_too_hum;      ///< TOO_HUM
    uint8_t  alarm_tmp_str;      ///< TMP_STR
    uint8_t  alarm_wtr_lk;       ///< WTR_LK
    uint8_t  alarm_wtr_bar;      ///< WTR_BAR
    uint8_t  alarm_wtr_clog;     ///< WTR_CLOG
    uint8_t  alarm_io_drop;      ///< IO_DROP
    uint8_t  alarm_log_fld;      ///< LOG_FLD
    uint8_t  alarm_wh_storm;     ///< WH_STORM
    uint8_t  _reserved[2];       ///< pad the alarm block to a 4-byte multiple
    // Config parameters + per-group commit serials, reflected by the PLC. The
    // params page reads these (out_offset); a commit serial bumps here after the
    // PLC applies a staged edit, which the consuming Component watches.
    GreenhouseConfig_t config;
};

/** @} */

/** @name Layout asserts
 *  The wire contract, checked at compile time. The 4-byte-multiple rule keeps
 *  the combined PlcInput/PlcOutput<> padding-free and satisfies ordered_copy's
 *  word-multiple requirement. The offset ceiling is the table contract: every
 *  declarative table addresses these structs with a 16-bit byte offset.
 *  @{ */

static_assert(sizeof(PlcInputData_t)  % 4 == 0, "PlcInputData_t must be 4-byte aligned");
static_assert(sizeof(PlcOutputData_t) % 4 == 0, "PlcOutputData_t must be 4-byte aligned");
static_assert(sizeof(PlcInputData_t)  <= 32767, "PlcInputData_t exceeds the table offset range");
static_assert(sizeof(PlcOutputData_t) <= 32767, "PlcOutputData_t exceeds the table offset range");

/** @} */

// On OPTA, verify the payload against the PLC IDE's generated types once they
// are in scope in the integrator's entry file:
//   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(Botanik::PlcIO::PlcInput<PlcInputData_t>));
//   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(Botanik::PlcIO::PlcOutput<PlcOutputData_t>));

#endif // SITE_PAYLOAD_H
