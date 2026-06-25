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
 * Single source of truth for the greenhouse: the payload + GreenhouseMLB41
 * firmware live here; pages.h and alarms.h are pulled in below. Config
 * overrides stay in config.h (CMake includes it before the library defaults).
 * See firmware.h for the extension model.
 */

#include <cstddef>  // offsetof - the telemetry scalar table
#include <cstdint>
#include <cstdio>   // snprintf - the example site command

#include "libbotanik/firmware.h"
#include "libbotanik/shell/shell.h"   // ShellCommand - the site command table

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
    // Widest member first (keeps the rest aligned, no implicit padding).
    uint64_t alarms;             ///< active alarm conditions from the PLC
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
    uint8_t  _reserved[1];       ///< pad to a 4-byte multiple
};
static_assert(sizeof(PlcOutputData_t) % 4 == 0, "PlcOutputData_t must be 4-byte aligned");

/** @} */

// Pages and alarm table build on the payload above, so they're included after
// it (this header is the only includer of pages.h; no circular include).
#include "sites/gh_mlb41/alarms.h"
#include "sites/gh_mlb41/pages.h"

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

#if BOTANIK_FEATURE_MQTT
// Site MQTT telemetry: the live PLC payload grouped by domain. Each group is a
// small ScalarField[] published under "<prefix>/telemetry/<group>/<leaf>",
// alongside the library "system" set; MqttTelemetry reads + renders every field
// generically off the payload (no per-field code). A field is a leaf name, the
// payload offset, a ScalarType (read width/sign), and for fixed-point a divisor
// + decimals (raw / divisor, centi/milli units); a SCALAR_ENUM field renders its
// raw byte through a status str(). Leaves drop the group prefix (the topic
// already carries it: ac_supply/grid_v, not ac_supply/grid_ac_v).
using namespace Botanik::Network;

const ScalarField g_ac_supply[] = {
    { "grid_v",            offsetof(PlcOutputData_t, grid_ac_mv),         SCALAR_U32, 1000, 1, nullptr },
    { "grid_freq_hz",      offsetof(PlcOutputData_t, grid_freq_centi_hz), SCALAR_U16,  100, 2, nullptr },
    { "generator_v",       offsetof(PlcOutputData_t, generator_mv),       SCALAR_U32, 1000, 1, nullptr },
    { "generator_running", offsetof(PlcOutputData_t, generator_running),  SCALAR_U8,     0, 0, nullptr },
    { "ups",               offsetof(PlcOutputData_t, ups_status),         SCALAR_ENUM,   0, 0, &UpsStatus::str },
};

const ScalarField g_dc_supply[] = {
    { "bus_v",          offsetof(PlcOutputData_t, dc_bus_mv),   SCALAR_U32, 1000, 1, nullptr },
    { "battery_v",      offsetof(PlcOutputData_t, battery_mv),  SCALAR_U32, 1000, 1, nullptr },
    { "rectifier_a_ok", offsetof(PlcOutputData_t, dc_bus_a_ok), SCALAR_U8,     0, 0, nullptr },
    { "rectifier_b_ok", offsetof(PlcOutputData_t, dc_bus_b_ok), SCALAR_U8,     0, 0, nullptr },
};

const ScalarField g_environment[] = {
    { "ext_temp_c",    offsetof(PlcOutputData_t, ext_temp_c_centi),   SCALAR_I16, 100, 1, nullptr },
    { "floor_temp_c",  offsetof(PlcOutputData_t, floor_temp_c_centi), SCALAR_I16, 100, 1, nullptr },
    { "ceil_temp_c",   offsetof(PlcOutputData_t, ceil_temp_c_centi),  SCALAR_I16, 100, 1, nullptr },
    { "ext_rh_pct",    offsetof(PlcOutputData_t, ext_rh_centi_pct),   SCALAR_U16, 100, 1, nullptr },
    { "floor_rh_pct",  offsetof(PlcOutputData_t, floor_rh_centi_pct), SCALAR_U16, 100, 1, nullptr },
    { "ceil_rh_pct",   offsetof(PlcOutputData_t, ceil_rh_centi_pct),  SCALAR_U16, 100, 1, nullptr },
    { "wind_peak_kmh", offsetof(PlcOutputData_t, wind_peak_kmh),      SCALAR_U16,   0, 0, nullptr },
    { "rain_age_s",    offsetof(PlcOutputData_t, rain_age_s),         SCALAR_U32,   0, 0, nullptr },
    { "rain_detected", offsetof(PlcOutputData_t, rain_detected),      SCALAR_U8,    0, 0, nullptr },
};

const ScalarField g_irrigation[] = {
    { "water_in_l",    offsetof(PlcOutputData_t, water_in_l),    SCALAR_U16,  0, 0, nullptr },
    { "water_out_lpm", offsetof(PlcOutputData_t, water_out_lpm), SCALAR_U16,  0, 0, nullptr },
    { "current_tray",  offsetof(PlcOutputData_t, current_tray),  SCALAR_U8,   0, 0, nullptr },
    { "tray_status",   offsetof(PlcOutputData_t, tray_status),   SCALAR_ENUM, 0, 0, &TrayStatus::str },
};

