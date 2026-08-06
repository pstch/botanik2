#ifndef SITE_VARS_H
#define SITE_VARS_H

/**
 * @file vars.h
 * @brief GENERATED - every gh_mlb41 variable, in one table, plus the home-screen
 *        readings.
 *
 * Generated from the site's Modbus device map. Regenerating overwrites this
 * file.
 *
 * Bus: RS485, ModbusRTU master, 38400 baud, none/8/1, on COM2.
 * 8 field devices, 71 mapped signals, 20 free channels.
 * (70 in the published device map, plus MTR-2 object 2 - see below.)
 *
 *   SRR/TAC/MTR-1  Eastron SDM630    addr 5   37 readings
 *   SRR/TAC/MTR-2  Lettel MCT1-140   addr 6    6 readings
 *   SRR/TDC/AI-1   Waveshare 25767   addr 11   8 free  (unmapped)
 *   SRR/TDC/AO-1   Waveshare 26211   addr 16   8 free  (unmapped)
 *   SRR/TDC/RO-1   Waveshare 26517   addr 31   6 coils (+2 free)
 *   SRR/TDC/RO-2   Waveshare 26517   addr 32   6 coils (+2 free)
 *   SRR/TDC/RO-3   Waveshare 26517   addr 33   8 coils
 *   SRR/TDC/RO-4   Waveshare 26517   addr 34   8 coils
 *
 * Everything is XP_MODBUS today: the AI-1 / AO-1 modules are wired but their
 * channels are unmapped, so the site has no XP_LOCAL variables yet, and
 * a later mapping is purely additive - one more row, nothing else to touch.
 *
 * @warning Binding::reg holds the FIELD-DEVICE address: the coil number on the
 * relay boards, and the device-profile OBJECT INDEX on the meters. The raw
 * holding register is fixed by the device template, so the RTU master must
 * translate object index -> register per device model. It is not the register
 * column of the device map - that one is the Opta's own Modbus *slave* address,
 * which belongs in the slave map, not here.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

/** @brief Text-parameter capacity, including the NUL. */
static const uint16_t STR_CAP = 64;
/** @brief 8 text parameters x STR_CAP. */
static const uint16_t STR_POOL_BYTES = 8 * STR_CAP;


/** @name Analog calibration - the two-point span and the believable band
 *
 *  One block per channel here, deliberately. A block may be SHARED - point two
 *  rows at the same one and they share its calibration - but sharing means
 *  sharing the PARAMETERS, so the two can never be trimmed apart. PS1, PS2 and
 *  the UPS battery are the same divider DESIGN and still get a block each,
 *  because three physical dividers have three tolerances. Share a block when
 *  two readings must agree by construction, not when they merely look alike.
 *
 *  Every field here is R(), an operator-editable parameter: these are bench
 *  measurements, and correcting one should not need a firmware build. K() is
 *  there for the fields a site does want pinned.
 *  @{ */

/** @brief DC PS1: 4-6 V across the divider is a 20-30 V bus. */
const AnalogCal CAL_PWR_DC1 = {
    R("pwr_dc1_rawMin_mV", 4000.0f), R("pwr_dc1_rawMax_mV", 6000.0f),
    R("pwr_dc1_engMin_V", 20.0f), R("pwr_dc1_engMax_V", 30.0f),
    R("pwr_dc1_min", 20.0f),      R("pwr_dc1_max", 30.0f),
};

/** @brief DC PS2: same divider design as PS1, its own trim. */
const AnalogCal CAL_PWR_DC2 = {
    R("pwr_dc2_rawMin_mV", 4000.0f), R("pwr_dc2_rawMax_mV", 6000.0f),
    R("pwr_dc2_engMin_V", 20.0f), R("pwr_dc2_engMax_V", 30.0f),
    R("pwr_dc2_min", 20.0f),      R("pwr_dc2_max", 30.0f),
};

/** @brief UPS battery: same divider design again, its own trim. */
const AnalogCal CAL_PWR_UPS_BAT = {
    R("pwr_upsBat_rawMin_mV", 4000.0f), R("pwr_upsBat_rawMax_mV", 6000.0f),
    R("pwr_upsBat_engMin_V", 20.0f), R("pwr_upsBat_engMax_V", 30.0f),
    R("pwr_upsBat_min", 20.0f),      R("pwr_upsBat_max", 30.0f),
};

/** @brief Generator start battery: a different divider (5-7.5 V for the same band). */
const AnalogCal CAL_PWR_GEN_BAT = {
    R("pwr_genBat_rawMin_mV", 5000.0f), R("pwr_genBat_rawMax_mV", 7500.0f),
    R("pwr_genBat_engMin_V", 20.0f), R("pwr_genBat_engMax_V", 30.0f),
    R("pwr_genBat_min", 20.0f),      R("pwr_genBat_max", 30.0f),
};

/** @brief Output flow meter: a 4-20 mA loop reported in uA, 0-50 L/min. */
const AnalogCal CAL_IRR_OUTPUT = {
    R("irr_out_rawMin_uA", 0.0f), R("irr_out_rawMax_uA", 10000.0f),
    R("irr_out_engMin_Lpm", 0.0f), R("irr_out_engMax_Lpm", 50.0f),
    R("irr_out_min", 0.0f),      R("irr_out_max", 50.0f),
};

/** @brief Intractor speed command: 0-10 V out for a 0-1 demand. An OUTPUT, so the
 *  conversion runs the other way (AnalogValues::to_raw). */
const AnalogCal CAL_INTRACTOR = {
    R("cli_intr_rawMin_mV", 0.0f), R("cli_intr_rawMax_mV", 10000.0f),
    R("cli_intr_engMin", 0.0f), R("cli_intr_engMax", 1.0f),
    R("cli_intr_min", 0.0f),      R("cli_intr_max", 1.0f),
};

/** @brief Every calibration block, so the site test can walk them: each name
 *  must resolve, and a channel's two raw endpoints must agree on their unit.
 *  Until the AI-1 / AO-1 channel maps land there is no row pointing at these,
 *  and this is what keeps them honest in the meantime. */
const AnalogCal* const g_analog_cals[] = {
    &CAL_PWR_DC1, &CAL_PWR_DC2, &CAL_PWR_UPS_BAT, &CAL_PWR_GEN_BAT,
    &CAL_IRR_OUTPUT, &CAL_INTRACTOR,
};
const uint8_t g_analog_cal_count =
    (uint8_t)(sizeof(g_analog_cals) / sizeof(g_analog_cals[0]));

/** @} */

/** @name Text-parameter defaults
 *
 *  Named rather than inlined: a hostname or an IP literal in the middle of a row
 *  is the widest cell in the block and says nothing a name would not.
 *  @{ */

