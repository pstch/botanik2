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
 * (BOTANIK_MODBUS_BAUD is pinned to match, in this site's config.h.)
 * 20 field devices, 102 mapped signals, 13 free channels. (The published map
 * lists 70, and omits MTR-2 object 2, active power, which the device answers.)
 *
 *   SRR/TAC/MTR-1  Eastron SDM630    addr 5   37 readings
 *   SRR/TAC/MTR-2  Lettel MCT1-140   addr 6    6 readings
 *   SRR/TDC/AI-1   Waveshare 25767   addr 11   5 readings (+3 free)
 *   SRR/TDC/AO-1   Waveshare 26211   addr 16   2 drives   (+6 free)
 *   SRR/TDC/RO-1   Waveshare 26517   addr 31   6 coils (+2 free)
 *   SRR/TDC/RO-2   Waveshare 26517   addr 32   6 coils (+2 free)
 *   SRR/TDC/RO-3   Waveshare 26517   addr 33   8 coils
 *   SRR/TDC/RO-4   Waveshare 26517   addr 34   8 coils
 *   SRR/CLI/TH-1..3   temp/RH           addr 41-43  exterior
 *   SRR/CLI/TH-4..6   temp/RH           addr 44-46  crawlspace
 *   SRR/CLI/TH-7..9   temp/RH           addr 47-49  low level
 *   SRR/CLI/TH-10..12 temp/RH           addr 50-52  high level
 *
 * Everything is XP_MODBUS today: the site has no XP_LOCAL variables yet.
 *
 * @warning Binding::reg is the Modbus REGISTER ADDRESS the master puts on the
 * wire: the coil number on the relay boards, and the protocol START ADDRESS on
 * the meters (30001 -> 0, 30003 -> 2, ...). NEVER the device profile's object
 * index - nothing translates one into the other, and a row carrying the index
 * decodes half of the row before it. Nor the register column of the device map,
 * which is the Opta's own Modbus *slave* address and belongs in the slave map.
 * Each row's notes record the object index, so a reading can still be traced
 * back to the published map.
 *
 * @note The meters answer FC04 (input registers). The SDM630 manual requires it;
 * the MCT1-140 accepts 03 or 04 and is given 04 to match.
 *
 * @note Both meters publish IEEE-754 singles, high word at the lower address, in
 * direct engineering units, so these rows are 1:1 and carry no calibration. The
 * MCT1-140's "unite et precision" column (0.1V, 0.01A, 10W) is RESOLUTION, not a
 * scale factor - except on the Long energy counters, where it is a multiplier
 * (10 Wh per count).
 */

#include <cstdint>      // uint8_t/uint16_t

#include "libbotanik.h" // the A/P/G/SV/MI/AI/DI/MO/AO/DO/RO factories, Vars::Bank

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

/** @brief Text-parameter capacity, including the NUL. */
static const uint16_t STR_CAP = 64;
/** @brief Room for every text parameter in the table. This site declares none of
 *  its own, so it is exactly what the library's rows need; adding one adds
 *  `+ n * STR_CAP` here. */
static const uint16_t STR_POOL_BYTES = BOTANIK_LIBRARY_STR_BYTES;


/** @name Analog calibration - the two-point span and the believable band
 *
 *  One block per channel: sharing a block shares its PARAMETERS, so share only
 *  when two readings must agree by construction. PS1, PS2 and the UPS battery
 *  are the same divider design but three physical dividers, so three blocks.
 *
 *  Every field is R(), an operator-editable parameter: these are bench
 *  measurements, and correcting one should not need a firmware build.
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
 *  must resolve, and a channel's two raw endpoints must agree on their unit. */
const AnalogCal* const g_analog_cals[] = {
    &CAL_PWR_DC1, &CAL_PWR_DC2, &CAL_PWR_UPS_BAT, &CAL_PWR_GEN_BAT,
    &CAL_IRR_OUTPUT, &CAL_INTRACTOR,
};
const uint8_t g_analog_cal_count =
    (uint8_t)(sizeof(g_analog_cals) / sizeof(g_analog_cals[0]));

/** @} */

/** @brief Every variable this site declares, in one table.
 *
 *  Each row is a call to one of the scope factories in vars/declare.h - A, P, G,
 *  SV, MI, AI, DI, MO, AO, DO, RO - and takes only the arguments its scope can
 *  use, so a relay with a plausibility band does not compile. Alarms live here
 *  and only here: an A() row carries both the boolean that raises it and the
 *  policy the AlarmController runs it on, found by scanning for SCOPE_A.
 *
 *  Plain `const`, so each including TU gets its own copy. */
const Variable g_vars[] = {

/** @name The library's own rows
 *
 *  The bank holds one contiguous table, so everything libbotanik declares is
 *  spliced in through this single define. It goes FIRST and ends with its
 *  parameters, so they abut the site's below and the ROLE_PARAM run stays
 *  contiguous. See library_vars.h.
 *  @{ */

    BOTANIK_LIBRARY_VARS

/** @} */

/** @name Parameters - persisted and editable; the PARAMS page and `set` write these
 *
 *  The `reg` column is the Modbus TCP *slave* register an external master reads
 *  this parameter at. It is hand-assigned, so **adding a parameter must never
 *  renumber the others**: each group gets a block with room to grow, and a new
 *  row takes the next free register INSIDE its own block:
 *
 *      net    1.. 19      cli  100..199
 *      ntp   20.. 39      sol  200..249
 *      mqtt  40.. 59      irr  250..399
 *      geo   60.. 79      pwr  400..469
 *      mb    80.. 89      (470..499 spare)
 *      (90..99 spare)
 *
 *  Status values live at 500+, alarms at 800..899 and the library's own rows at
 *  900..999, so the four kinds never collide and the number says which one a row
 *  is. A group that outgrows its block takes a spare range rather than spilling
 *  into its neighbour. tests/test_site_names.cpp fails on a duplicate, a zero,
 *  or a row outside its scope's block.
 *  @{ */

    //name                    type    default  min     max      dgt unit    reg  description                               notes
    P("cli_coldBankOfs_degC", T_F32,  6.0,     0.0,    15.0,    1,  "degC", 100, "how much cooler before a hot day",       ""),
    P("cli_heatBankOfs_degC", T_F32,  7.0,     0.0,    15.0,    1,  "degC", 101, "how much warmer before a cold night",    ""),
    P("cli_heatBankTrg_degC", T_F32,  10.0,    6.0,    18.0,    1,  "degC", 102, "yesterday's low below this: bank heat",  ""),
    P("cli_coldBankTrg_degC", T_F32,  24.0,    12.0,   40.0,    1,  "degC", 103, "yesterday's high above this: bank cold", ""),
    P("cli_coolSet_degC",     T_F32,  20.0,    10.0,   30.0,    1,  "degC", 104, "base cooling target temperature",        "every cooling stage keys off this"),
    P("cli_coolKp",           T_F32,  0.08,    0.0,    1.0,     2,  "",     105, "cooling PID proportional gain",          "leave as set"),
    P("cli_coolTi_sec",       T_F32,  200.0,   0.0,    3600.0,  0,  "sec",  106, "cooling PID integral time",              "leave as set"),
    P("cli_coolTd_sec",       T_F32,  0.0,     0.0,    10.0,    0,  "sec",  107, "cooling PID derivative time",            "leave at zero"),
    P("cli_ventTempMgn_degC", T_F32,  1.0,     0.0,    15.0,    1,  "degC", 108, "intake air must be this much cooler",    ""),
    P("cli_skirtDryMin_degC", T_F32,  4.0,     0.0,    10.0,    1,  "degC", 109, "air this dry lets the pad cool",         ""),
    P("cli_thermalScreenDem", T_F32,  -0.2,    -1.0,   0.0,     2,  "",     110, "cooling effort that deploys the screen", "negative demand = heating"),
    P("cli_groundHeatDem",    T_F32,  -0.1,    -1.0,   0.0,     2,  "",     111, "cooling effort that draws ground air",   "negative demand = heating"),
    P("cli_windowStartDem",   T_F32,  0.1,     0.0,    1.0,     2,  "",     112, "cooling effort that opens windows",      ""),
    P("cli_intakeStartDem",   T_F32,  0.3,     0.0,    1.0,     2,  "",     113, "cooling effort that starts intake fans", ""),
    P("cli_windowFullDem",    T_F32,  0.5,     0.0,    1.0,     2,  "",     114, "cooling effort at fully open windows",   ""),
    P("cli_skirtCoolDem",     T_F32,  0.7,     0.0,    1.0,     2,  "",     115, "cooling effort that starts the skirt",   ""),
    P("cli_fanLowDem",        T_F32,  0.2,     0.0,    1.0,     2,  "",     116, "cooling effort for fan speed 1",         ""),
    P("cli_fanMedDem",        T_F32,  0.4,     0.0,    1.0,     2,  "",     117, "cooling effort for fan speed 2",         "> cli_fanLowDem"),
    P("cli_fanHighDem",       T_F32,  0.8,     0.0,    1.0,     2,  "",     118, "cooling effort for fan speed 3",         "> cli_fanMedDem"),
    P("cli_intakeFullDem",    T_F32,  0.9,     0.0,    1.0,     2,  "",     119, "cooling effort at full intake speed",    ""),
    P("cli_dTStratLow_degC",  T_F32,  2.0,     0.0,    10.0,    1,  "degC", 120, "stratification gap for fan speed 1",     ""),
    P("cli_dTStratMed_degC",  T_F32,  4.0,     0.0,    10.0,    1,  "degC", 121, "stratification gap for fan speed 2",     ""),
    P("cli_dTStratHigh_degC", T_F32,  6.0,     0.0,    10.0,    1,  "degC", 122, "stratification gap for fan speed 3",     ""),
    P("cli_dTStratHyst_degC", T_F32,  2.0,     0.5,    5.0,     1,  "degC", 123, "gap change needed to switch fan speed",  ""),
    P("cli_dehumFull_degC",   T_F32,  3.0,     0.0,    10.0,    1,  "degC", 124, "dryness margin: dehumidify fully",       "< cli_dehumStart_degC"),
    P("cli_dehumStart_degC",  T_F32,  4.5,     0.0,    10.0,    1,  "degC", 125, "dryness margin: dehumidify starts",      "> cli_dehumFull_degC"),
    P("cli_humidStart_degC",  T_F32,  18.0,    0.0,    30.0,    1,  "degC", 126, "dryness margin: humidify starts",        "> cli_dehumStart_degC; skirt starts pulsing"),
    P("cli_humidFull_degC",   T_F32,  28.0,    0.0,    40.0,    1,  "degC", 127, "dryness margin: humidify fully",         "> cli_humidStart_degC; skirt at full pulse"),
    P("cli_rotRhCap",         T_F32,  0.6,     0.0,    1.0,     2,  "",     129, "RH that starts rot prevention",          ""),
    P("cli_ventDryMgn_degC",  T_F32,  1.0,     0.0,    5.0,     1,  "degC", 130, "outside air must be this much drier",    ""),
    P("cli_tooCold_degC",     T_F32,  4.0,     0.0,    10.0,    1,  "degC", 131, "alarm: too COLD",                        "match the gas-heater thermostat"),
    P("cli_tooHot_degC",      T_F32,  35.0,    20.0,   50.0,    1,  "degC", 132, "alarm: too HOT",                         ""),
    P("cli_tooHumid_degC",    T_F32,  1.0,     0.0,    5.0,     1,  "degC", 133, "alarm: too HUMID",                       "match the dehumidifier setting"),
    P("cli_intr_rawMin_mV",   T_F32,  0.0,     0.0,    24000.0, 0,  "mV",   134, "intractor raw value at zero speed",      "per intractor datasheet"),
    P("cli_intr_rawMax_mV",   T_F32,  10000.0, 0.0,    24000.0, 0,  "mV",   135, "intractor raw value at full speed",      "per intractor datasheet"),
    P("cli_intr_engMin",      T_F32,  0.0,     0.0,    1.0,     2,  "",     136, "intractor speed at the low end",         "per intractor datasheet"),
    P("cli_intr_engMax",      T_F32,  1.0,     0.0,    1.0,     2,  "",     137, "intractor speed at the high end",        ""),
    P("cli_intr_min",         T_F32,  0.0,     0.0,    1.0,     2,  "",     138, "lowest believable intractor speed",      "under this: a dead sensor"),
    P("cli_intr_max",         T_F32,  1.0,     0.0,    1.0,     2,  "",     139, "highest believable intractor speed",     "over this: a dead sensor"),
    P("cli_windShut_mps",     T_F32,  5.0,     0.0,    50.0,    1,  "mps",  140, "wind speed that forces windows shut",    ""),
    P("cli_windowTravel_sec", T_U16,  15,      1,      60,      0,  "sec",  141, "time to fully open/close a window",      "per actuator datasheet"),
    P("cli_windowTmo_sec",    T_U16,  20,      1,      90,      0,  "sec",  142, "safe time to fully open/close a window", "per actuator datasheet"),
    P("cli_screenTravel_sec", T_U16,  60,      1,      300,     0,  "sec",  143, "time to deploy/remove the screen",       "per actuator datasheet"),
    P("cli_screenTmo_sec",    T_U16,  80,      1,      400,     0,  "sec",  144, "safe time to deploy/remove the screen",  "per actuator datasheet"),
    P("cli_rainLockout_sec",  T_U16,  900,     0,      3600,    0,  "sec",  145, "keep windows shut this long after rain", ""),
    P("cli_skirtPeriod_sec",  T_U16,  60,      10,     600,     0,  "sec",  146, "skirt valve modulation period",          "humidify effort sets the on-time in it"),
    P("cli_skirtMinOn_sec",   T_U16,  10,      1,      600,     0,  "sec",  147, "shortest skirt valve pulse",             "<= cli_skirtPeriod_sec; under it, stay off"),
    P("cli_screenNight_degC", T_F32,  13.0,    0.0,    50.0,    1,  "degC", 148, "night temperature deploying the screen", ""),
    P("sol_coldBankRise_min", T_I16,  -30,     -180,   60,      0,  "min",  200, "cold banking starts, from sunrise",      ""),
    P("sol_breezeRise_min",   T_I16,  30,      -60,    180,     0,  "min",  201, "dawn breeze starts, from sunrise",       ""),
    P("sol_dayRise_min",      T_I16,  60,      0,      180,     0,  "min",  202, "day operation starts, from sunrise",     ""),
    P("sol_heatBankSet_min",  T_I16,  -180,    -300,   0,       0,  "min",  203, "heat banking starts, from sunset",       ""),
    P("sol_nightSet_min",     T_I16,  30,      0,      180,     0,  "min",  204, "night operation starts, from sunset",    ""),
    P("irr_forceNow",         T_BOOL, 0,       0,      1,       0,  "",     250, "water now, then clear this flag",        ""),
    P("irr_liters_tray1",     T_U16,  150,     0,      500,     0,  "L",    251, "litres to flood this tray in summer",    ""),
    P("irr_days_tray1",       T_U8,   14,      1,      60,      0,  "days", 252, "days between waterings in summer",       ""),
    P("irr_last_tray1",       T_U32,  0,       0,      100000,  0,  "days", 253, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray1",    T_BOOL, 0,       0,      1,       0,  "",     254, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray1",   T_BOOL, 0,       0,      1,       0,  "",     255, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray2",     T_U16,  150,     0,      500,     0,  "L",    256, "litres to flood this tray in summer",    ""),
    P("irr_days_tray2",       T_U8,   14,      1,      60,      0,  "days", 257, "days between waterings in summer",       ""),
    P("irr_last_tray2",       T_U32,  0,       0,      100000,  0,  "days", 258, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray2",    T_BOOL, 0,       0,      1,       0,  "",     259, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray2",   T_BOOL, 0,       0,      1,       0,  "",     260, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray3",     T_U16,  150,     0,      500,     0,  "L",    261, "litres to flood this tray in summer",    ""),
    P("irr_days_tray3",       T_U8,   14,      1,      60,      0,  "days", 262, "days between waterings in summer",       ""),
    P("irr_last_tray3",       T_U32,  0,       0,      100000,  0,  "days", 263, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray3",    T_BOOL, 0,       0,      1,       0,  "",     264, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray3",   T_BOOL, 0,       0,      1,       0,  "",     265, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray4",     T_U16,  150,     0,      500,     0,  "L",    266, "litres to flood this tray in summer",    ""),
    P("irr_days_tray4",       T_U8,   14,      1,      60,      0,  "days", 267, "days between waterings in summer",       ""),
    P("irr_last_tray4",       T_U32,  0,       0,      100000,  0,  "days", 268, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray4",    T_BOOL, 0,       0,      1,       0,  "",     269, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray4",   T_BOOL, 0,       0,      1,       0,  "",     270, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray5",     T_U16,  150,     0,      500,     0,  "L",    271, "litres to flood this tray in summer",    ""),
    P("irr_days_tray5",       T_U8,   14,      1,      60,      0,  "days", 272, "days between waterings in summer",       ""),
    P("irr_last_tray5",       T_U32,  0,       0,      100000,  0,  "days", 273, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray5",    T_BOOL, 0,       0,      1,       0,  "",     274, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray5",   T_BOOL, 0,       0,      1,       0,  "",     275, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray6",     T_U16,  150,     0,      500,     0,  "L",    276, "litres to flood this tray in summer",    ""),
    P("irr_days_tray6",       T_U8,   14,      1,      60,      0,  "days", 277, "days between waterings in summer",       ""),
    P("irr_last_tray6",       T_U32,  0,       0,      100000,  0,  "days", 278, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray6",    T_BOOL, 0,       0,      1,       0,  "",     279, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray6",   T_BOOL, 0,       0,      1,       0,  "",     280, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray7",     T_U16,  150,     0,      500,     0,  "L",    281, "litres to flood this tray in summer",    ""),
    P("irr_days_tray7",       T_U8,   14,      1,      60,      0,  "days", 282, "days between waterings in summer",       ""),
    P("irr_last_tray7",       T_U32,  0,       0,      100000,  0,  "days", 283, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray7",    T_BOOL, 0,       0,      1,       0,  "",     284, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray7",   T_BOOL, 0,       0,      1,       0,  "",     285, "force skipping this tray next cycle",    ""),
    P("irr_liters_tray8",     T_U16,  150,     0,      500,     0,  "L",    286, "litres to flood this tray in summer",    ""),
    P("irr_days_tray8",       T_U8,   14,      1,      60,      0,  "days", 287, "days between waterings in summer",       ""),
    P("irr_last_tray8",       T_U32,  0,       0,      100000,  0,  "days", 288, "last watering (days since 1 Jan 2020)",  ""),
    P("irr_forceOn_tray8",    T_BOOL, 0,       0,      1,       0,  "",     289, "force watering this tray next cycle",    ""),
    P("irr_forceOff_tray8",   T_BOOL, 0,       0,      1,       0,  "",     290, "force skipping this tray next cycle",    ""),
    P("irr_dryEwmaDays",      T_U16,  3,       1,      30,      0,  "days", 291, "days of dryness history set the dose",   "sampled every 2 h"),
    P("irr_dryLo_kPa",        T_F32,  0.15,    0.0,    2.0,     2,  "kPa",  292, "air dryness giving the least water",     ""),
    P("irr_dryHi_kPa",        T_F32,  1.2,     0.0,    5.0,     2,  "kPa",  293, "air dryness giving the most water",      "> irr_dryLo_kPa"),
    P("irr_modMin",           T_F32,  0.25,    0.1,    1.0,     2,  "",     294, "least water given, in humid weather",    "the floor even in still, humid air"),
    P("irr_modGamma",         T_F32,  1.0,     1.0,    4.0,     2,  "",     295, "how fast watering ramps up with dryness",""),
    P("irr_volFreqSplit",     T_F32,  0.5,     0.0,    1.0,     2,  "",     296, "cut the volume, or stretch the period",  "0 = period, 1 = volume"),
    P("irr_traysPerDay",      T_U16,  2,       1,      5,       0,  "",     297, "most trays watered in one day",          ""),
    P("irr_dawnGate_degC",    T_F32,  12.0,    5.0,    30.0,    1,  "degC", 298, "skip pre-dawn watering below this",      ""),
    P("irr_maxFill_sec",      T_U16,  300,     100,    1800,    0,  "sec",  299, "alarm if a tray takes longer to fill",   ""),
    P("irr_drainWindow_sec",  T_U16,  300,     100,    1800,    0,  "sec",  300, "time allowed for a tray to drain",       ""),
    P("irr_expDrainFrac",     T_F32,  0.6,     0.0,    1.0,     2,  "",     301, "fraction expected to drain back",        ""),
    P("irr_drainMargin",      T_F32,  0.3,     0.0,    1.0,     2,  "",     302, "drain fraction below this is a clog",    ""),
    P("irr_flush_sec",        T_U16,  10,      5,      300,     0,  "sec",  303, "flush duration around the cycle",        ""),
    P("irr_input_qtyPerImp",  T_F32,  1.0,     0.01,   10.0,    2,  "L",    304, "litres per pulse from the water meter",  "per meter datasheet"),
    P("irr_out_rawMin_uA",    T_U16,  0,       0,      24000,   0,  "uA",   305, "output meter uA at zero flow",           "per output-meter datasheet"),
    P("irr_out_rawMax_uA",    T_U16,  10000,   0,      24000,   0,  "uA",   306, "output meter uA at full scale",          "per output-meter datasheet"),
    P("irr_out_engMin_Lpm",   T_F32,  0.0,     0.0,    500.0,   1,  "Lpm",  307, "output litres/min at zero signal",       "per output-meter datasheet"),
    P("irr_out_engMax_Lpm",   T_F32,  50.0,    0.0,    500.0,   1,  "Lpm",  308, "output litres/min at full scale",        "per output-meter datasheet"),
    P("irr_out_min",          T_F32,  0.0,     0.0,    500.0,   1,  "Lpm",  309, "lowest believable output flow",          "under this: a dead sensor"),
    P("irr_out_max",          T_F32,  50.0,    0.0,    500.0,   1,  "Lpm",  310, "highest believable output flow",         "over this: a dead sensor"),
    P("irr_out_avg_ms",       T_U16,  200,     0,      10000,   0,  "ms",   311, "rolling average for this flow meter",    "per output-meter datasheet"),
    P("pwr_dc1_rawMin_mV",    T_U16,  4000,    0,      10000,   0,  "mV",   400, "DC PS1 raw value at the low end",        "per divider setup"),
    P("pwr_dc1_rawMax_mV",    T_U16,  6000,    0,      10000,   0,  "mV",   401, "DC PS1 raw value at the high end",       "per divider setup"),
    P("pwr_dc1_engMin_V",     T_F32,  20.0,    0.0,    48.0,    1,  "V",    402, "DC PS1 volts at the low end",            "per divider setup"),
    P("pwr_dc1_engMax_V",     T_F32,  30.0,    0.0,    48.0,    1,  "V",    403, "DC PS1 volts at the high end",           "per divider setup"),
    P("pwr_dc1_min",          T_F32,  20.0,    0.0,    48.0,    1,  "V",    404, "lowest believable dc ps1 volts",         "under this: a dead sensor"),
    P("pwr_dc1_max",          T_F32,  30.0,    0.0,    48.0,    1,  "V",    405, "highest believable dc ps1 volts",        "over this: a dead sensor"),
    P("pwr_dc1_avg_ms",       T_U16,  1000,    10,     10000,   0,  "ms",   406, "rolling average for DC PS1",             "per divider setup"),
    P("pwr_dc2_rawMin_mV",    T_U16,  4000,    0,      10000,   0,  "mV",   407, "DC PS2 raw value at the low end",        "per divider setup"),
    P("pwr_dc2_rawMax_mV",    T_U16,  6000,    0,      10000,   0,  "mV",   408, "DC PS2 raw value at the high end",       "per divider setup"),
    P("pwr_dc2_engMin_V",     T_F32,  20.0,    0.0,    48.0,    1,  "V",    409, "DC PS2 volts at the low end",            "per divider setup"),
    P("pwr_dc2_engMax_V",     T_F32,  30.0,    0.0,    48.0,    1,  "V",    410, "DC PS2 volts at the high end",           "per divider setup"),
    P("pwr_dc2_min",          T_F32,  20.0,    0.0,    48.0,    1,  "V",    411, "lowest believable dc ps2 volts",         "under this: a dead sensor"),
    P("pwr_dc2_max",          T_F32,  30.0,    0.0,    48.0,    1,  "V",    412, "highest believable dc ps2 volts",        "over this: a dead sensor"),
    P("pwr_dc2_avg_ms",       T_U16,  1000,    10,     10000,   0,  "ms",   413, "rolling average for DC PS2",             "per divider setup"),
    P("pwr_upsBat_rawMin_mV", T_U16,  4000,    0,      10000,   0,  "mV",   414, "UPS battery raw value at the low end",   "per divider setup"),
    P("pwr_upsBat_rawMax_mV", T_U16,  6000,    0,      10000,   0,  "mV",   415, "UPS battery raw value at the high end",  "per divider setup"),
    P("pwr_upsBat_engMin_V",  T_F32,  20.0,    0.0,    48.0,    1,  "V",    416, "UPS battery volts at the low end",       "per divider setup"),
    P("pwr_upsBat_engMax_V",  T_F32,  30.0,    0.0,    48.0,    1,  "V",    417, "UPS battery volts at the high end",      "per divider setup"),
    P("pwr_upsBat_min",       T_F32,  20.0,    0.0,    48.0,    1,  "V",    418, "lowest believable ups battery volts",    "under this: a dead sensor"),
    P("pwr_upsBat_max",       T_F32,  30.0,    0.0,    48.0,    1,  "V",    419, "highest believable ups battery volts",   "over this: a dead sensor"),
    P("pwr_upsBat_avg_ms",    T_U16,  1000,    10,     10000,   0,  "ms",   420, "rolling average for the UPS battery",    "per divider setup"),
    P("pwr_genBat_rawMin_mV", T_U16,  5000,    0,      10000,   0,  "mV",   421, "gen. battery raw value at the low end",  "per divider setup"),
    P("pwr_genBat_rawMax_mV", T_U16,  7500,    0,      10000,   0,  "mV",   422, "gen. battery raw value at the high end", "per divider setup"),
    P("pwr_genBat_engMin_V",  T_F32,  20.0,    10.0,   48.0,    1,  "V",    423, "gen. battery volts at the low end",      "per divider setup"),
    P("pwr_genBat_engMax_V",  T_F32,  30.0,    15.0,   48.0,    1,  "V",    424, "gen. battery volts at the high end",     "per divider setup"),
    P("pwr_genBat_min",       T_F32,  20.0,    10.0,   48.0,    1,  "V",    425, "lowest believable gen. battery volts",   "under this: a dead sensor"),
    P("pwr_genBat_max",       T_F32,  30.0,    10.0,   48.0,    1,  "V",    426, "highest believable gen. battery volts",  "over this: a dead sensor"),
    P("pwr_genBat_avg_ms",    T_U16,  1000,    10,     10000,   0,  "ms",   427, "rolling average for the Gen. battery",   "per divider setup"),

    // The net_ / ntp_ / mqtt_ / geo_ parameters are the LIBRARY's rows, spliced
    // in above by BOTANIK_LIBRARY_VARS. This site pins their values in config.h.

/** @} */

/** @name Sensor inputs - read from the field, invalid past their timeout
 *  @{ */

    // name              type   dev fc reg  n  min                max               fallback  timeout dgt unit     description                           notes
    MI("grid_L1_V",       T_F32, 5, 4,   0, 2, SF(0.0f),          SF(500.0f),       SF(0.0f), 15000,  1, "V",     "L1 phase voltage",                   "SDM630 p1 30001", 10000),
    MI("grid_L2_V",       T_F32, 5, 4,   2, 2, SF(0.0f),          SF(500.0f),       SF(0.0f), 15000,  1, "V",     "L2 phase voltage",                   "SDM630 p2 30003", 10000),
    MI("grid_L3_V",       T_F32, 5, 4,   4, 2, SF(0.0f),          SF(500.0f),       SF(0.0f), 15000,  1, "V",     "L3 phase voltage",                   "SDM630 p3 30005", 10000),
    MI("grid_L1_A",       T_F32, 5, 4,   6, 2, SF(0.0f),          SF(200.0f),       SF(0.0f), 15000,  1, "A",     "L1 current",                         "SDM630 p4 30007", 10000),
    MI("grid_L2_A",       T_F32, 5, 4,   8, 2, SF(0.0f),          SF(200.0f),       SF(0.0f), 15000,  1, "A",     "L2 current",                         "SDM630 p5 30009", 10000),
    MI("grid_L3_A",       T_F32, 5, 4,  10, 2, SF(0.0f),          SF(200.0f),       SF(0.0f), 15000,  1, "A",     "L3 current",                         "SDM630 p6 30011", 10000),
    MI("grid_L1_VA",      T_F32, 5, 4,  18, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VA",    "L1 apparent power",                  "SDM630 p10 30019", 10000),
    MI("grid_L2_VA",      T_F32, 5, 4,  20, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VA",    "L2 apparent power",                  "SDM630 p11 30021", 10000),
    MI("grid_L3_VA",      T_F32, 5, 4,  22, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VA",    "L3 apparent power",                  "SDM630 p12 30023", 10000),
    MI("grid_L1_VAr",     T_F32, 5, 4,  24, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VAr",   "L1 reactive power",                  "SDM630 p13 30025", 10000),
    MI("grid_L2_VAr",     T_F32, 5, 4,  26, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VAr",   "L2 reactive power",                  "SDM630 p14 30027", 10000),
    MI("grid_L3_VAr",     T_F32, 5, 4,  28, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VAr",   "L3 reactive power",                  "SDM630 p15 30029", 10000),
    MI("grid_L1_PF",      T_F32, 5, 4,  30, 2, SF(-1.0f),         SF(1.0f),         SF(0.0f), 15000,  3, "",      "L1 power factor",                    "SDM630 p16 30031", 10000),
    MI("grid_L2_PF",      T_F32, 5, 4,  32, 2, SF(-1.0f),         SF(1.0f),         SF(0.0f), 15000,  3, "",      "L2 power factor",                    "SDM630 p17 30033", 10000),
    MI("grid_L3_PF",      T_F32, 5, 4,  34, 2, SF(-1.0f),         SF(1.0f),         SF(0.0f), 15000,  3, "",      "L3 power factor",                    "SDM630 p18 30035", 10000),
    MI("grid_L1_PA",      T_F32, 5, 4,  36, 2, SF(-180.0f),       SF(180.0f),       SF(0.0f), 15000,  1, "deg",   "L1 phase angle",                     "SDM630 p19 30037", 10000),
    MI("grid_L2_PA",      T_F32, 5, 4,  38, 2, SF(-180.0f),       SF(180.0f),       SF(0.0f), 15000,  1, "deg",   "L2 phase angle",                     "SDM630 p20 30039", 10000),
    MI("grid_L3_PA",      T_F32, 5, 4,  40, 2, SF(-180.0f),       SF(180.0f),       SF(0.0f), 15000,  1, "deg",   "L3 phase angle",                     "SDM630 p21 30041", 10000),
    MI("grid_LN_V",       T_F32, 5, 4,  42, 2, SF(0.0f),          SF(500.0f),       SF(0.0f), 15000,  1, "V",     "average line-to-neutral voltage",    "SDM630 p22 30043", 10000),
    MI("grid_LL_V",       T_F32, 5, 4, 206, 2, SF(0.0f),          SF(900.0f),       SF(0.0f), 15000,  1, "V",     "average line-to-line voltage",       "SDM630 p104 30207", 10000),
    MI("grid_avg_A",      T_F32, 5, 4,  46, 2, SF(0.0f),          SF(200.0f),       SF(0.0f), 15000,  1, "A",     "average current",                    "SDM630 p24 30047", 10000),
    MI("grid_total_A",    T_F32, 5, 4,  48, 2, SF(0.0f),          SF(600.0f),       SF(0.0f), 15000,  1, "A",     "total current",                      "SDM630 p25 30049", 10000),
    MI("grid_total_W",    T_F32, 5, 4,  52, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "W",     "total active power",                 "SDM630 p27 30053", 10000),
    MI("grid_total_VA",   T_F32, 5, 4,  56, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VA",    "total apparent power",               "SDM630 p29 30057", 10000),
    MI("grid_total_VAr",  T_F32, 5, 4,  60, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VAr",   "total reactive power",               "SDM630 p31 30061", 10000),
    MI("grid_total_PF",   T_F32, 5, 4,  62, 2, SF(-1.0f),         SF(1.0f),         SF(0.0f), 15000,  3, "",      "total power factor",                 "SDM630 p32 30063", 10000),
    MI("grid_total_PA",   T_F32, 5, 4,  66, 2, SF(-180.0f),       SF(180.0f),       SF(0.0f), 15000,  1, "deg",   "total phase angle",                  "SDM630 p34 30067", 10000),
    MI("grid_Hz",         T_F32, 5, 4,  70, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 15000,  2, "Hz",    "line frequency",                     "SDM630 p36 30071", 10000),
    MI("grid_L12_V",      T_F32, 5, 4, 200, 2, SF(0.0f),          SF(900.0f),       SF(0.0f), 15000,  1, "V",     "L1-L2 voltage",                      "SDM630 p101 30201", 10000),
    MI("grid_L23_V",      T_F32, 5, 4, 202, 2, SF(0.0f),          SF(900.0f),       SF(0.0f), 15000,  1, "V",     "L2-L3 voltage",                      "SDM630 p102 30203", 10000),
    MI("grid_L31_V",      T_F32, 5, 4, 204, 2, SF(0.0f),          SF(900.0f),       SF(0.0f), 15000,  1, "V",     "L3-L1 voltage",                      "SDM630 p103 30205", 10000),
    MI("grid_LL_avg_THD",  T_F32, 5, 4, 340, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 15000,  1, "%",     "line-to-line average THD",           "SDM630 p171 30341", 10000),
    MI("grid_LN_avg_THD",  T_F32, 5, 4, 248, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 15000,  1, "%",     "line-to-neutral average THD",        "SDM630 p125 30249", 10000),
    MI("grid_N_A",        T_F32, 5, 4, 224, 2, SF(0.0f),          SF(200.0f),       SF(0.0f), 15000,  1, "A",     "neutral current",                    "SDM630 p113 30225", 10000),
    MI("grid_L1_W",       T_F32, 5, 4,  12, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "W",     "L1 active power",                    "SDM630 p7 30013", 10000),
    MI("grid_L2_W",       T_F32, 5, 4,  14, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "W",     "L2 active power",                    "SDM630 p8 30015", 10000),
    MI("grid_L3_W",       T_F32, 5, 4,  16, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "W",     "L3 active power",                    "SDM630 p9 30017", 10000),
    MI("ats_V",           T_F32, 6, 4,   0, 2, SF(0.0f),          SF(500.0f),       SF(0.0f), 15000,  1, "V",     "ATS voltage",                        "MCT1-140 0x0000", 10000),
    MI("ats_A",           T_F32, 6, 4,   2, 2, SF(0.0f),          SF(200.0f),       SF(0.0f), 15000,  1, "A",     "ATS current",                        "MCT1-140 0x0002", 10000),
    MI("ats_W",           T_F32, 6, 4,   4, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "W",     "ATS active power",                   "MCT1-140 0x0004", 10000),
    MI("ats_VAr",         T_F32, 6, 4,   6, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VAr",   "ATS reactive power",                 "MCT1-140 0x0006", 10000),
    MI("ats_VA",          T_F32, 6, 4,   8, 2, SF(-1000000.0f),   SF(1000000.0f),   SF(0.0f), 15000,  0, "VA",    "ATS apparent power",                 "MCT1-140 0x0008", 10000),
    MI("ats_Hz",          T_F32, 6, 4,  12, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 15000,  2, "Hz",    "ATS frequency",                      "MCT1-140 0x000C", 10000),

    // Temperature / humidity, 12 sensors on 4 groups of 3. Each device is one
    // block: an f32 pair at 0 and another at 2, contiguous, so a sensor costs
    // one transaction rather than two.
    MI("cli_temp_ext1",   T_F32, 41, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "exterior temperature 1",              "temp/RH sensor 0x0000"),
    MI("cli_rh_ext1",   T_F32, 41, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "exterior humidity 1",                 "temp/RH sensor 0x0002"),
    MI("cli_temp_ext2",   T_F32, 42, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "exterior temperature 2",              "temp/RH sensor 0x0000"),
    MI("cli_rh_ext2",   T_F32, 42, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "exterior humidity 2",                 "temp/RH sensor 0x0002"),
    MI("cli_temp_ext3",   T_F32, 43, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "exterior temperature 3",              "temp/RH sensor 0x0000"),
    MI("cli_rh_ext3",   T_F32, 43, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "exterior humidity 3",                 "temp/RH sensor 0x0002"),
    MI("cli_temp_cs1",   T_F32, 44, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "crawlspace temperature 1",            "temp/RH sensor 0x0000"),
    MI("cli_rh_cs1",    T_F32, 44, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "crawlspace humidity 1",               "temp/RH sensor 0x0002"),
    MI("cli_temp_cs2",   T_F32, 45, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "crawlspace temperature 2",            "temp/RH sensor 0x0000"),
    MI("cli_rh_cs2",    T_F32, 45, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "crawlspace humidity 2",               "temp/RH sensor 0x0002"),
    MI("cli_temp_cs3",   T_F32, 46, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "crawlspace temperature 3",            "temp/RH sensor 0x0000"),
    MI("cli_rh_cs3",    T_F32, 46, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "crawlspace humidity 3",               "temp/RH sensor 0x0002"),
    MI("cli_temp_lo1",   T_F32, 47, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "low level temperature 1",             "temp/RH sensor 0x0000"),
    MI("cli_rh_lo1",    T_F32, 47, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "low level humidity 1",                "temp/RH sensor 0x0002"),
    MI("cli_temp_lo2",   T_F32, 48, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "low level temperature 2",             "temp/RH sensor 0x0000"),
    MI("cli_rh_lo2",    T_F32, 48, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "low level humidity 2",                "temp/RH sensor 0x0002"),
    MI("cli_temp_lo3",   T_F32, 49, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "low level temperature 3",             "temp/RH sensor 0x0000"),
    MI("cli_rh_lo3",    T_F32, 49, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "low level humidity 3",                "temp/RH sensor 0x0002"),
    MI("cli_temp_hi1",   T_F32, 50, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "high level temperature 1",            "temp/RH sensor 0x0000"),
    MI("cli_rh_hi1",    T_F32, 50, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "high level humidity 1",               "temp/RH sensor 0x0002"),
    MI("cli_temp_hi2",   T_F32, 51, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "high level temperature 2",            "temp/RH sensor 0x0000"),
    MI("cli_rh_hi2",    T_F32, 51, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "high level humidity 2",               "temp/RH sensor 0x0002"),
    MI("cli_temp_hi3",   T_F32, 52, 4,   0, 2, SF(-40.0f),        SF(80.0f),        SF(0.0f), 5000,  1, "C",     "high level temperature 3",            "temp/RH sensor 0x0000"),
    MI("cli_rh_hi3",    T_F32, 52, 4,   2, 2, SF(0.0f),          SF(100.0f),       SF(0.0f), 5000,  1, "%",     "high level humidity 3",               "temp/RH sensor 0x0002"),

 // TDC/AI-1 - calibrated 0-10 V / 4-20 mA channels. The wire unit is the module's
 // (mV, uA); the calibration block turns it into the engineering one, and every
 // field of it is a parameter, so a channel is re-trimmed from PARAMS.

    // name              calibration        dev fc ch n  wire   fallback  timeout dgt unit   description                notes
    AI("pwr_genBat_V",   &CAL_PWR_GEN_BAT,  11, 4, 0, 1, T_U16, SF(0.0f), 5000,   1,  "V",   "generator start battery", ""),
    AI("pwr_upsBat_V",   &CAL_PWR_UPS_BAT,  11, 4, 1, 1, T_U16, SF(0.0f), 5000,   1,  "V",   "UPS battery voltage",     ""),
    AI("pwr_dc1_V",      &CAL_PWR_DC1,      11, 4, 2, 1, T_U16, SF(0.0f), 5000,   1,  "V",   "DC PS1 bus voltage",      ""),
    AI("pwr_dc2_V",      &CAL_PWR_DC2,      11, 4, 3, 1, T_U16, SF(0.0f), 5000,   1,  "V",   "DC PS2 bus voltage",      ""),
    AI("irr_out_Lpm",    &CAL_IRR_OUTPUT,   11, 4, 4, 1, T_U16, SF(0.0f), 5000,   1,  "Lpm", "irrigation output flow",  ""),

/** @} */

/** @name Alarms - each row IS its condition: the boolean plus the announce policy
 *  @{ */

    //name                       code        on off rec dly mn reg  description                        remediation
    A("alarm_ac_grid",           "AC_GRID",  2, 2, 0, 0, 10, 800, "grid AC out-of-spec/offline",     "check mains breaker / utility feed"),
    A("alarm_dc_ps1",            "DC_PS1",   2, 2, 0, 0, 0,  801, "DC PS1 volts too low",            ""),
    A("alarm_gen",               "GEN_FLT",  1, 2, 0, 0, 10, 802, "generator failed to start",       "inspect generator + start battery"),
    A("alarm_ac_local",          "AC_LOCAL", 2, 2, 0, 0, 0,  803, "local AC out of spec or offline", ""),
    A("alarm_dc_ps2",            "DC_PS2",   2, 2, 0, 0, 10, 804, "DC PS2 volts too low",            ""),
    A("alarm_dc_ups",            "DC_UPS",   1, 2, 0, 0, 10, 805, "DC UPS on battery",               "restore mains; check UPS battery"),
    A("alarm_dc_local",          "DC_LOCAL", 2, 2, 0, 0, 0,  806, "local DC volts too low",          ""),
    A("alarm_generator_battery", "GEN_BATT", 5, 5, 0, 0, 0,  807, "gen. start battery low",          ""),
    A("alarm_net_fault",         "NET_FLT",  5, 5, 0, 0, 10, 809, "network error",                   ""),
    A("alarm_ntp_timeout",       "NTP_TIM",  5, 5, 0, 0, 10, 810, "NTP sync timeout",                ""),
    A("alarm_actuator_fault",    "ACT_FLT",  2, 2, 0, 0, 0,  811, "actuator failure",                ""),
    A("alarm_sensor_fault",      "SNS_FLT",  2, 2, 0, 0, 0,  812, "sensor fault",                    ""),
    A("alarm_too_hot",           "TOO_HOT",  3, 3, 0, 0, 15, 813, "inside temperature too high",     "increase ventilation / open windows"),
    A("alarm_too_cold",          "TOO_COLD", 3, 3, 0, 0, 15, 814, "inside temperature too low",      ""),
    A("alarm_too_humid",         "TOO_HUM",  3, 3, 0, 0, 15, 815, "relative humidity too high",      ""),
    A("alarm_tmp_str",           "TMP_STR",  5, 5, 0, 0, 0,  816, "temp. stratification too high",   ""),
    A("alarm_water_leak",        "WTR_LK",   1, 2, 0, 0, 0,  817, "leak in irrigation circuit",      "shut irrigation valve, inspect circuit"),
    A("alarm_water_pressure",    "WTR_BAR",  2, 2, 0, 0, 0,  818, "water pressure too low",          ""),
    A("alarm_water_clog",        "WTR_CLOG", 3, 3, 0, 0, 0,  819, "failing to drain a tray",         ""),
    A("alarm_io_drop",           "IO_DROP",  2, 2, 0, 0, 0,  820, "logical I/O drops",               ""),
    A("alarm_log_flood",         "LOG_FLD",  2, 2, 0, 0, 0,  821, "logs flooding",                   ""),
    A("alarm_weather_storm",     "WH_STORM", 2, 5, 0, 0, 15, 822, "current storm conditions",        "close roof windows, secure screens"),
/** @} */

/** @name Status values - published over MQTT and the Modbus TCP slave
 *
 *  Hand-assigned from a range of their own, so the parameter block above can
 *  grow without renumbering anything an external master holds.
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
    RO("cli_skirtValve",      34, 5,   false, false, "wet skirt valve",                "pulsed: humidify effort, or cooling"),
    RO("irr_tray7",           34, 6,   true,  true,  "tray 7 valve",                   ""),
    RO("irr_tray8",           34, 7,   true,  true,  "tray 8 valve",                   ""),

 // TDC/AO-1 - 0-10 V drives. The conversion runs the other way (engineering value
 // to raw count), and the safe state is a stopped drive.

    // name             calibration      dev fc ch n  wire   safe digits unit description          notes
    AO("cli_intr1",     &CAL_INTRACTOR,  16, 6, 0, 1, T_U16, 0.0f, 2,    "",  "intractor 1 speed", "0-1 of full speed"),
    AO("cli_intr2",     &CAL_INTRACTOR,  16, 6, 1, 1, T_U16, 0.0f, 2,    "",  "intractor 2 speed", "shares intractor 1's calibration"),

/** @} */

};

const uint16_t g_var_count = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));


/** @brief How many rows the table has, derived from the table itself.
 *  Everything else addresses a row by NAME: see Bank::find(). */
constexpr uint16_t VAR_TOTAL = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));

/** @name Home screen - the one table no rule derives
 *
 *  A variable's group and whether it is published follow from its name and
 *  scope. A home field does not: the caption is a person's choice and nothing in
 *  the name says the row belongs on the landing page, so it is declared.
 *  @{ */

/** @brief Home-screen readings: label + variable. Laid out in three columns of
 *  8, column-major; column 1 holds 6 (its top two cells carry the uptime readout
 *  and a spacer). The geometry caps labels at 11 chars and values at 13. */
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

// The home screen silently ignores fields past its columns, so pin the count.
static_assert(sizeof(g_home_fields) / sizeof(g_home_fields[0]) <= Config::HOME_FIELDS_MAX,
              "too many home fields: the HomePage would silently drop the tail");

/** @name Value store (RAM, parallel to the table)
 *
 *  Header-only through one trick: a namespace-scope array would be a DEFINITION
 *  in every including TU and the link would break on the second. C++11's
 *  equivalent of C++17 `inline` is a class template's static member, which has
 *  vague linkage. Its address is still a constant expression, so SITE_VAR_TABLES
 *  below stays constant-initialised into flash.
 *  @{ */

/** @brief The site's mutable state. @c Tag is unused: the template exists only
 *  for vague linkage, and @c Store is the one instantiation. */
template <typename Tag>
struct SiteStore {
    /** @brief One slot per row of g_vars, addressed the same way. */
    static Value  values[VAR_TOTAL];
    /** @brief T_STR live text: one STR_CAP slot per text parameter, addressed by
     *  the row's TextSlot offset. */
    static char   pool[STR_POOL_BYTES];
    /** @brief Scratch for the bank's name index: one row number per row, sorted
     *  by name at construction so find() is a binary search. */
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

// A short pool would let one text parameter's slot overlap its neighbour.
static_assert(STR_POOL_BYTES >= 8 * STR_CAP, "the string pool is too small");

}  // namespace Site

#endif // SITE_VARS_H