const ScalarField g_ventilation[] = {
    { "window_a",    offsetof(PlcOutputData_t, window_a),    SCALAR_U8, 0, 0, nullptr },
    { "window_b",    offsetof(PlcOutputData_t, window_b),    SCALAR_U8, 0, 0, nullptr },
    { "window_c",    offsetof(PlcOutputData_t, window_c),    SCALAR_U8, 0, 0, nullptr },
    { "window_d",    offsetof(PlcOutputData_t, window_d),    SCALAR_U8, 0, 0, nullptr },
    { "blower_a",    offsetof(PlcOutputData_t, blower_a),    SCALAR_U8, 0, 0, nullptr },
    { "blower_b",    offsetof(PlcOutputData_t, blower_b),    SCALAR_U8, 0, 0, nullptr },
    { "blower_c",    offsetof(PlcOutputData_t, blower_c),    SCALAR_U8, 0, 0, nullptr },
    { "blower_d",    offsetof(PlcOutputData_t, blower_d),    SCALAR_U8, 0, 0, nullptr },
    { "extractor_a", offsetof(PlcOutputData_t, extractor_a), SCALAR_U8, 0, 0, nullptr },
    { "extractor_b", offsetof(PlcOutputData_t, extractor_b), SCALAR_U8, 0, 0, nullptr },
};

const ScalarField g_climate[] = {
    { "heatpump",       offsetof(PlcOutputData_t, heatpump_on),    SCALAR_U8, 0, 0, nullptr },
    { "heat_blower",    offsetof(PlcOutputData_t, heat_blower_on), SCALAR_U8, 0, 0, nullptr },
    { "cooling",        offsetof(PlcOutputData_t, cooling_on),     SCALAR_U8, 0, 0, nullptr },
    { "thermal_screen", offsetof(PlcOutputData_t, thermal_screen), SCALAR_U8, 0, 0, nullptr },
};

const ScalarField g_system[] = {
    { "net_status",    offsetof(PlcOutputData_t, net_status),    SCALAR_ENUM, 0, 0, &NetStatus::str },
    { "ntp_synced",    offsetof(PlcOutputData_t, ntp_synced),    SCALAR_U8,   0, 0, nullptr },
    { "ntp_offset_ms", offsetof(PlcOutputData_t, ntp_offset_ms), SCALAR_I16,  0, 0, nullptr },
    { "sunrise_min",   offsetof(PlcOutputData_t, sunrise_min),   SCALAR_U16,  0, 0, nullptr },
    { "sunset_min",    offsetof(PlcOutputData_t, sunset_min),    SCALAR_U16,  0, 0, nullptr },
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
#endif

}  // namespace

// ===========================================================================
// GreenhouseMLB41 - the gh_mlb41 site firmware (derives BotanikFirmware)
// ===========================================================================

/**
 * @brief The gh_mlb41 greenhouse firmware. `final` so the per-beat hook
 *        devirtualizes.
 *
 * Adds the site pages as members (built from the base's exposed components) and
 * customizes registration: its home page leads the tab order and sinks the log
 * stream, and it registers one site shell command.
 */
class GreenhouseMLB41 final : public Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t> {
    /** @name Site pages
     *  @{ */
    GreenhouseHomePage   _home_page;
    GreenhouseStatusPage _status_page;
    /** @} */

public:
    /** @brief Build the base around this site's alarm table, then the site
     *  pages. */
    GreenhouseMLB41(Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in,
                    Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out,
                    uint32_t console_baud, uint16_t cols = 80, uint16_t rows = 24)
        : Botanik::BotanikFirmware<PlcInputData_t, PlcOutputData_t>(
              shared_in, shared_out, g_alarms, g_alarm_count,
              console_baud, cols, rows),
          _home_page(terminal(), plc_output_data()),
          _status_page(terminal(), plc_output_data())
    {}

    /** @brief Hardware convenience: bind to the coprocessor's PLC region (via
     *  HAL) at the config-default baud, so the OPTA sketch is just
     *  `BotanikSite fw;` (no region casts). The simulator uses the explicit
     *  region constructor above. */
    explicit GreenhouseMLB41(uint16_t cols = 80, uint16_t rows = 24)
        : GreenhouseMLB41(hal_input(), hal_output(), Botanik::Config::CONSOLE_BAUD, cols, rows)
    {}

protected:
    /** @brief Tab order (add_page order = tab order): home leads as the boot
     *  page -> home/shell/logs/status/alarms. */
    void register_pages() override {
        terminal().add_page(&_home_page);     // page 0 (the boot page)
        terminal().add_page(&shell_page());
        terminal().add_page(&logs_page());
        terminal().add_page(&_status_page);
        terminal().add_page(&alarms_page());
    }

    /** @brief Register the site's "gh" command, reading the live PLC payload
     *  (ctx). */
    void register_commands() override {
        shell_page().set_commands(g_site_commands,
                                  sizeof(g_site_commands) / sizeof(g_site_commands[0]),
                                  (void*)plc_output_data());
    }

    /** @brief The home page also sinks the log stream (its bottom log-tail
     *  band). */
    void on_setup() override {
        logger().add_writer(&_home_page);
    }

#if BOTANIK_FEATURE_MQTT
    /** @brief Register the site telemetry scalars, read generically off the live
     *  PLC payload. Published after the library base set each cycle. */
    void register_telemetry() override {
        telemetry().set_site_scalar_groups(
            g_telemetry_groups,
            sizeof(g_telemetry_groups) / sizeof(g_telemetry_groups[0]),
            plc_output_data());
    }
#endif
};

// Simulator-only mock PLC (body compiles only under TARGET_SIMULATOR;
// near-empty on OPTA).
#include "sites/gh_mlb41/sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITES_GH_MLB41_SITE_H