#define DEF_NET_IP       "192.168.1.42"
#define DEF_NET_MASK     "255.255.255.0"
#define DEF_NET_DNS1     "8.8.8.8"
#define DEF_NET_DNS2     "4.4.4.4"
#define DEF_NET_GW       "192.168.1.1"
#define DEF_NTP_SRV1     "0.fr.pool.ntp.org"
#define DEF_NTP_SRV2     "1.fr.pool.ntp.org"
#define DEF_MQTT_HOST    "192.168.1.43"

/** @} */

/** @brief Every variable this site declares, in one table.
 *
 *  Every row is a call to one of the scope factories in vars/declare.h - A, P,
 *  G, SV, MI, AI, DI, MO, AO, DO, RO. Each takes only the arguments its scope
 *  can use, so a relay with a plausibility band or an alarm with a register
 *  does not compile. The row's `scope` follows from which factory built it,
 *  which is what the tables filter on.
 *
 *  Alarms live here too, and only here: an A() row carries the boolean that
 *  raises the alarm AND the policy the AlarmController runs it on, and the model
 *  finds them by scanning for SCOPE_A. There is no second alarm table whose row
 *  order has to be kept in step with anything.
 *
 *  Plain `const`, so each including TU gets its own copy; only main.cpp does,
 *  today. */
