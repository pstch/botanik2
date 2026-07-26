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
    // Widest members first (keeps the rest aligned, no implicit padding). Each
    // alarm is an individual boolean field near the end (AlarmReader gathers
    // them into the raw bitset via g_alarms offsets), not a packed word.
    // Power (voltages in millivolts)
    uint32_t grid_ac_mv;         ///< mains AC RMS voltage, millivolts
    uint32_t dc_bus_mv;          ///< DC system bus voltage, millivolts
    uint32_t battery_mv;         ///< battery bank voltage, millivolts
    uint32_t generator_mv;       ///< generator output voltage, millivolts
    uint16_t grid_freq_centi_hz; ///< mains frequency, 0.01 Hz
    // Climate (filtered sensor data, one reading per height zone)
    int16_t  ext_temp_c_centi;   ///< exterior air temp, 0.01 degC
    int16_t  floor_temp_c_centi; ///< floor-level temp, 0.01 degC
    int16_t  ceil_temp_c_centi;  ///< ceiling-level temp, 0.01 degC
    uint16_t ext_rh_centi_pct;   ///< exterior relative humidity, 0.01 %
    uint16_t floor_rh_centi_pct; ///< floor relative humidity, 0.01 %
    uint16_t ceil_rh_centi_pct;  ///< ceiling relative humidity, 0.01 %
    uint16_t wind_peak_kmh;      ///< 15-minute peak wind speed, km/h
    /** Seconds since rain was last detected (uint32: well past the uint16 ~18h
     *  ceiling, up to ~136 years). */
    uint32_t rain_age_s;
    // Irrigation
    uint16_t water_in_l;         ///< cumulative water input, liters
    uint16_t water_out_lpm;      ///< current drain flow, liters/minute
    // System / environment extras
    uint16_t sunrise_min;        ///< sunrise, minutes since local midnight
    uint16_t sunset_min;         ///< sunset, minutes since local midnight
    int16_t  ntp_offset_ms;      ///< NTP clock offset, milliseconds (signed)
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
using namespace Botanik::Network;

// Trailing fields per row: str, description, unit, notes. Notes are optional
// (left off on most rows). Silence the pedantic missing-field-initializer note
// (str/notes unset on many rows).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

const ScalarField g_ac_supply[] = {
    { "grid_v",            offsetof(PlcOutputData_t, grid_ac_mv),         SCALAR_U32, 1000, 1, nullptr, "Mains AC voltage",   "V"  },
    { "grid_freq_hz",      offsetof(PlcOutputData_t, grid_freq_centi_hz), SCALAR_U16,  100, 2, nullptr, "Mains frequency",    "Hz" },
    { "generator_v",       offsetof(PlcOutputData_t, generator_mv),       SCALAR_U32, 1000, 1, nullptr, "Generator voltage",  "V"  },
    { "generator_running", offsetof(PlcOutputData_t, generator_running),  SCALAR_U8,     0, 0, nullptr, "Generator running",  ""   },
    { "ups",               offsetof(PlcOutputData_t, ups_status),         SCALAR_ENUM,   0, 0, &UpsStatus::str, "UPS state",   "",  "OFFLINE/ONLINE/BATTERY/FAULT" },
};

const ScalarField g_dc_supply[] = {
    { "bus_v",          offsetof(PlcOutputData_t, dc_bus_mv),   SCALAR_U32, 1000, 1, nullptr, "DC bus voltage",      "V" },
    { "battery_v",      offsetof(PlcOutputData_t, battery_mv),  SCALAR_U32, 1000, 1, nullptr, "Battery bank voltage","V" },
    { "rectifier_a_ok", offsetof(PlcOutputData_t, dc_bus_a_ok), SCALAR_U8,     0, 0, nullptr, "Rectifier A healthy", ""  },
    { "rectifier_b_ok", offsetof(PlcOutputData_t, dc_bus_b_ok), SCALAR_U8,     0, 0, nullptr, "Rectifier B healthy", ""  },
};

