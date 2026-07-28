// REVIEW_35: TODO
#ifndef SITES_GH_MLB41_SITE_H
#define SITES_GH_MLB41_SITE_H

/**
 * @file site.h
 * @brief gh_mlb41 site: the PLC payload, the firmware subclass, and the
 *        uniform handle a target harness builds against.
 *
 * A target selects a site with `-DBOTANIK_SITE=<name>`; CMake points
 * BOTANIK_SITE_HEADER at this file, so the sim harness and OPTA firmware
 * include it without naming the site. Each site exposes the same surface:
 *   - `BotanikSite`           - the firmware type to instantiate (a
 *                               BotanikFirmware<>).
 *   - `PlcInput/OutputData_t` - the site PLC payload (shared verbatim by the
 *                               mock PLC and the OPTA firmware).
 *   - `fill_greenhouse_output()` / `plc_dilated_timeout()` - the sim-only mock
 *                               PLC data animator + timeout dilation (sim.h).
 *
 * Single source of truth for the greenhouse: the payload, the declarative tables
 * (status variables, shell commands, and the alarm table in alarms.h), the
 * @ref g_site SiteDescriptor, and the thin GreenhouseMLB41 binding. The base
 * (BotanikFirmware) owns every page and wires the tables - the site declares no
 * pages. Config overrides stay in config.h (CMake includes it before the library
 * defaults). See firmware.h for the extension model.
 */

#include <cstddef>  // offsetof - the telemetry scalar table
#include <cstdint>
#include <cstdio>   // snprintf - the example site command

#include "libbotanik/core/firmware.h"
#include "libbotanik/shell/shell.h"   // ShellCommand - the site command table
// The CONFIG / MONITOR / HOME pages are base-owned now (BotanikFirmware); a
// site supplies only the declarative tables below (see SiteDescriptor).

// ===========================================================================
// PLC payload (shared verbatim by the mock PLC and the OPTA firmware)
// ===========================================================================
//
// On OPTA PlcOutputData_t mirrors the PLC IDE's generated type, verified with:
//   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<PlcInputData_t>));
//   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<PlcOutputData_t>));
// PLC-style integer conventions (no floats on the wire): millivolts,
// centi-degrees C, centi-percent humidity, percent actuator levels, discrete
// states as enums / flags.

/** @name PLC payload
 *  @{ */

// no site-specific data beyond the library log fields
struct PlcInputData_t {};

// Each discrete status wraps an `enum Value` plus a static str() converter,
// giving a namespace (UpsStatus::ONLINE) and the str() C++ enums can't carry.
// Payload fields stay raw uint8_t for the wire; str() takes that raw value
// (UpsStatus::str(d->ups_status)).

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

/**
 * @brief Greenhouse PLC output payload (sensors + actuator feedback).
 *
 * Padded to a 4-byte multiple and ordered widest-first so the combined
 * PlcOutput<> layout has no implicit padding (matches the byte-for-byte
 * contract in plcio/vars.h).
 */
struct PlcOutputData_t {
    // 4-byte members first (floats in native units + the one u32 counter), then
    // 2-byte, then the u8 flags/levels - widest-first so the combined PlcOutput<>
    // has no implicit padding. Real-valued quantities are floats (V / Hz / degC /
    // %), not scaled integers. Each alarm is an individual boolean near the end.
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
    // Per-alarm booleans (1 = active), one per g_alarms row in table order; the
    // row index is the alarm bit, and g_alarms carries each field's offset so
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
    uint8_t  _reserved[2];       ///< pad to a 4-byte multiple
};
static_assert(sizeof(PlcOutputData_t) % 4 == 0, "PlcOutputData_t must be 4-byte aligned");

/** @} */

// The alarm table builds on the payload above (offsetof), so it is included
// after it.
#include "sites/gh_mlb41/alarms.h"

// ===========================================================================
// Site shell command (example extension via the register_commands() hook)
// ===========================================================================