const Variable g_vars[] = {


/** @name Parameters - persisted and editable; the PARAMS page and `set` write these
 *  @{ */

    //name                    type    default  min     max      dgt unit    reg  description                            notes
    P("net_serial",           T_U32,  1,       0,      9999,    0,  "",     1,   "Network config commit",               "bump to apply staged net_* edits"),
    P("net_dhcp",             T_BOOL, 0,       0,      1,       0,  "",     2,   "Enable DHCP IP configuration",        "ignores the static ip/mask/gw rows"),
    P("ntp_serial",           T_U32,  1,       0,      9999,    0,  "",     8,   "NTP config commit",                   ""),
    P("ntp_enable",           T_BOOL, 1,       0,      1,       0,  "",     9,   "Enable NTP clock sync",               ""),
    P("ntp_poll_sec",         T_U32,  3600,    10,     86400,   0,  "sec",  10,  "NTP poll interval, in seconds",       ""),
    P("mqtt_serial",          T_U32,  1,       0,      9999,    0,  "",     13,  "MQTT config commit",                  ""),
    P("mqtt_enable",          T_BOOL, 1,       0,      1,       0,  "",     14,  "Enable MQTT",                         ""),
    P("mqtt_port",            T_U16,  58443,   0,      65535,   0,  "",     16,  "MQTT broker port",                    ""),
    P("mqtt_ka_sec",          T_U16,  20,      10,     1800,    0,  "sec",  17,  "MQTT keep-alive interval",            "also the dead-broker detection"),
    P("geo_serial",           T_U32,  1,       0,      9999,    0,  "",     18,  "Geo config commit",                   ""),
    P("geo_lat_deg",          T_F32,  48.07,   -90.0,  90.0,    2,  "deg",  19,  "Geographical latitude, in degrees",   "per site location"),
    P("geo_lon_deg",          T_F32,  1.32,    -180.0, 180.0,   2,  "deg",  20,  "Geographical longitude, in degrees",  "per site location"),
    P("cli_coldBankOfs_degC", T_F32,  6.0,     0.0,    15.0,    1,  "degC", 21,  "How much cooler to run before a hot", ""),
    P("cli_heatBankOfs_degC", T_F32,  7.0,     0.0,    15.0,    1,  "degC", 22,  "How much warmer to run before cold",  ""),
    P("cli_heatBankTrg_degC", T_F32,  10.0,    6.0,    18.0,    1,  "degC", 23,  "Yesterday's low below this: bank",    ""),
    P("cli_coldBankTrg_degC", T_F32,  24.0,    12.0,   40.0,    1,  "degC", 24,  "Yesterday's high above this:",        ""),
    P("cli_coolOn_degC",      T_F32,  20.0,    10.0,   30.0,    1,  "degC", 25,  "Base cooling target temperature",     "every cooling stage keys off this"),
    P("cli_coolKp",           T_F32,  0.08,    0.0,    1.0,     2,  "",     26,  "Cooling PID proportional factor",     "Leave as set"),
    P("cli_coolTi_sec",       T_F32,  200.0,   0.0,    3600.0,  0,  "sec",  27,  "Cooling PID integral factor",         "Leave as set"),
    P("cli_coolTd_sec",       T_F32,  0.0,     0.0,    10.0,    0,  "sec",  28,  "Cooling PID derivative factor",       "leave at zero"),
    P("cli_ventMargin_degC",  T_F32,  1.0,     0.0,    15.0,    1,  "degC", 29,  "Intake air must be this much cooler", ""),
    P("cli_skirtAvail_degC",  T_F32,  4.0,     0.0,    10.0,    1,  "degC", 30,  "Air must be this dry for the pad to", ""),
    P("cli_thermalScreenDem", T_F32,  -0.2,    -1.0,   0.0,     2,  "",     31,  "Cooling effort that deploys the",     "negative demand = heating"),
    P("cli_groundHeatDem",    T_F32,  -0.1,    -1.0,   0.0,     2,  "",     32,  "Cooling effort drawing air from",     "negative demand = heating"),
    P("cli_winOpenDem",       T_F32,  0.1,     0.0,    1.0,     2,  "",     33,  "Cooling effort that opens the",       ""),
    P("cli_intakeStartDem",   T_F32,  0.3,     0.0,    1.0,     2,  "",     34,  "Cooling effort where intake fans",    ""),
    P("cli_winFullDem",       T_F32,  0.5,     0.0,    1.0,     2,  "",     35,  "Cooling effort at fully open",        ""),
    P("cli_skirtEnableDem",   T_F32,  0.7,     0.0,    1.0,     2,  "",     36,  "Cooling effort that starts the wet",  ""),
    P("cli_fanDemLow",        T_F32,  0.2,     0.0,    1.0,     2,  "",     37,  "Cooling effort for fan speed 1",      ""),
    P("cli_fanDemMed",        T_F32,  0.4,     0.0,    1.0,     2,  "",     38,  "Cooling effort for fan speed 2",      "> cli_fanDemLow"),
    P("cli_fanDemHigh",       T_F32,  0.8,     0.0,    1.0,     2,  "",     39,  "Cooling effort for fan speed 3",      "> cli_fanDemMed"),
    P("cli_intakeFullDem",    T_F32,  0.9,     0.0,    1.0,     2,  "",     40,  "Cooling effort at full speed intake", ""),
    P("cli_dTStratLo_degC",   T_F32,  2.0,     0.0,    10.0,    1,  "degC", 41,  "Stratification for fan speed 1",      ""),
    P("cli_dTStratMed_degC",  T_F32,  4.0,     0.0,    10.0,    1,  "degC", 42,  "Stratification for fan speed 2",      ""),
    P("cli_dTStratHi_degC",   T_F32,  6.0,     0.0,    10.0,    1,  "degC", 43,  "Stratification for fan speed 3",      ""),
    P("cli_dTStratHyst_degC", T_F32,  2.0,     0.5,    5.0,     1,  "degC", 44,  "Gap change needed to switch fan",     ""),
    P("cli_rot_dpLow_degC",   T_F32,  3.0,     0.0,    10.0,    1,  "degC", 45,  "Dryness margin: dehumidify fully",    ""),
    P("cli_rot_dpHigh_degC",  T_F32,  4.5,     0.0,    10.0,    1,  "degC", 46,  "Dryness margin: dehumidify starts",   "> cli_rot_dpLow_degC"),
    P("cli_humidLo_degC",     T_F32,  18.0,    0.0,    30.0,    1,  "degC", 47,  "Dryness margin where humidifying",    "> cli_rot_dpHigh_degC"),
    P("cli_humidHi_degC",     T_F32,  28.0,    0.0,    40.0,    1,  "degC", 48,  "Dryness margin: humidify fully",      "> cli_humidLo_degC"),
    P("cli_humidSkirtDem",    T_F32,  0.5,     0.0,    1.0,     2,  "",     49,  "Dryness that humidifies via skirt",   ""),
    P("cli_rhCap",            T_F32,  0.6,     0.0,    1.0,     2,  "",     50,  "Humidity that also starts rot",       ""),
    P("cli_dpVentMgn_degC",   T_F32,  1.0,     0.0,    5.0,     1,  "degC", 51,  "Outside air must be this much drier", ""),
    P("cli_tooCold_degC",     T_F32,  4.0,     0.0,    10.0,    1,  "degC", 52,  "Alarm: greenhouse is dangerously",    "match the gas-heater thermostat"),
    P("cli_tooHot_degC",      T_F32,  35.0,    20.0,   50.0,    1,  "degC", 53,  "Alarm: greenhouse is dangerously",    ""),
    P("cli_rotFlt_degC",      T_F32,  1.0,     0.0,    5.0,     1,  "degC", 54,  "Alarm: greenhouse is dangerously",    "match the dehumidifier setting"),
    P("cli_intr_rawMin_mV",   T_F32,  0.0,     0.0,    24000.0, 0,  "mV",   55,  "Intractor raw low-end value (zero",   "per intractor datasheet"),
    P("cli_intr_rawMax_mV",   T_F32,  10000.0, 0.0,    24000.0, 0,  "mV",   56,  "Intractor raw high-end value (max",   "per intractor datasheet"),
    P("cli_intr_engMin",      T_F32,  0.0,     0.0,    1.0,     2,  "",     57,  "Intractor speed at the low end",      "per intractor datasheet"),
    P("cli_intr_engMax",      T_F32,  1.0,     0.0,    1.0,     2,  "",     58,  "Intractor speed at the high end",     ""),
    P("cli_intr_min",         T_F32,  0.0,     0.0,    1.0,     2,  "",     59,  "Lowest believable intractor speed",   "under this: a dead sensor"),
    P("cli_intr_max",         T_F32,  1.0,     0.0,    1.0,     2,  "",     60,  "Highest believable intractor speed",  "over this: a dead sensor"),
    P("cli_windLockout_mps",  T_F32,  5.0,     0.0,    50.0,    1,  "mps",  61,  "Wind speed that forces windows shut", ""),
    P("cli_windowTravel_sec", T_U16,  15,      1,      60,      0,  "sec",  62,  "Time to fully open/close a window",   "per actuator datasheet"),
    P("cli_windowTmo_sec",    T_U16,  20,      1,      90,      0,  "sec",  63,  "Safe time to fully open/close a",     "per actuator datasheet"),
    P("cli_screenTravel_sec", T_U16,  60,      1,      300,     0,  "sec",  64,  "Time to deploy/remove the screen",    "per actuator datasheet"),
    P("cli_screenTmo_sec",    T_U16,  80,      1,      400,     0,  "sec",  65,  "Safe time to deploy/remove the",      "per actuator datasheet"),
    P("cli_rainLockout_sec",  T_U16,  900,     0,      3600,    0,  "sec",  66,  "Keep windows shut this long after",   ""),
    P("cli_skirtPer_sec",     T_U16,  60,      10,     600,     0,  "sec",  67,  "Skirt valve modulation period",       ""),
    P("cli_skirtMinOn_sec",   T_U16,  10,      1,      600,     0,  "sec",  68,  "Shortest skirt valve pulse",          "<= cli_skirtValvePeriod_sec"),
    P("cli_screenDep_degC",   T_F32,  13.0,    0.0,    50.0,    1,  "degC", 69,  "Night temperature that deploys",      ""),
    P("sol_coldBankRise_min", T_I16,  -30,     -180,   60,      0,  "min",  70,  "Cold banking starts, from sunrise",   ""),
    P("sol_breezeRise_min",   T_I16,  30,      -60,    180,     0,  "min",  71,  "Dawn breeze starts, from sunrise",    ""),
    P("sol_dayRise_min",      T_I16,  60,      0,      180,     0,  "min",  72,  "Day operation starts, from sunrise",  ""),
    P("sol_heatBankSet_min",  T_I16,  -180,    -300,   0,       0,  "min",  73,  "Heat banking starts, from sunset",    ""),
    P("sol_nightSet_min",     T_I16,  30,      0,      180,     0,  "min",  74,  "Night operation starts, from sunset", ""),
    P("irr_forceNow",         T_BOOL, 0,       0,      1,       0,  "",     75,  "Run a watering cycle immediately,",   ""),
    P("irr_liters_tray1",     T_U16,  150,     0,      500,     0,  "L",    76,  "Litres to flood this tray in summer", ""),
    P("irr_days_tray1",       T_U8,   14,      1,      60,      0,  "days", 77,  "Days between waterings in summer",    ""),
    P("irr_last_tray1",       T_U32,  0,       0,      100000,  0,  "days", 78,  "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray1",    T_BOOL, 0,       0,      1,       0,  "",     79,  "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray1",   T_BOOL, 0,       0,      1,       0,  "",     80,  "Force skipping this tray next cycle", ""),
    P("irr_liters_tray2",     T_U16,  150,     0,      500,     0,  "L",    81,  "Litres to flood this tray in summer", ""),
    P("irr_days_tray2",       T_U8,   14,      1,      60,      0,  "days", 82,  "Days between waterings in summer",    ""),
    P("irr_last_tray2",       T_U32,  0,       0,      100000,  0,  "days", 83,  "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray2",    T_BOOL, 0,       0,      1,       0,  "",     84,  "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray2",   T_BOOL, 0,       0,      1,       0,  "",     85,  "Force skipping this tray next cycle", ""),
    P("irr_liters_tray3",     T_U16,  150,     0,      500,     0,  "L",    86,  "Litres to flood this tray in summer", ""),
    P("irr_days_tray3",       T_U8,   14,      1,      60,      0,  "days", 87,  "Days between waterings in summer",    ""),
    P("irr_last_tray3",       T_U32,  0,       0,      100000,  0,  "days", 88,  "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray3",    T_BOOL, 0,       0,      1,       0,  "",     89,  "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray3",   T_BOOL, 0,       0,      1,       0,  "",     90,  "Force skipping this tray next cycle", ""),
    P("irr_liters_tray4",     T_U16,  150,     0,      500,     0,  "L",    91,  "Litres to flood this tray in summer", ""),
    P("irr_days_tray4",       T_U8,   14,      1,      60,      0,  "days", 92,  "Days between waterings in summer",    ""),
    P("irr_last_tray4",       T_U32,  0,       0,      100000,  0,  "days", 93,  "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray4",    T_BOOL, 0,       0,      1,       0,  "",     94,  "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray4",   T_BOOL, 0,       0,      1,       0,  "",     95,  "Force skipping this tray next cycle", ""),
    P("irr_liters_tray5",     T_U16,  150,     0,      500,     0,  "L",    96,  "Litres to flood this tray in summer", ""),
    P("irr_days_tray5",       T_U8,   14,      1,      60,      0,  "days", 97,  "Days between waterings in summer",    ""),
    P("irr_last_tray5",       T_U32,  0,       0,      100000,  0,  "days", 98,  "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray5",    T_BOOL, 0,       0,      1,       0,  "",     99,  "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray5",   T_BOOL, 0,       0,      1,       0,  "",     100, "Force skipping this tray next cycle", ""),
    P("irr_liters_tray6",     T_U16,  150,     0,      500,     0,  "L",    101, "Litres to flood this tray in summer", ""),
    P("irr_days_tray6",       T_U8,   14,      1,      60,      0,  "days", 102, "Days between waterings in summer",    ""),
    P("irr_last_tray6",       T_U32,  0,       0,      100000,  0,  "days", 103, "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray6",    T_BOOL, 0,       0,      1,       0,  "",     104, "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray6",   T_BOOL, 0,       0,      1,       0,  "",     105, "Force skipping this tray next cycle", ""),
    P("irr_liters_tray7",     T_U16,  150,     0,      500,     0,  "L",    106, "Litres to flood this tray in summer", ""),
    P("irr_days_tray7",       T_U8,   14,      1,      60,      0,  "days", 107, "Days between waterings in summer",    ""),
    P("irr_last_tray7",       T_U32,  0,       0,      100000,  0,  "days", 108, "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray7",    T_BOOL, 0,       0,      1,       0,  "",     109, "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray7",   T_BOOL, 0,       0,      1,       0,  "",     110, "Force skipping this tray next cycle", ""),
    P("irr_liters_tray8",     T_U16,  150,     0,      500,     0,  "L",    111, "Litres to flood this tray in summer", ""),
    P("irr_days_tray8",       T_U8,   14,      1,      60,      0,  "days", 112, "Days between waterings in summer",    ""),
    P("irr_last_tray8",       T_U32,  0,       0,      100000,  0,  "days", 113, "Last watering (days since 1 Jan",     ""),
    P("irr_forceOn_tray8",    T_BOOL, 0,       0,      1,       0,  "",     114, "Force watering this tray next cycle", ""),
    P("irr_forceOff_tray8",   T_BOOL, 0,       0,      1,       0,  "",     115, "Force skipping this tray next cycle", ""),
    P("irr_dryEwmaDays",      T_U16,  3,       1,      30,      0,  "days", 116, "Days of dryness history to modulate", "sampled every 2 h"),
    P("irr_dryLo_kPa",        T_F32,  0.15,    0.0,    2.0,     2,  "kPa",  117, "Air dryness giving the least",        ""),
    P("irr_dryHi_kPa",        T_F32,  1.2,     0.0,    5.0,     2,  "kPa",  118, "Air dryness giving the most",         "> irr_dryLo_kPa"),
    P("irr_modMin",           T_F32,  0.25,    0.1,    1.0,     2,  "",     119, "Least water given, in humid weather", "the floor even in still, humid air"),
    P("irr_modGamma",         T_F32,  1.0,     1.0,    4.0,     2,  "",     120, "How fast watering ramps up with",     ""),
    P("irr_volFreqSplit",     T_F32,  0.5,     0.0,    1.0,     2,  "",     121, "Reduce volume, or lengthen the",      "0 = period, 1 = volume"),
    P("irr_traysPerDay",      T_U16,  2,       1,      5,       0,  "",     122, "Most trays watered in one day",       ""),
    P("irr_dawnGate_degC",    T_F32,  12.0,    5.0,    30.0,    1,  "degC", 123, "Skip pre-dawn watering below this",   ""),
    P("irr_maxFill_sec",      T_U16,  300,     100,    1800,    0,  "sec",  124, "Alarm if a tray takes longer to",     ""),
    P("irr_drainWindow_sec",  T_U16,  300,     100,    1800,    0,  "sec",  125, "Time allowed for a tray to drain",    ""),
    P("irr_expDrainFrac",     T_F32,  0.6,     0.0,    1.0,     2,  "",     126, "Expected drain fraction",             ""),
    P("irr_drainMargin",      T_F32,  0.3,     0.0,    1.0,     2,  "",     127, "Drain fraction below this is a",      ""),
    P("irr_flush_sec",        T_U16,  10,      5,      300,     0,  "sec",  128, "Flush duration around the cycle",     ""),
    P("irr_input_qtyPerImp",  T_F32,  1.0,     0.01,   10.0,    2,  "L",    129, "Litres per pulse from the water",     "per meter datasheet"),
    P("irr_out_rawMin_uA",    T_U16,  0,       0,      24000,   0,  "uA",   130, "uA at zero output flow",              "per output-meter datasheet"),
    P("irr_out_rawMax_uA",    T_U16,  10000,   0,      24000,   0,  "uA",   131, "uA at full output scale",             "per output-meter datasheet"),
    P("irr_out_engMin_Lpm",   T_F32,  0.0,     0.0,    500.0,   1,  "Lpm",  132, "Output litres/min at zero signal",    "per output-meter datasheet"),
    P("irr_out_engMax_Lpm",   T_F32,  50.0,    0.0,    500.0,   1,  "Lpm",  133, "Output litres/min at full scale",     "per output-meter datasheet"),
    P("irr_out_min",          T_F32,  0.0,     0.0,    500.0,   1,  "Lpm",  134, "Lowest believable output flow",       "under this: a dead sensor"),
    P("irr_out_max",          T_F32,  50.0,    0.0,    500.0,   1,  "Lpm",  135, "Highest believable output flow",      "over this: a dead sensor"),
    P("irr_out_avg_ms",       T_U16,  200,     0,      10000,   0,  "ms",   136, "Rolling average for this flow meter", "per output-meter datasheet"),
    P("pwr_dc1_rawMin_mV",    T_U16,  4000,    0,      10000,   0,  "mV",   137, "Low-end of the raw measurement",      "per divider setup"),
    P("pwr_dc1_rawMax_mV",    T_U16,  6000,    0,      10000,   0,  "mV",   138, "High-end of the raw measurement",     "per divider setup"),
    P("pwr_dc1_engMin_V",     T_F32,  20.0,    0.0,    48.0,    1,  "V",    139, "Low-end of the actual DC voltage",    "per divider setup"),
    P("pwr_dc1_engMax_V",     T_F32,  30.0,    0.0,    48.0,    1,  "V",    140, "High-end of the actual DC voltage",   "per divider setup"),
    P("pwr_dc1_min",          T_F32,  20.0,    0.0,    48.0,    1,  "V",    141, "Lowest believable DC PS1 voltage",    "under this: a dead sensor"),
    P("pwr_dc1_max",          T_F32,  30.0,    0.0,    48.0,    1,  "V",    142, "Highest believable DC PS1 voltage",   "over this: a dead sensor"),
    P("pwr_dc1_avg_ms",       T_U16,  1000,    10,     10000,   0,  "ms",   143, "Rolling average for this DC bus",     "per divider setup"),
    P("pwr_dc2_rawMin_mV",    T_U16,  4000,    0,      10000,   0,  "mV",   144, "Low-end of the raw measurement",      "per divider setup"),
    P("pwr_dc2_rawMax_mV",    T_U16,  6000,    0,      10000,   0,  "mV",   145, "High-end of the raw measurement",     "per divider setup"),
    P("pwr_dc2_engMin_V",     T_F32,  20.0,    0.0,    48.0,    1,  "V",    146, "Low-end of the actual DC voltage",    "per divider setup"),
    P("pwr_dc2_engMax_V",     T_F32,  30.0,    0.0,    48.0,    1,  "V",    147, "High-end of the actual DC voltage",   "per divider setup"),
    P("pwr_dc2_min",          T_F32,  20.0,    0.0,    48.0,    1,  "V",    148, "Lowest believable DC PS2 voltage",    "under this: a dead sensor"),
    P("pwr_dc2_max",          T_F32,  30.0,    0.0,    48.0,    1,  "V",    149, "Highest believable DC PS2 voltage",   "over this: a dead sensor"),
    P("pwr_dc2_avg_ms",       T_U16,  1000,    10,     10000,   0,  "ms",   150, "Rolling average for this DC bus",     "per divider setup"),
    P("pwr_upsBat_rawMin_mV", T_U16,  4000,    0,      10000,   0,  "mV",   151, "Low-end of the raw measurement",      "per divider setup"),
    P("pwr_upsBat_rawMax_mV", T_U16,  6000,    0,      10000,   0,  "mV",   152, "High-end of the raw measurement",     "per divider setup"),
    P("pwr_upsBat_engMin_V",  T_F32,  20.0,    0.0,    48.0,    1,  "V",    153, "Low-end of the actual DC voltage",    "per divider setup"),
    P("pwr_upsBat_engMax_V",  T_F32,  30.0,    0.0,    48.0,    1,  "V",    154, "High-end of the actual DC voltage",   "per divider setup"),
    P("pwr_upsBat_min",       T_F32,  20.0,    0.0,    48.0,    1,  "V",    155, "Lowest believable UPS battery",       "under this: a dead sensor"),
    P("pwr_upsBat_max",       T_F32,  30.0,    0.0,    48.0,    1,  "V",    156, "Highest believable UPS battery",      "over this: a dead sensor"),
    P("pwr_upsBat_avg_ms",    T_U16,  1000,    10,     10000,   0,  "ms",   157, "Rolling average for this DC bus",     "per divider setup"),
    P("pwr_genBat_rawMin_mV", T_U16,  5000,    0,      10000,   0,  "mV",   158, "Low-end of the raw measurement",      "per divider setup"),
    P("pwr_genBat_rawMax_mV", T_U16,  7500,    0,      10000,   0,  "mV",   159, "High-end of the raw measurement",     "per divider setup"),
    P("pwr_genBat_engMin_V",  T_F32,  20.0,    10.0,   48.0,    1,  "V",    160, "Low-end of the actual DC voltage",    "per divider setup"),
    P("pwr_genBat_engMax_V",  T_F32,  30.0,    15.0,   48.0,    1,  "V",    161, "High-end of the actual DC voltage",   "per divider setup"),
    P("pwr_genBat_min",       T_F32,  20.0,    10.0,   48.0,    1,  "V",    162, "Lowest believable gen. battery V",    "under this: a dead sensor"),
    P("pwr_genBat_max",       T_F32,  30.0,    10.0,   48.0,    1,  "V",    163, "Highest believable gen. battery V",   "over this: a dead sensor"),
    P("pwr_genBat_avg_ms",    T_U16,  1000,    10,     10000,   0,  "ms",   164, "Rolling average for this DC bus",     "per divider setup"),

    // text parameters: their slot in the pool is laid out by
    // Bank::init(), so a row says how big it is, never where.
    // name         default        cap      validation    reg description              notes
    PS("net_ip",    DEF_NET_IP,    STR_CAP, STR_IPV4,     3,  "Static IP address",     ""),
    PS("net_mask",  DEF_NET_MASK,  STR_CAP, STR_IPV4,     4,  "Static IP netmask",     ""),
    PS("net_dns1",  DEF_NET_DNS1,  STR_CAP, STR_IPV4,     5,  "Static DNS resolver 1", ""),
    PS("net_dns2",  DEF_NET_DNS2,  STR_CAP, STR_IPV4_OPT, 6,  "Static DNS resolver 2", "empty = use dns1 alone"),
    PS("net_gw",    DEF_NET_GW,    STR_CAP, STR_IPV4,     7,  "Static IP gateway",     ""),
    PS("ntp_srv1",  DEF_NTP_SRV1,  STR_CAP, STR_HOST,     11, "NTP server 1",          ""),
    PS("ntp_srv2",  DEF_NTP_SRV2,  STR_CAP, STR_HOST_OPT, 12, "NTP server 2",          "empty = no fallback"),
    PS("mqtt_host", DEF_MQTT_HOST, STR_CAP, STR_HOST,     15, "MQTT broker host",      ""),

/** @} */

/** @name Sensor inputs - read from the field, invalid past their timeout
 *  @{ */

    // name               type   dev fc reg n  min              max             fallback  timeout dgt unit   description                        notes
    MI("grid_L1_V",       T_F32, 5,  3, 0,  2, SF(0.0f),        SF(500.0f),     SF(0.0f), 5000,   1,  "V",   "L1 phase voltage",                ""),
    MI("grid_L2_V",       T_F32, 5,  3, 1,  2, SF(0.0f),        SF(500.0f),     SF(0.0f), 5000,   1,  "V",   "L2 phase voltage",                ""),
    MI("grid_L3_V",       T_F32, 5,  3, 2,  2, SF(0.0f),        SF(500.0f),     SF(0.0f), 5000,   1,  "V",   "L3 phase voltage",                ""),
    MI("grid_L1_A",       T_F32, 5,  3, 3,  2, SF(0.0f),        SF(200.0f),     SF(0.0f), 5000,   1,  "A",   "L1 current",                      ""),
    MI("grid_L2_A",       T_F32, 5,  3, 4,  2, SF(0.0f),        SF(200.0f),     SF(0.0f), 5000,   1,  "A",   "L2 current",                      ""),
    MI("grid_L3_A",       T_F32, 5,  3, 5,  2, SF(0.0f),        SF(200.0f),     SF(0.0f), 5000,   1,  "A",   "L3 current",                      ""),
    MI("grid_L1_VA",      T_F32, 5,  3, 6,  2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VA",  "L1 apparent power",               ""),
    MI("grid_L2_VA",      T_F32, 5,  3, 7,  2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VA",  "L2 apparent power",               ""),
    MI("grid_L3_VA",      T_F32, 5,  3, 8,  2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VA",  "L3 apparent power",               ""),
    MI("grid_L1_VAr",     T_F32, 5,  3, 9,  2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VAr", "L1 reactive power",               ""),
    MI("grid_L2_VAr",     T_F32, 5,  3, 10, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VAr", "L2 reactive power",               ""),
    MI("grid_L3_VAr",     T_F32, 5,  3, 11, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VAr", "L3 reactive power",               ""),
    MI("grid_P1_PF",      T_F32, 5,  3, 12, 2, SF(-1.0f),       SF(1.0f),       SF(0.0f), 5000,   3,  "",    "L1 power factor",                 ""),
    MI("grid_P2_PF",      T_F32, 5,  3, 13, 2, SF(-1.0f),       SF(1.0f),       SF(0.0f), 5000,   3,  "",    "L2 power factor",                 ""),
    MI("grid_P3_PF",      T_F32, 5,  3, 14, 2, SF(-1.0f),       SF(1.0f),       SF(0.0f), 5000,   3,  "",    "L3 power factor",                 ""),
    MI("grid_P1_PA",      T_F32, 5,  3, 15, 2, SF(-180.0f),     SF(180.0f),     SF(0.0f), 5000,   1,  "deg", "L1 phase angle",                  ""),
    MI("grid_P2_PA",      T_F32, 5,  3, 16, 2, SF(-180.0f),     SF(180.0f),     SF(0.0f), 5000,   1,  "deg", "L2 phase angle",                  ""),
    MI("grid_P3_PA",      T_F32, 5,  3, 17, 2, SF(-180.0f),     SF(180.0f),     SF(0.0f), 5000,   1,  "deg", "L3 phase angle",                  ""),
    MI("grid_LN_V",       T_F32, 5,  3, 18, 2, SF(0.0f),        SF(500.0f),     SF(0.0f), 5000,   1,  "V",   "average line-to-neutral voltage", ""),
    MI("grid_LL_V",       T_F32, 5,  3, 19, 2, SF(0.0f),        SF(900.0f),     SF(0.0f), 5000,   1,  "V",   "average line-to-line voltage",    ""),
    MI("grid_avg_A",      T_F32, 5,  3, 20, 2, SF(0.0f),        SF(200.0f),     SF(0.0f), 5000,   1,  "A",   "average current",                 ""),
    MI("grid_total_A",    T_F32, 5,  3, 21, 2, SF(0.0f),        SF(600.0f),     SF(0.0f), 5000,   1,  "A",   "total current",                   ""),
    MI("grid_total_W",    T_F32, 5,  3, 22, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "W",   "total active power",              ""),
    MI("grid_total_VA",   T_F32, 5,  3, 23, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VA",  "total apparent power",            ""),
    MI("grid_total_VAR",  T_F32, 5,  3, 24, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VAr", "total reactive power",            ""),
    MI("grid_total_PF",   T_F32, 5,  3, 25, 2, SF(-1.0f),       SF(1.0f),       SF(0.0f), 5000,   3,  "",    "total power factor",              ""),
    MI("grid_total_PA",   T_F32, 5,  3, 26, 2, SF(-180.0f),     SF(180.0f),     SF(0.0f), 5000,   1,  "deg", "total phase angle",               ""),
    MI("grid_Hz",         T_F32, 5,  3, 27, 2, SF(0.0f),        SF(100.0f),     SF(0.0f), 5000,   2,  "Hz",  "line frequency",                  ""),
    MI("grid_12_V",       T_F32, 5,  3, 28, 2, SF(0.0f),        SF(900.0f),     SF(0.0f), 5000,   1,  "V",   "L1-L2 voltage",                   ""),
    MI("grid_L23_V",      T_F32, 5,  3, 29, 2, SF(0.0f),        SF(900.0f),     SF(0.0f), 5000,   1,  "V",   "L2-L3 voltage",                   ""),
    MI("grid_L31_V",      T_F32, 5,  3, 30, 2, SF(0.0f),        SF(900.0f),     SF(0.0f), 5000,   1,  "V",   "L3-L1 voltage",                   ""),
    MI("grid_LL_avg_THD", T_F32, 5,  3, 31, 2, SF(0.0f),        SF(100.0f),     SF(0.0f), 5000,   1,  "%",   "line-to-line average THD",        ""),
    MI("grid_LN_avg_THD", T_F32, 5,  3, 32, 2, SF(0.0f),        SF(100.0f),     SF(0.0f), 5000,   1,  "%",   "line-to-neutral average THD",     ""),
    MI("grid_N_A",        T_F32, 5,  3, 33, 2, SF(0.0f),        SF(200.0f),     SF(0.0f), 5000,   1,  "A",   "neutral current",                 ""),
    MI("grid_L1_W",       T_F32, 5,  3, 34, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "W",   "L1 active power",                 ""),
    MI("grid_L2_W",       T_F32, 5,  3, 35, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "W",   "L2 active power",                 ""),
    MI("grid_L3_W",       T_F32, 5,  3, 36, 2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "W",   "L3 active power",                 ""),
    MI("ats_V",           T_F32, 6,  3, 0,  2, SF(0.0f),        SF(500.0f),     SF(0.0f), 5000,   1,  "V",   "ATS voltage",                     ""),
    MI("ats_A",           T_F32, 6,  3, 1,  2, SF(0.0f),        SF(200.0f),     SF(0.0f), 5000,   1,  "A",   "ATS current",                     ""),
    MI("ats_VAr",         T_F32, 6,  3, 3,  2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VAr", "ATS reactive power",              ""),
    MI("ats_VA",          T_F32, 6,  3, 4,  2, SF(-1000000.0f), SF(1000000.0f), SF(0.0f), 5000,   0,  "VA",  "ATS apparent power",              ""),
    MI("ats_Hz",          T_F32, 6,  3, 5,  2, SF(0.0f),        SF(100.0f),     SF(0.0f), 5000,   2,  "Hz",  "ATS frequency",                   ""),

/** @} */

/** @name Alarms - each row IS its condition: the boolean plus the announce policy
 *  @{ */

    //name                       code        on off recent delay minOn description                        remediation
    A("alarm_ac_grid",           "AC_GRID",  2, 2,  0,     0,    10,   "Grid AC out-of-spec/offline",     "check mains breaker / utility feed"),
    A("alarm_dc_ps1",            "DC_PS1",   2, 2,  0,     0,    0,    "DC PS1 volts too low",            ""),
    A("alarm_gen",               "GEN_FLT",  1, 2,  0,     0,    10,   "Generator failed to start",       "inspect generator + start battery"),
    A("alarm_ac_local",          "AC_LOCAL", 2, 2,  0,     0,    0,    "Local AC out of spec or offline", ""),
    A("alarm_dc_ps2",            "DC_PS2",   2, 2,  0,     0,    10,   "DC PS2 volts too low",            ""),
    A("alarm_dc_ups",            "DC_UPS",   1, 2,  0,     0,    10,   "DC UPS on battery",               "restore mains; check UPS battery"),
    A("alarm_dc_local",          "DC_LOCAL", 2, 2,  0,     0,    0,    "Local DC volts too low",          ""),
    A("alarm_generator_battery", "GEN_BATT", 5, 5,  0,     0,    0,    "Gen. start battery low",          ""),
    A("alarm_plc",               "PLC_FLT",  2, 2,  0,     0,    0,    "Failed to synchronize with PLC",  ""),
    A("alarm_net_fault",         "NET_FLT",  5, 5,  0,     0,    10,   "Network error",                   ""),
    A("alarm_ntp_timeout",       "NTP_TIM",  5, 5,  0,     0,    10,   "NTP sync timeout",                ""),
    A("alarm_actuator_fault",    "ACT_FLT",  2, 2,  0,     0,    0,    "Actioner failure",                ""),
    A("alarm_sensor_fault",      "SNS_FLT",  2, 2,  0,     0,    0,    "Sensor fault",                    ""),
    A("alarm_too_hot",           "TOO_HOT",  3, 3,  0,     0,    15,   "Inside temperature too high",     "increase ventilation / open windows"),
    A("alarm_too_cold",          "TOO_COLD", 3, 3,  0,     0,    15,   "Inside temperature too low",      ""),
    A("alarm_too_humid",         "TOO_HUM",  3, 3,  0,     0,    15,   "Relative humidity too high",      ""),
    A("alarm_tmp_str",           "TMP_STR",  5, 5,  0,     0,    0,    "Temp. stratification too high",   ""),
    A("alarm_water_leak",        "WTR_LK",   1, 2,  0,     0,    0,    "Leak in irrigation circuit",      "shut irrigation valve, inspect circuit"),
    A("alarm_water_pressure",    "WTR_BAR",  2, 2,  0,     0,    0,    "Water pressure too low",          ""),
    A("alarm_water_clog",        "WTR_CLOG", 3, 3,  0,     0,    0,    "Failing to drain a tray",         ""),
    A("alarm_io_drop",           "IO_DROP",  2, 2,  0,     0,    0,    "Logical I/O drops",               ""),
    A("alarm_log_flood",         "LOG_FLD",  2, 2,  0,     0,    0,    "Logs flooding",                   ""),
    A("alarm_weather_storm",     "WH_STORM", 2, 5,  0,     0,    15,   "Current storm conditions",        "close roof windows, secure screens"),
/** @} */

/** @name Status values - published over MQTT and the Modbus TCP slave
 *
 *  Their register is hand-assigned from a range of its own, so the parameter
 *  block above can grow without renumbering anything an external master holds.
 *  @{ */
    // name                  type    default dgt unit  reg  description                             notes
    SV("sys_supply_lockout", T_BOOL, 0,      0,  "",   500, "supply unhealthy: actuators held off", ""),

    SV("sys_supply_hz",      T_F32,  0,      2,  "Hz", 501, "deadbanded supply frequency",          ""),
/** @} */

/** @name Actuator outputs - driven to the field, dropped to safe when control is off
 *  @{ */

 // TDC/RO-1

    // name                   dev coil safe   invert description                       notes
    RO("cli_window1_opn",     31, 0,   false, false, "window 1 open",                  ""),
    RO("cli_window1_clo",     31, 1,   false, false, "window 1 close",                 ""),
    RO("irr_input_ro1",       31, 2,   false, false, "irrigation input valve 1",       ""),
    RO("irr_output_ro1",      31, 3,   true,  true,  "irrigation output valve 1",      ""),
    RO("irr_tray1",           31, 4,   true,  true,  "tray 1 valve",                   ""),
    RO("irr_tray2",           31, 5,   true,  true,  "tray 2 valve",                   ""),

 // TDC/RO-2
    RO("cli_window2_opn",     32, 0,   false, false, "window 2 open",                  ""),
    RO("cli_window2_clo",     32, 1,   false, false, "window 2 close",                 ""),
    RO("irr_input_ro2",       32, 2,   false, false, "irrigation input valve 2",       ""),
    RO("irr_output_ro2",      32, 3,   true,  true,  "irrigation output valve 2",      ""),
    RO("irr_tray3",           32, 4,   true,  true,  "tray 3 valve",                   ""),
    RO("irr_tray4",           32, 5,   true,  true,  "tray 4 valve",                   ""),

 // TDC/RO-3
    RO("cli_window3_opn",     33, 0,   false, false, "window 3 open",                  ""),
    RO("cli_window3_clo",     33, 1,   false, false, "window 3 close",                 ""),
    RO("cli_destrat1_speed1", 33, 2,   false, false, "destratification fan 1 speed 1", ""),
    RO("cli_destrat1_speed2", 33, 3,   false, false, "destratification fan 1 speed 2", ""),
    RO("cli_destrat1_speed3", 33, 4,   false, false, "destratification fan 1 speed 3", ""),
    RO("cli_screen",          33, 5,   false, false, "thermal screen",                 ""),
    RO("irr_tray5",           33, 6,   true,  true,  "tray 5 valve",                   ""),
    RO("irr_tray6",           33, 7,   true,  true,  "tray 6 valve",                   ""),

 // TDC/RO-4
    RO("cli_window4_opn",     34, 0,   false, false, "window 4 open",                  ""),
    RO("cli_window4_clo",     34, 1,   false, false, "window 4 close",                 ""),
    RO("cli_destrat2_speed1", 34, 2,   false, false, "destratification fan 2 speed 1", ""),
    RO("cli_destrat2_speed2", 34, 3,   false, false, "destratification fan 2 speed 2", ""),
    RO("cli_destrat2_speed3", 34, 4,   false, false, "destratification fan 2 speed 3", ""),
    RO("cli_skirtValve",      34, 5,   false, false, "wet skirt valve",                ""),
    RO("irr_tray7",           34, 6,   true,  true,  "tray 7 valve",                   ""),
    RO("irr_tray8",           34, 7,   true,  true,  "tray 8 valve",                   ""),

/** @} */

};

const uint16_t g_var_count = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));


/** @brief How many rows the table has. Derived from the table itself - there
 *  is no second list of variables to keep in step with it. Everything else
 *  addresses a row by NAME and the bank resolves it: see Bank::find(). */
constexpr uint16_t VAR_TOTAL = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));

/** @name Home screen - the one table no rule derives
 *
 *  Everything else about a variable follows from the variable: its group is its
 *  name prefix, whether it is published is its scope, whether it is an alarm is
 *  its scope again. A home field is different. "Line V" is a caption a person
 *  chose for grid_LN_V, and nothing in the name says the row belongs on the
 *  landing page, let alone where in the layout - so it is declared.
 *  @{ */

/** @brief Home-screen readings: label + variable. The HomePage lays these out
 *  in three columns of 8, filled column-major; column 1 holds 6 (its top two
 *  cells carry the uptime readout and a spacer). Labels are capped at 11 chars
 *  and values at 13 by the column geometry. */
const HomeVar g_home_fields[] = {
    // column 1 (6): the incoming supply at a glance
    { "Line V",    "grid_LN_V" },
    { "Line-line", "grid_LL_V" },
    { "Line Hz",   "grid_Hz" },
    { "Current",   "grid_total_A" },
    { "Power",     "grid_total_W" },
    { "Pwr factor","grid_total_PF" },
    // column 2 (8): the same supply, per phase
    { "L1 V",      "grid_L1_V" },
    { "L2 V",      "grid_L2_V" },
    { "L3 V",      "grid_L3_V" },
    { "L1 A",      "grid_L1_A" },
    { "L2 A",      "grid_L2_A" },
    { "L3 A",      "grid_L3_A" },
    { "Neutral A", "grid_N_A" },
    { "THD L-N",   "grid_LN_avg_THD" },
    // column 3 (8): the transfer switch, then derived state
    { "ATS V",     "ats_V" },
    { "ATS A",     "ats_A" },
    { "ATS Hz",    "ats_Hz" },
    { "Supply Hz", "sys_supply_hz" },
    { "Lockout",   "sys_supply_lockout" },
    { "Screen",    "cli_screen" },
    { "Skirt",     "cli_skirtValve" },
};
const uint8_t g_home_field_count =
    (uint8_t)(sizeof(g_home_fields) / sizeof(g_home_fields[0]));
/** @brief The same count as a constant expression, to size the resolved-ref
 *  array in the store above. */
static const uint16_t HOME_FIELDS = sizeof(g_home_fields) / sizeof(g_home_fields[0]);

/** @} */

// Capacity contract: the home screen silently ignores fields past its two
// columns, so pin the count at compile time instead.
static_assert(sizeof(g_home_fields) / sizeof(g_home_fields[0]) <= Config::HOME_FIELDS_MAX,
              "too many home fields: the HomePage would silently drop the tail");

/** @name Value store (RAM, parallel to the table)
 *
 *  Header-only, which needs one trick worth explaining. A plain
 *  `Value g_var_values[]` at namespace scope here would be a DEFINITION in every
 *  TU that includes this file, and the link breaks the moment two of them do
 *  (the firmware has two, the test binary more). C++17 would spell the fix
 *  `inline`; the portable C++11 equivalent is a class template's static member,
 *  which has vague linkage - the compiler emits it in each TU and the linker
 *  folds them into one object.
 *
 *  That is the whole reason this site needs no .cpp beside its tables. Taking
 *  the address of a static member is still a constant expression, so
 *  SITE_VAR_TABLES below stays constant-initialised into flash.
 *  @{ */

/** @brief The site's mutable state. @c Tag is never used - the template exists
 *  only to get vague linkage; there is exactly one instantiation, @c Store. */
template <typename Tag>
struct SiteStore {
    /** @brief One slot per row of g_vars, addressed the same way. */
    static Value  values[VAR_TOTAL];
    /** @brief T_STR live text: one STR_CAP slot per text parameter, addressed by
     *  the row's TextSlot offset. The one place a byte offset survives. */
    static char   pool[STR_POOL_BYTES];
    /** @brief Scratch for the bank's name index: one row number per row, sorted
     *  by name at construction so find() is a binary search. Sized off the table
     *  itself, so there is no capacity to keep in step with it. */
    static uint16_t name_index[VAR_TOTAL];
    /** @brief Where the home fields' names resolve to, filled at bring-up
     *  (BotanikFirmware::setup). VAR_NONE until then, which reads as the inert
     *  sink rather than as row 0. */
    static VarRef home_refs[HOME_FIELDS];
};

template <typename Tag> Value  SiteStore<Tag>::values[VAR_TOTAL] = {};
template <typename Tag> char   SiteStore<Tag>::pool[STR_POOL_BYTES] = {};
template <typename Tag> uint16_t SiteStore<Tag>::name_index[VAR_TOTAL] = {};
template <typename Tag> VarRef SiteStore<Tag>::home_refs[HOME_FIELDS] = {};

/** @brief The one instantiation. Everything addresses the store through this. */
using Store = SiteStore<void>;

/** @} */

#define SITE_VAR_TABLES { \
    g_vars, Site::Store::values, VAR_TOTAL, \
    Site::Store::pool, STR_POOL_BYTES, Site::Store::name_index }

// The pool must cover every text parameter's slot; a short pool would let one
// silently overlap its neighbour.
static_assert(STR_POOL_BYTES >= 8 * STR_CAP, "the string pool is too small");

}  // namespace Site

#endif // SITE_VARS_H
