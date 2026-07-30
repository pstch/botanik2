#ifndef SITE_STATUS_H
#define SITE_STATUS_H

/**
 * @file status.h
 * @brief GENERATED - the gh_mlb41 status variables: the read-only telemetry
 *        table and the home-screen readings.
 *
 * Generated from the Arduino PLC IDE project. Regenerating overwrites this
 * file.
 *
 * One table, three consumers, no per-field code: the MONITOR page, the shell
 * `status` command, and MQTT telemetry all render these generically off the
 * live payload. Each group is published under
 * "<prefix>/telemetry/<group>/<leaf>", so leaves drop the group prefix
 * (ac_supply/grid_v, not ac_supply/grid_ac_v).
 *
 * A field is: name, out_offset, type, digits, str, unit, desc,
 * notes. Status variables are read-only, so in_offset is always -1 and the
 * edit metadata (default/min/max/capacity/str_kind/group) is left off. Real
 * values are SCALAR_F32 in native units with a per-field display precision;
 * a SCALAR_ENUM field renders its raw byte through a payload str().
 */

#include <cstddef>   // offsetof
#include <cstdint>

#include "botanik.h"

#include "data.h"

namespace Site {

using namespace Botanik;

// Trailing fields per row are optional (notes are left off on most rows), so
// silence the pedantic missing-field-initializer note for the tables.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#define SO(f) (int16_t)offsetof(SiteData_t, f)

const ScalarField g_ac_supply[] = {
    { "grid_v",            SO(grid_ac_v), SCALAR_F32, 1,  nullptr,         "V",  "Mains AC voltage",   "" },
    { "grid_freq_hz",      SO(grid_freq_hz), SCALAR_F32, 2,  nullptr,         "Hz", "Mains frequency",    "" },
    { "generator_v",       SO(generator_v), SCALAR_F32, 1,  nullptr,         "V",  "Generator voltage",  "" },
    { "generator_running", SO(generator_running), SCALAR_U8, 0,   nullptr,         "",   "Generator running",  "" },
    { "ups",               SO(ups_status), SCALAR_ENUM, 0, &UpsStatus::str, "",   "UPS state", "OFFLINE/ONLINE/BATTERY/FAULT" },
};

const ScalarField g_dc_supply[] = {
    { "bus_v",          SO(dc_bus_v), SCALAR_F32, 1, nullptr, "V", "DC bus voltage",       "" },
    { "battery_v",      SO(battery_v), SCALAR_F32, 1, nullptr, "V", "Battery bank voltage", "" },
    { "rectifier_a_ok", SO(dc_bus_a_ok), SCALAR_U8, 0,  nullptr, "",  "Rectifier A healthy",  "" },
    { "rectifier_b_ok", SO(dc_bus_b_ok), SCALAR_U8, 0,  nullptr, "",  "Rectifier B healthy",  "" },
};

const ScalarField g_environment[] = {
    { "ext_temp_c",    SO(ext_temp_c), SCALAR_F32, 1, nullptr, "C",    "Exterior temperature",      "" },
    { "floor_temp_c",  SO(floor_temp_c), SCALAR_F32, 1, nullptr, "C",    "Floor-level temperature",   "" },
    { "ceil_temp_c",   SO(ceil_temp_c), SCALAR_F32, 1, nullptr, "C",    "Ceiling-level temperature", "" },
    { "ext_rh_pct",    SO(ext_rh_pct), SCALAR_F32, 1, nullptr, "%",    "Exterior humidity",         "" },
    { "floor_rh_pct",  SO(floor_rh_pct), SCALAR_F32, 1, nullptr, "%",    "Floor humidity",            "" },
    { "ceil_rh_pct",   SO(ceil_rh_pct), SCALAR_F32, 1, nullptr, "%",    "Ceiling humidity",          "" },
    { "wind_peak_kmh", SO(wind_peak_kmh), SCALAR_U16, 0, nullptr, "km/h", "15-min peak wind",          "" },
    { "rain_age_s",    SO(rain_age_s), SCALAR_U32, 0, nullptr, "s",    "Time since last rain", "0 while raining" },
    { "rain_detected", SO(rain_detected), SCALAR_U8, 0,  nullptr, "",     "Rain sensor",               "" },
};

const ScalarField g_irrigation[] = {
    { "water_in_l",    SO(water_in_l), SCALAR_U16, 0,  nullptr, "L",     "Cumulative water input", "" },
    { "water_out_lpm", SO(water_out_lpm), SCALAR_U16, 0,  nullptr, "L/min", "Current drain flow",     "" },
    { "current_tray",  SO(current_tray), SCALAR_U8, 0,   nullptr, "",      "Active tray",            "0 = none" },
    { "tray_status",   SO(tray_status), SCALAR_ENUM, 0, &TrayStatus::str, "", "Tray cycle phase", "IDLE/FILLING/SOAKING/DRAINING" },
};

const ScalarField g_ventilation[] = {
    { "window_a",    SO(window_a), SCALAR_U8, 0, nullptr, "%", "Roof window A opening", "" },
    { "window_b",    SO(window_b), SCALAR_U8, 0, nullptr, "%", "Roof window B opening", "" },
    { "window_c",    SO(window_c), SCALAR_U8, 0, nullptr, "%", "Roof window C opening", "" },
    { "window_d",    SO(window_d), SCALAR_U8, 0, nullptr, "%", "Roof window D opening", "" },
    { "blower_a",    SO(blower_a), SCALAR_U8, 0, nullptr, "%", "Blower A level",        "" },
    { "blower_b",    SO(blower_b), SCALAR_U8, 0, nullptr, "%", "Blower B level",        "" },
    { "blower_c",    SO(blower_c), SCALAR_U8, 0, nullptr, "%", "Blower C level",        "" },
    { "blower_d",    SO(blower_d), SCALAR_U8, 0, nullptr, "%", "Blower D level",        "" },
    { "extractor_a", SO(extractor_a), SCALAR_U8, 0, nullptr, "%", "Air extractor A level", "" },
    { "extractor_b", SO(extractor_b), SCALAR_U8, 0, nullptr, "%", "Air extractor B level", "" },
};

const ScalarField g_climate[] = {
    { "heatpump",       SO(heatpump_on), SCALAR_U8, 0, nullptr, "",  "Heatpump",               "" },
    { "heat_blower",    SO(heat_blower_on), SCALAR_U8, 0, nullptr, "",  "Emergency heat blower",  "" },
    { "cooling",        SO(cooling_on), SCALAR_U8, 0, nullptr, "",  "Evaporative cooling",    "" },
    { "thermal_screen", SO(thermal_screen), SCALAR_U8, 0, nullptr, "%", "Thermal screen closure", "" },
};

const ScalarField g_system[] = {
    { "net_status",    SO(net_status), SCALAR_ENUM, 0, &NetStatus::str, "",    "Network link state", "DOWN/LINK/ONLINE" },
    { "ntp_synced",    SO(ntp_synced), SCALAR_U8, 0,   nullptr, "",    "NTP disciplined the RTC", "" },
    { "ntp_offset_ms", SO(ntp_offset_ms), SCALAR_I16, 0,  nullptr, "ms",  "NTP clock offset",        "" },
    { "sunrise_min",   SO(sunrise_min), SCALAR_U16, 0,  nullptr, "min", "Sunrise",                 "" },
    { "sunset_min",    SO(sunset_min), SCALAR_U16, 0,  nullptr, "min", "Sunset",                  "" },
};

const ScalarGroup g_status_groups[] = {
    { "ac_supply",   g_ac_supply,   sizeof(g_ac_supply)   / sizeof(g_ac_supply[0])   },
    { "dc_supply",   g_dc_supply,   sizeof(g_dc_supply)   / sizeof(g_dc_supply[0])   },
    { "environment", g_environment, sizeof(g_environment) / sizeof(g_environment[0]) },
    { "irrigation",  g_irrigation,  sizeof(g_irrigation)  / sizeof(g_irrigation[0])  },
    { "ventilation", g_ventilation, sizeof(g_ventilation) / sizeof(g_ventilation[0]) },
    { "climate",     g_climate,     sizeof(g_climate)     / sizeof(g_climate[0])     },
    { "system",      g_system,      sizeof(g_system)      / sizeof(g_system[0])      },
};
const uint8_t g_status_group_count =
    (uint8_t)(sizeof(g_status_groups) / sizeof(g_status_groups[0]));

// Home-screen readings: label + status variable. The HomePage lays these out in
// two columns, LEFT_MAX then RIGHT_MAX. Inner ScalarField fields: name,
// out_offset, type, digits, str, unit.
const HomeField g_home_fields[] = {
    // left column
    { "Temp",      { "", SO(ext_temp_c), SCALAR_F32, 1, nullptr, "C" } },
    { "Humidity",  { "", SO(ext_rh_pct), SCALAR_F32, 1, nullptr, "%" } },
    { "Wind",      { "", SO(wind_peak_kmh), SCALAR_U16, 0, nullptr, "km/h" } },
    { "Water in",  { "", SO(water_in_l), SCALAR_U16, 0, nullptr, "L" } },
    { "Tray",      { "", SO(current_tray), SCALAR_U8, 0,  nullptr, "" } },
    // right column
    { "Grid",      { "", SO(grid_ac_v), SCALAR_F32, 1,  nullptr,         "V" } },
    { "DC bus",    { "", SO(dc_bus_v), SCALAR_F32, 1,  nullptr,         "V" } },
    { "Battery",   { "", SO(battery_v), SCALAR_F32, 1,  nullptr,         "V" } },
    { "UPS",       { "", SO(ups_status), SCALAR_ENUM, 0, &UpsStatus::str, "" } },
    { "Network",   { "", SO(net_status), SCALAR_ENUM, 0, &NetStatus::str, "" } },
    { "NTP sync",  { "", SO(ntp_synced), SCALAR_U8, 0,   nullptr,         "" } },
    { "Cooling",   { "", SO(cooling_on), SCALAR_U8, 0,   nullptr,         "" } },
    { "Heatpump",  { "", SO(heatpump_on), SCALAR_U8, 0,   nullptr,         "" } },
};
const uint8_t g_home_field_count = (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));

#undef SO
#pragma GCC diagnostic pop

// Capacity contract: the home screen silently ignores fields past its two
// columns, so pin the count at compile time instead.
static_assert(sizeof(g_home_fields) / sizeof(g_home_fields[0]) <= Config::HOME_FIELDS_MAX,
              "too many home fields: the HomePage would silently drop the tail");

}  // namespace Site

#endif // SITE_STATUS_H