namespace {

/** @brief Example site command: a one-line greenhouse summary from live PLC
 *  data.
 *
 *  Shows the extension model (firmware.h): the ctx passed to set_commands()
 *  is the const PlcOutputData_t* the command reads. */
inline void cmd_gh(Botanik::Shell::ShellWriter& out, const char*, uint16_t, void* ctx) {
    const PlcOutputData_t* d = static_cast<const PlcOutputData_t*>(ctx);
    char buf[96];
    int n;
    if (d == nullptr)
        n = snprintf(buf, sizeof(buf), "%s: no PLC data", Botanik::Config::SITE_NAME);
    else if (d->current_tray)
        n = snprintf(buf, sizeof(buf), "%s: tray %u %s, UPS %s, net %s",
                     Botanik::Config::SITE_NAME, (unsigned)d->current_tray,
                     TrayStatus::str(d->tray_status), UpsStatus::str(d->ups_status),
                     NetStatus::str(d->net_status));
    else
        n = snprintf(buf, sizeof(buf), "%s: idle, UPS %s, net %s",
                     Botanik::Config::SITE_NAME, UpsStatus::str(d->ups_status),
                     NetStatus::str(d->net_status));
    if (n > 0) out.write(buf, (size_t)n);
}

/** @brief gh_mlb41 shell commands, registered after the built-ins (can't
 *  shadow them). */
const Botanik::Shell::ShellCommand g_site_commands[] = {
    { "gh", "greenhouse summary (live PLC data)", &cmd_gh },
};

// Site status-variable table: the live PLC payload grouped by domain, read
// generically by MQTT telemetry, the shell `status` command, and the MONITOR
// page. Each group is a small ScalarField[] published under
// "<prefix>/telemetry/<group>/<leaf>",
// alongside the library "system" set; MqttSender reads + renders every field
// generically off the payload (no per-field code). A field is a leaf name, the
// payload offset, a ScalarType (read width/sign), and for fixed-point a divisor
// + decimals (raw / divisor, centi/milli units); a SCALAR_ENUM field renders its
// raw byte through a status str(). Leaves drop the group prefix (the topic
// already carries it: ac_supply/grid_v, not ac_supply/grid_ac_v).
using namespace Botanik;

// Trailing fields per row: str, description, unit, notes. Notes are optional
// (left off on most rows). Silence the pedantic missing-field-initializer note
// (str/notes unset on many rows).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// Read-only status variables: out_offset = payload field, in_offset = -1. Real
// values are SCALAR_F32 in native units. Fields per row: name, out_offset,
// in_offset, type, digits, str, unit, desc, notes.
#define SO(f) (int16_t)offsetof(PlcOutputData_t, f)
const ScalarField g_ac_supply[] = {
    { "grid_v",            SO(grid_ac_v),         -1, SCALAR_F32, 1,  nullptr,         "V",  "Mains AC voltage",   "" },
    { "grid_freq_hz",      SO(grid_freq_hz),      -1, SCALAR_F32, 2,  nullptr,         "Hz", "Mains frequency",    "" },
    { "generator_v",       SO(generator_v),       -1, SCALAR_F32, 1,  nullptr,         "V",  "Generator voltage",  "" },
    { "generator_running", SO(generator_running), -1, SCALAR_U8, 0,   nullptr,         "",   "Generator running",  "" },
    { "ups",               SO(ups_status),        -1, SCALAR_ENUM, 0, &UpsStatus::str, "",   "UPS state", "OFFLINE/ONLINE/BATTERY/FAULT" },
};

const ScalarField g_dc_supply[] = {
    { "bus_v",          SO(dc_bus_v),   -1, SCALAR_F32, 1, nullptr, "V", "DC bus voltage",       "" },
    { "battery_v",      SO(battery_v),  -1, SCALAR_F32, 1, nullptr, "V", "Battery bank voltage", "" },
    { "rectifier_a_ok", SO(dc_bus_a_ok),-1, SCALAR_U8, 0,  nullptr, "",  "Rectifier A healthy",  "" },
    { "rectifier_b_ok", SO(dc_bus_b_ok),-1, SCALAR_U8, 0,  nullptr, "",  "Rectifier B healthy",  "" },
};

const ScalarField g_environment[] = {
    { "ext_temp_c",    SO(ext_temp_c),   -1, SCALAR_F32, 1, nullptr, "C",    "Exterior temperature",      "" },
    { "floor_temp_c",  SO(floor_temp_c), -1, SCALAR_F32, 1, nullptr, "C",    "Floor-level temperature",   "" },
    { "ceil_temp_c",   SO(ceil_temp_c),  -1, SCALAR_F32, 1, nullptr, "C",    "Ceiling-level temperature", "" },
    { "ext_rh_pct",    SO(ext_rh_pct),   -1, SCALAR_F32, 1, nullptr, "%",    "Exterior humidity",         "" },
    { "floor_rh_pct",  SO(floor_rh_pct), -1, SCALAR_F32, 1, nullptr, "%",    "Floor humidity",            "" },
    { "ceil_rh_pct",   SO(ceil_rh_pct),  -1, SCALAR_F32, 1, nullptr, "%",    "Ceiling humidity",          "" },
    { "wind_peak_kmh", SO(wind_peak_kmh),-1, SCALAR_U16, 0, nullptr, "km/h", "15-min peak wind",          "" },
    { "rain_age_s",    SO(rain_age_s),   -1, SCALAR_U32, 0, nullptr, "s",    "Time since last rain", "0 while raining" },
    { "rain_detected", SO(rain_detected),-1, SCALAR_U8, 0,  nullptr, "",     "Rain sensor",               "" },
};

const ScalarField g_irrigation[] = {
    { "water_in_l",    SO(water_in_l),   -1, SCALAR_U16, 0,  nullptr, "L",     "Cumulative water input", "" },
    { "water_out_lpm", SO(water_out_lpm),-1, SCALAR_U16, 0,  nullptr, "L/min", "Current drain flow",     "" },
    { "current_tray",  SO(current_tray), -1, SCALAR_U8, 0,   nullptr, "",      "Active tray",            "0 = none" },
    { "tray_status",   SO(tray_status),  -1, SCALAR_ENUM, 0, &TrayStatus::str, "", "Tray cycle phase", "IDLE/FILLING/SOAKING/DRAINING" },
};

const ScalarField g_ventilation[] = {
    { "window_a",    SO(window_a),    -1, SCALAR_U8, 0, nullptr, "%", "Roof window A opening", "" },
    { "window_b",    SO(window_b),    -1, SCALAR_U8, 0, nullptr, "%", "Roof window B opening", "" },
    { "window_c",    SO(window_c),    -1, SCALAR_U8, 0, nullptr, "%", "Roof window C opening", "" },
    { "window_d",    SO(window_d),    -1, SCALAR_U8, 0, nullptr, "%", "Roof window D opening", "" },
    { "blower_a",    SO(blower_a),    -1, SCALAR_U8, 0, nullptr, "%", "Blower A level",        "" },
    { "blower_b",    SO(blower_b),    -1, SCALAR_U8, 0, nullptr, "%", "Blower B level",        "" },
    { "blower_c",    SO(blower_c),    -1, SCALAR_U8, 0, nullptr, "%", "Blower C level",        "" },
    { "blower_d",    SO(blower_d),    -1, SCALAR_U8, 0, nullptr, "%", "Blower D level",        "" },
    { "extractor_a", SO(extractor_a), -1, SCALAR_U8, 0, nullptr, "%", "Air extractor A level", "" },
    { "extractor_b", SO(extractor_b), -1, SCALAR_U8, 0, nullptr, "%", "Air extractor B level", "" },
};

const ScalarField g_climate[] = {
    { "heatpump",       SO(heatpump_on),    -1, SCALAR_U8, 0, nullptr, "",  "Heatpump",               "" },
    { "heat_blower",    SO(heat_blower_on), -1, SCALAR_U8, 0, nullptr, "",  "Emergency heat blower",  "" },
    { "cooling",        SO(cooling_on),     -1, SCALAR_U8, 0, nullptr, "",  "Evaporative cooling",    "" },
    { "thermal_screen", SO(thermal_screen), -1, SCALAR_U8, 0, nullptr, "%", "Thermal screen closure", "" },
};

const ScalarField g_system[] = {
    { "net_status",    SO(net_status),    -1, SCALAR_ENUM, 0, &NetStatus::str, "",    "Network link state", "DOWN/LINK/ONLINE" },
    { "ntp_synced",    SO(ntp_synced),    -1, SCALAR_U8, 0,   nullptr, "",    "NTP disciplined the RTC", "" },
    { "ntp_offset_ms", SO(ntp_offset_ms), -1, SCALAR_I16, 0,  nullptr, "ms",  "NTP clock offset",        "" },
    { "sunrise_min",   SO(sunrise_min),   -1, SCALAR_U16, 0,  nullptr, "min", "Sunrise",                 "" },
    { "sunset_min",    SO(sunset_min),    -1, SCALAR_U16, 0,  nullptr, "min", "Sunset",                  "" },
};

const ScalarGroup g_telemetry_groups[] = {
    { "ac_supply",   g_ac_supply,   sizeof(g_ac_supply)   / sizeof(g_ac_supply[0])   },
    { "dc_supply",   g_dc_supply,   sizeof(g_dc_supply)   / sizeof(g_dc_supply[0])   },
    { "environment", g_environment, sizeof(g_environment) / sizeof(g_environment[0]) },
    { "irrigation",  g_irrigation,  sizeof(g_irrigation)  / sizeof(g_irrigation[0])  },
    { "ventilation", g_ventilation, sizeof(g_ventilation) / sizeof(g_ventilation[0]) },
    { "climate",     g_climate,     sizeof(g_climate)     / sizeof(g_climate[0])     },
    { "system",      g_system,      sizeof(g_system)      / sizeof(g_system[0])      },
};
const uint8_t g_telemetry_group_count =
    (uint8_t)(sizeof(g_telemetry_groups) / sizeof(g_telemetry_groups[0]));

// Home-screen readings: label + status variable. The HomePage lays these out in
// two columns, up to 5 left then 8 right (13 total). Inner ScalarField fields:
// name, out_offset, in_offset (-1), type, digits, str, unit.
const HomeField g_home_fields[] = {
    // left column (5)
    { "Temp",      { "", SO(ext_temp_c),   -1, SCALAR_F32, 1, nullptr, "C" } },
    { "Humidity",  { "", SO(ext_rh_pct),   -1, SCALAR_F32, 1, nullptr, "%" } },
    { "Wind",      { "", SO(wind_peak_kmh),-1, SCALAR_U16, 0, nullptr, "km/h" } },
    { "Water in",  { "", SO(water_in_l),   -1, SCALAR_U16, 0, nullptr, "L" } },
    { "Tray",      { "", SO(current_tray), -1, SCALAR_U8, 0,  nullptr, "" } },
    // right column (8)
    { "Grid",      { "", SO(grid_ac_v),  -1, SCALAR_F32, 1,  nullptr,         "V" } },
    { "DC bus",    { "", SO(dc_bus_v),   -1, SCALAR_F32, 1,  nullptr,         "V" } },
    { "Battery",   { "", SO(battery_v),  -1, SCALAR_F32, 1,  nullptr,         "V" } },
    { "UPS",       { "", SO(ups_status), -1, SCALAR_ENUM, 0, &UpsStatus::str, "" } },
    { "Network",   { "", SO(net_status), -1, SCALAR_ENUM, 0, &NetStatus::str, "" } },
    { "NTP sync",  { "", SO(ntp_synced), -1, SCALAR_U8, 0,   nullptr,         "" } },
    { "Cooling",   { "", SO(cooling_on), -1, SCALAR_U8, 0,   nullptr,         "" } },
    { "Heatpump",  { "", SO(heatpump_on),-1, SCALAR_U8, 0,   nullptr,         "" } },
};
const uint8_t g_home_field_count = (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));
#undef SO
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------------
// The declarative site descriptor: the one bundle GreenhouseMLB41 hands the
// firmware base. Name + the four generated tables (no site config parameters
// yet - the slot is null/0). The base owns every page and wires each table.
// ---------------------------------------------------------------------------
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    nullptr, 0,                                  // config parameters (none yet)
    g_site_commands,     (uint8_t)(sizeof(g_site_commands) / sizeof(g_site_commands[0])),
    g_telemetry_groups,  g_telemetry_group_count,
    g_home_fields,       g_home_field_count,
    g_alarms,            g_alarm_count,
};

}  // namespace