const ScalarField g_environment[] = {
    { "ext_temp_c",    offsetof(PlcOutputData_t, ext_temp_c_centi),   SCALAR_I16, 100, 1, nullptr, "Exterior temperature", "C"    },
    { "floor_temp_c",  offsetof(PlcOutputData_t, floor_temp_c_centi), SCALAR_I16, 100, 1, nullptr, "Floor-level temperature", "C" },
    { "ceil_temp_c",   offsetof(PlcOutputData_t, ceil_temp_c_centi),  SCALAR_I16, 100, 1, nullptr, "Ceiling-level temperature", "C" },
    { "ext_rh_pct",    offsetof(PlcOutputData_t, ext_rh_centi_pct),   SCALAR_U16, 100, 1, nullptr, "Exterior humidity",    "%"    },
    { "floor_rh_pct",  offsetof(PlcOutputData_t, floor_rh_centi_pct), SCALAR_U16, 100, 1, nullptr, "Floor humidity",       "%"    },
    { "ceil_rh_pct",   offsetof(PlcOutputData_t, ceil_rh_centi_pct),  SCALAR_U16, 100, 1, nullptr, "Ceiling humidity",     "%"    },
    { "wind_peak_kmh", offsetof(PlcOutputData_t, wind_peak_kmh),      SCALAR_U16,   0, 0, nullptr, "15-min peak wind",     "km/h" },
    { "rain_age_s",    offsetof(PlcOutputData_t, rain_age_s),         SCALAR_U32,   0, 0, nullptr, "Time since last rain", "s",   "0 while raining" },
    { "rain_detected", offsetof(PlcOutputData_t, rain_detected),      SCALAR_U8,    0, 0, nullptr, "Rain sensor",          ""     },
};

const ScalarField g_irrigation[] = {
    { "water_in_l",    offsetof(PlcOutputData_t, water_in_l),    SCALAR_U16,  0, 0, nullptr, "Cumulative water input", "L"     },
    { "water_out_lpm", offsetof(PlcOutputData_t, water_out_lpm), SCALAR_U16,  0, 0, nullptr, "Current drain flow",     "L/min" },
    { "current_tray",  offsetof(PlcOutputData_t, current_tray),  SCALAR_U8,   0, 0, nullptr, "Active tray",            "",     "0 = none" },
    { "tray_status",   offsetof(PlcOutputData_t, tray_status),   SCALAR_ENUM, 0, 0, &TrayStatus::str, "Tray cycle phase", "", "IDLE/FILLING/SOAKING/DRAINING" },
};

const ScalarField g_ventilation[] = {
    { "window_a",    offsetof(PlcOutputData_t, window_a),    SCALAR_U8, 0, 0, nullptr, "Roof window A opening", "%" },
    { "window_b",    offsetof(PlcOutputData_t, window_b),    SCALAR_U8, 0, 0, nullptr, "Roof window B opening", "%" },
    { "window_c",    offsetof(PlcOutputData_t, window_c),    SCALAR_U8, 0, 0, nullptr, "Roof window C opening", "%" },
    { "window_d",    offsetof(PlcOutputData_t, window_d),    SCALAR_U8, 0, 0, nullptr, "Roof window D opening", "%" },
    { "blower_a",    offsetof(PlcOutputData_t, blower_a),    SCALAR_U8, 0, 0, nullptr, "Blower A level",        "%" },
    { "blower_b",    offsetof(PlcOutputData_t, blower_b),    SCALAR_U8, 0, 0, nullptr, "Blower B level",        "%" },
    { "blower_c",    offsetof(PlcOutputData_t, blower_c),    SCALAR_U8, 0, 0, nullptr, "Blower C level",        "%" },
    { "blower_d",    offsetof(PlcOutputData_t, blower_d),    SCALAR_U8, 0, 0, nullptr, "Blower D level",        "%" },
    { "extractor_a", offsetof(PlcOutputData_t, extractor_a), SCALAR_U8, 0, 0, nullptr, "Air extractor A level", "%" },
    { "extractor_b", offsetof(PlcOutputData_t, extractor_b), SCALAR_U8, 0, 0, nullptr, "Air extractor B level", "%" },
};