// ===========================================================================
// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)
// ===========================================================================

/**
 * @brief The gh_mlb41 greenhouse firmware.
 *
 * A thin binding of the generic base to this site's PLC payload + descriptor:
 * it declares no pages and overrides no hooks. The base owns every page
 * (home/shell/logs/alarms/config/monitor) and wires the descriptor's tables
 * (alarms, commands, status variables, config parameters) generically, so the
 * site is data (@ref g_site + the tables above), not code. A site that needs
 * custom per-beat logic or an extra page (e.g. re-adding a STATUS page) would
 * override the base's on_step() / register_pages() hooks here.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t> {
public:
    /** @brief Simulator / explicit-region ctor: hand the base the caller-owned
     *  PLC region and this site's descriptor. */
    GreenhouseMLB41(Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in,
                    Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out)
        : Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t>(
              shared_in, shared_out, g_site)
    {}

    /** @brief Hardware convenience: bind to the coprocessor's PLC region (via
     *  HAL), so the OPTA sketch is just `BotanikSite fw;` (no region casts). The
     *  simulator uses the explicit region constructor above. */
    GreenhouseMLB41()
        : GreenhouseMLB41(hal_input(), hal_output())
    {}
};

// Simulator-only mock PLC (body compiles only under TARGET_SIMULATOR;
// near-empty on OPTA).
#include "sites/gh_mlb41/sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITES_GH_MLB41_SITE_H