const ScalarField g_climate[] = {
    { "heatpump",       offsetof(PlcOutputData_t, heatpump_on),    SCALAR_U8, 0, 0, nullptr, "Heatpump",              ""  },
    { "heat_blower",    offsetof(PlcOutputData_t, heat_blower_on), SCALAR_U8, 0, 0, nullptr, "Emergency heat blower", ""  },
    { "cooling",        offsetof(PlcOutputData_t, cooling_on),     SCALAR_U8, 0, 0, nullptr, "Evaporative cooling",   ""  },
    { "thermal_screen", offsetof(PlcOutputData_t, thermal_screen), SCALAR_U8, 0, 0, nullptr, "Thermal screen closure","%" },
};

const ScalarField g_system[] = {
    { "net_status",    offsetof(PlcOutputData_t, net_status),    SCALAR_ENUM, 0, 0, &NetStatus::str, "Network link state", "", "DOWN/LINK/ONLINE" },
    { "ntp_synced",    offsetof(PlcOutputData_t, ntp_synced),    SCALAR_U8,   0, 0, nullptr, "NTP disciplined the RTC", ""    },
    { "ntp_offset_ms", offsetof(PlcOutputData_t, ntp_offset_ms), SCALAR_I16,  0, 0, nullptr, "NTP clock offset",        "ms"  },
    { "sunrise_min",   offsetof(PlcOutputData_t, sunrise_min),   SCALAR_U16,  0, 0, nullptr, "Sunrise",                 "min" },
    { "sunset_min",    offsetof(PlcOutputData_t, sunset_min),    SCALAR_U16,  0, 0, nullptr, "Sunset",                  "min" },
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

// Home-screen readings: label + status variable (offset/type/scaling/unit). The
// HomePage lays these out two columns, up to 5 left then 8 right (13 total).
const HomeField g_home_fields[] = {
    // left column (5)
    { "Temp",      { "", offsetof(PlcOutputData_t, ext_temp_c_centi), SCALAR_I16, 100, 1, nullptr, nullptr, "C" } },
    { "Humidity",  { "", offsetof(PlcOutputData_t, ext_rh_centi_pct), SCALAR_U16, 100, 1, nullptr, nullptr, "%" } },
    { "Wind",      { "", offsetof(PlcOutputData_t, wind_peak_kmh),    SCALAR_U16,   0, 0, nullptr, nullptr, "km/h" } },
    { "Water in",  { "", offsetof(PlcOutputData_t, water_in_l),       SCALAR_U16,   0, 0, nullptr, nullptr, "L" } },
    { "Tray",      { "", offsetof(PlcOutputData_t, current_tray),     SCALAR_U8,    0, 0, nullptr, nullptr, "" } },
    // right column (8)
    { "Grid",      { "", offsetof(PlcOutputData_t, grid_ac_mv),  SCALAR_U32, 1000, 1, nullptr, nullptr, "V" } },
    { "DC bus",    { "", offsetof(PlcOutputData_t, dc_bus_mv),   SCALAR_U32, 1000, 1, nullptr, nullptr, "V" } },
    { "Battery",   { "", offsetof(PlcOutputData_t, battery_mv),  SCALAR_U32, 1000, 1, nullptr, nullptr, "V" } },
    { "UPS",       { "", offsetof(PlcOutputData_t, ups_status),  SCALAR_ENUM,   0, 0, &UpsStatus::str, nullptr, "" } },
    { "Network",   { "", offsetof(PlcOutputData_t, net_status),  SCALAR_ENUM,   0, 0, &NetStatus::str, nullptr, "" } },
    { "NTP sync",  { "", offsetof(PlcOutputData_t, ntp_synced),  SCALAR_U8,     0, 0, nullptr, nullptr, "" } },
    { "Cooling",   { "", offsetof(PlcOutputData_t, cooling_on),  SCALAR_U8,     0, 0, nullptr, nullptr, "" } },
    { "Heatpump",  { "", offsetof(PlcOutputData_t, heatpump_on), SCALAR_U8,     0, 0, nullptr, nullptr, "" } },
};
const uint8_t g_home_field_count = (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));
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
