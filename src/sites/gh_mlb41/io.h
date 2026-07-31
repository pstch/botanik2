#ifndef SITE_IO_H
#define SITE_IO_H

/**
 * @file io.h
 * @brief GENERATED - the gh_mlb41 field IO: Modbus RTU variables and their
 *        value stores.
 *
 * Generated from the site's Modbus device map. Regenerating overwrites this
 * file.
 *
 * Bus: RS485, ModbusRTU master, 38400 baud, none/8/1, on COM2.
 * 8 field devices, 71 mapped signals, 20 free channels.
 * (70 in the published device map, plus MTR-2 object 2 - see below.)
 *
 *   SRR/TAC/MTR-1  Eastron SDM630    addr 5   37 analog in
 *   SRR/TAC/MTR-2  Lettel MCT1-140   addr 6    6 analog in
 *   SRR/TDC/AI-1   Waveshare 25767   addr 11   8 free  (unmapped)
 *   SRR/TDC/AO-1   Waveshare 26211   addr 16   8 free  (unmapped)
 *   SRR/TDC/RO-1   Waveshare 26517   addr 31   6 coils out (+2 free)
 *   SRR/TDC/RO-2   Waveshare 26517   addr 32   6 coils out (+2 free)
 *   SRR/TDC/RO-3   Waveshare 26517   addr 33   8 coils out
 *   SRR/TDC/RO-4   Waveshare 26517   addr 34   8 coils out
 *
 * The two free modules carry no variables, so this site has no analog outputs
 * and no digital inputs yet; their tables are empty rather than absent, so a
 * later mapping is purely additive.
 *
 * @warning IoBinding::reg holds the FIELD-DEVICE address: the coil number on
 * the relay boards, and the device-profile OBJECT INDEX on the meters. The raw
 * holding register is fixed by the device template, so the RTU master must
 * translate object index -> register per device model. It is not the register
 * column of the device map - that one is the Opta's own Modbus *slave* address,
 * which belongs in the slave map, not here.
 */

#include <cstdint>

#include "botanik.h"

namespace Site {

using namespace Botanik;
using namespace Botanik::Vars;

// Meter reading: a REAL over Modbus, read by object index. Bands are generous
// on purpose - they exist to catch a dead or unplugged meter reporting a rail,
// not to police the process.
// `dgt` is the display precision: a power factor rendered to 0 decimals is
// useless, a watt reading to 2 is noise.
#define MTR(nm, unit, dgt, dev, obj, lo, hi, desc) \
    { {nm, unit, desc, "", "0", dgt, nullptr}, {IO_MODBUS_RTU, dev, obj, 3}, \
      SCALAR_F32, 1.0f, 0.0f, lo, hi, 5000 }

// Relay coil. Default "0" is the safe state: every actuator de-energises on a
// fault, which for windows, valves and fans is the direction that does no harm.
#define COIL(nm, dev, coil, desc) \
    { {nm, "", desc, "", "0", 0, nullptr}, {IO_MODBUS_RTU, dev, coil, 5}, false }

/** @name Analog inputs (43)
 *
 *  Plain `const`, so each is internal to the including TU. Two TUs include this
 *  site in the firmware build, so the tables cost ~2.8 KB of flash twice. Moving
 *  the rows into io.cpp behind an `extern const` declaration would fix that; it
 *  is deferred because it restructures the generated file, and 2.8 KB against a
 *  1.8 MB budget is not yet worth the churn.
 *  @{ */

const AnalogInVar g_analog_in[] = {
    // -- SRR/TAC/MTR-1, Eastron SDM630, addr 5: three-phase power meter --
    MTR("pwr_L1_V",        "V", 1,   5,  0, 0.0f,   500.0f, "L1 phase voltage"),
    MTR("pwr_L2_V",        "V", 1,   5,  1, 0.0f,   500.0f, "L2 phase voltage"),
    MTR("pwr_L3_V",        "V", 1,   5,  2, 0.0f,   500.0f, "L3 phase voltage"),
    MTR("pwr_L1_A",        "A", 1,   5,  3, 0.0f,   200.0f, "L1 current"),
    MTR("pwr_L2_A",        "A", 1,   5,  4, 0.0f,   200.0f, "L2 current"),
    MTR("pwr_L3_A",        "A", 1,   5,  5, 0.0f,   200.0f, "L3 current"),
    MTR("pwr_L1_VA",       "VA", 0,  5,  6, -1e6f,  1e6f,   "L1 apparent power"),
    MTR("pwr_L2_VA",       "VA", 0,  5,  7, -1e6f,  1e6f,   "L2 apparent power"),
    MTR("pwr_L3_VA",       "VA", 0,  5,  8, -1e6f,  1e6f,   "L3 apparent power"),
    MTR("pwr_L1_VAr",      "VAr", 0, 5,  9, -1e6f,  1e6f,   "L1 reactive power"),
    MTR("pwr_L2_VAr",      "VAr", 0, 5, 10, -1e6f,  1e6f,   "L2 reactive power"),
    MTR("pwr_L3_VAr",      "VAr", 0, 5, 11, -1e6f,  1e6f,   "L3 reactive power"),
    MTR("pwr_P1_PF",       "", 3,    5, 12, -1.0f,  1.0f,   "L1 power factor"),
    MTR("pwr_P2_PF",       "", 3,    5, 13, -1.0f,  1.0f,   "L2 power factor"),
    MTR("pwr_P3_PF",       "", 3,    5, 14, -1.0f,  1.0f,   "L3 power factor"),
    MTR("pwr_P1_PA",       "deg", 1, 5, 15, -180.0f, 180.0f, "L1 phase angle"),
    MTR("pwr_P2_PA",       "deg", 1, 5, 16, -180.0f, 180.0f, "L2 phase angle"),
    MTR("pwr_P3_PA",       "deg", 1, 5, 17, -180.0f, 180.0f, "L3 phase angle"),
    MTR("pwr_LN_V",        "V", 1,   5, 18, 0.0f,   500.0f, "average line-to-neutral voltage"),
    MTR("pwr_LL_V",        "V", 1,   5, 19, 0.0f,   900.0f, "average line-to-line voltage"),
    MTR("pwr_avg_A",       "A", 1,   5, 20, 0.0f,   200.0f, "average current"),
    MTR("pwr_total_A",     "A", 1,   5, 21, 0.0f,   600.0f, "total current"),
    MTR("pwr_total_W",     "W", 0,   5, 22, -1e6f,  1e6f,   "total active power"),
    MTR("pwr_total_VA",    "VA", 0,  5, 23, -1e6f,  1e6f,   "total apparent power"),
    MTR("pwr_total_VAR",   "VAr", 0, 5, 24, -1e6f,  1e6f,   "total reactive power"),
    MTR("pwr_total_PF",    "", 3,    5, 25, -1.0f,  1.0f,   "total power factor"),
    MTR("pwr_total_PA",    "deg", 1, 5, 26, -180.0f, 180.0f, "total phase angle"),
    MTR("pwr_Hz",          "Hz", 2,  5, 27, 0.0f,   100.0f, "line frequency"),
    MTR("pwr_12_V",        "V", 1,   5, 28, 0.0f,   900.0f, "L1-L2 voltage"),
    MTR("pwr_L23_V",       "V", 1,   5, 29, 0.0f,   900.0f, "L2-L3 voltage"),
    MTR("pwr_L31_V",       "V", 1,   5, 30, 0.0f,   900.0f, "L3-L1 voltage"),
    MTR("pwr_LL_avg_THD",  "%", 1,   5, 31, 0.0f,   100.0f, "line-to-line average THD"),
    MTR("pwr_LN_avg_THD",  "%", 1,   5, 32, 0.0f,   100.0f, "line-to-neutral average THD"),
    MTR("pwr_N_A",         "A", 1,   5, 33, 0.0f,   200.0f, "neutral current"),
    MTR("pwr_L1_W",        "W", 0,   5, 34, -1e6f,  1e6f,   "L1 active power"),
    MTR("pwr_L2_W",        "W", 0,   5, 35, -1e6f,  1e6f,   "L2 active power"),
    MTR("pwr_L3_W",        "W", 0,   5, 36, -1e6f,  1e6f,   "L3 active power"),

    // -- SRR/TAC/MTR-2, Lettel MCT1-140, addr 6: single-phase meter --
    // Object 2 (ats_W) is not in the published device map, which lists this
    // meter as 5 signals; it is mapped here deliberately.
    MTR("ats_V",           "V", 1,   6,  0, 0.0f,   500.0f, "ATS voltage"),
    MTR("ats_A",           "A", 1,   6,  1, 0.0f,   200.0f, "ATS current"),
    MTR("ats_W",           "W", 0,   6,  2, 0.0f,   1000.0f, "ATS active power"),
    MTR("ats_VAr",         "VAr", 0, 6,  3, -1e6f,  1e6f,   "ATS reactive power"),
    MTR("ats_VA",          "VA", 0,  6,  4, -1e6f,  1e6f,   "ATS apparent power"),
    MTR("ats_Hz",          "Hz", 2,  6,  5, 0.0f,   100.0f, "ATS frequency"),
};
const uint16_t g_analog_in_count = (uint16_t)(sizeof(g_analog_in) / sizeof(g_analog_in[0]));

/** @brief Symbolic index per analog input, so programs name their signals. */
enum AiId : uint16_t {
    AI_PWR_L1_V, AI_PWR_L2_V, AI_PWR_L3_V,
    AI_PWR_L1_A, AI_PWR_L2_A, AI_PWR_L3_A,
    AI_PWR_L1_VA, AI_PWR_L2_VA, AI_PWR_L3_VA,
    AI_PWR_L1_VAR, AI_PWR_L2_VAR, AI_PWR_L3_VAR,
    AI_PWR_P1_PF, AI_PWR_P2_PF, AI_PWR_P3_PF,
    AI_PWR_P1_PA, AI_PWR_P2_PA, AI_PWR_P3_PA,
    AI_PWR_LN_V, AI_PWR_LL_V, AI_PWR_AVG_A, AI_PWR_TOTAL_A,
    AI_PWR_TOTAL_W, AI_PWR_TOTAL_VA, AI_PWR_TOTAL_VAR,
    AI_PWR_TOTAL_PF, AI_PWR_TOTAL_PA, AI_PWR_HZ,
    AI_PWR_12_V, AI_PWR_L23_V, AI_PWR_L31_V,
    AI_PWR_LL_AVG_THD, AI_PWR_LN_AVG_THD, AI_PWR_N_A,
    AI_PWR_L1_W, AI_PWR_L2_W, AI_PWR_L3_W,
    AI_ATS_V, AI_ATS_A, AI_ATS_W, AI_ATS_VAR, AI_ATS_VA, AI_ATS_HZ,
    AI_COUNT
};

/** @} */

/** @name Digital outputs (28 relay coils)
 *  @{ */

const DigitalOutVar g_digital_out[] = {
    // -- SRR/TDC/RO-1, addr 31 --
    COIL("cli_window1_opn",     31, 0, "window 1 open"),
    COIL("cli_window1_clo",     31, 1, "window 1 close"),
    COIL("irr_input_ro1",       31, 2, "irrigation input valve 1"),
    COIL("irr_output_ro1",      31, 3, "irrigation output valve 1"),
    COIL("irr_tray1",           31, 4, "tray 1 valve"),
    COIL("irr_tray2",           31, 5, "tray 2 valve"),
    // -- SRR/TDC/RO-2, addr 32 --
    COIL("cli_window2_opn",     32, 0, "window 2 open"),
    COIL("cli_window2_clo",     32, 1, "window 2 close"),
    COIL("irr_input_ro2",       32, 2, "irrigation input valve 2"),
    COIL("irr_output_ro2",      32, 3, "irrigation output valve 2"),
    COIL("irr_tray3",           32, 4, "tray 3 valve"),
    COIL("irr_tray4",           32, 5, "tray 4 valve"),
    // -- SRR/TDC/RO-3, addr 33 --
    COIL("cli_window3_opn",     33, 0, "window 3 open"),
    COIL("cli_window3_clo",     33, 1, "window 3 close"),
    COIL("cli_destrat1_speed1", 33, 2, "destratification fan 1 speed 1"),
    COIL("cli_destrat1_speed2", 33, 3, "destratification fan 1 speed 2"),
    COIL("cli_destrat1_speed3", 33, 4, "destratification fan 1 speed 3"),
    COIL("cli_screen",          33, 5, "thermal screen"),
    COIL("irr_tray5",           33, 6, "tray 5 valve"),
    COIL("irr_tray6",           33, 7, "tray 6 valve"),
    // -- SRR/TDC/RO-4, addr 34 --
    COIL("cli_window4_opn",     34, 0, "window 4 open"),
    COIL("cli_window4_clo",     34, 1, "window 4 close"),
    COIL("cli_destrat2_speed1", 34, 2, "destratification fan 2 speed 1"),
    COIL("cli_destrat2_speed2", 34, 3, "destratification fan 2 speed 2"),
    COIL("cli_destrat2_speed3", 34, 4, "destratification fan 2 speed 3"),
    COIL("cli_skirtValve",      34, 5, "wet skirt valve"),
    COIL("irr_tray7",           34, 6, "tray 7 valve"),
    COIL("irr_tray8",           34, 7, "tray 8 valve"),
};
const uint16_t g_digital_out_count = (uint16_t)(sizeof(g_digital_out) / sizeof(g_digital_out[0]));

/** @brief Symbolic index per relay coil. */
enum DoId : uint16_t {
    DO_CLI_WINDOW1_OPN, DO_CLI_WINDOW1_CLO, DO_IRR_INPUT_RO1,
    DO_IRR_OUTPUT_RO1,  DO_IRR_TRAY1,       DO_IRR_TRAY2,
    DO_CLI_WINDOW2_OPN, DO_CLI_WINDOW2_CLO, DO_IRR_INPUT_RO2,
    DO_IRR_OUTPUT_RO2,  DO_IRR_TRAY3,       DO_IRR_TRAY4,
    DO_CLI_WINDOW3_OPN, DO_CLI_WINDOW3_CLO,
    DO_CLI_DESTRAT1_SPEED1, DO_CLI_DESTRAT1_SPEED2, DO_CLI_DESTRAT1_SPEED3,
    DO_CLI_SCREEN,      DO_IRR_TRAY5,       DO_IRR_TRAY6,
    DO_CLI_WINDOW4_OPN, DO_CLI_WINDOW4_CLO,
    DO_CLI_DESTRAT2_SPEED1, DO_CLI_DESTRAT2_SPEED2, DO_CLI_DESTRAT2_SPEED3,
    DO_CLI_SKIRTVALVE,  DO_IRR_TRAY7,       DO_IRR_TRAY8,
    DO_COUNT
};

/** @} */


/** @name Internal variables (computed globals)
 *
 *  Everything the firmware derives rather than reads. They are variables like
 *  any other - the monitor page, the shell and telemetry resolve them through
 *  the same VarRef - which is what lets the site describe its whole state
 *  without a payload struct for the console to walk.
 *  @{ */

/** @brief Computed analog globals, written by the control programs. */
const Variable g_internal_analog[] = {
    { "sys_supply_hz", "Hz", "deadbanded incoming supply frequency", "", "0", 2, nullptr },
};
const uint16_t g_internal_analog_count =
    (uint16_t)(sizeof(g_internal_analog) / sizeof(g_internal_analog[0]));

/** @brief Symbolic index per computed analog. */
enum IaId : uint16_t {
    IA_SYS_SUPPLY_HZ,
    IA_COUNT
};

/** @brief Computed digital globals: the alarm conditions, plus the state flags
 *  the control programs latch.
 *
 *  The alarm rows come FIRST and in alarms.h order, because each row of that
 *  table now names its condition by VarRef - the two files are generated
 *  together and their order is the contract between them. */
const Variable g_internal_digital[] = {
    { "alarm_ac_grid",        "", "Grid AC out-of-spec/offline",            "", "0", 0, nullptr },
    { "alarm_dc_ps1",         "", "DC PS1 volts too low",                   "", "0", 0, nullptr },
    { "alarm_gen_flt",        "", "Generator failed to start",              "", "0", 0, nullptr },
    { "alarm_ac_local",       "", "Local AC out of spec or offline",        "", "0", 0, nullptr },
    { "alarm_dc_ps2",         "", "DC PS2 volts too low",                   "", "0", 0, nullptr },
    { "alarm_dc_ups",         "", "DC UPS on battery",                      "", "0", 0, nullptr },
    { "alarm_dc_local",       "", "Local DC volts too low",                 "", "0", 0, nullptr },
    { "alarm_gen_batt",       "", "Gen. start battery low",                 "", "0", 0, nullptr },
    { "alarm_plc_flt",        "", "Failed to synchronize with PLC",         "", "0", 0, nullptr },
    { "alarm_net_flt",        "", "Network error",                          "", "0", 0, nullptr },
    { "alarm_ntp_tim",        "", "NTP sync timeout",                       "", "0", 0, nullptr },
    { "alarm_act_flt",        "", "Actioner failure",                       "", "0", 0, nullptr },
    { "alarm_sns_flt",        "", "Sensor fault",                           "", "0", 0, nullptr },
    { "alarm_too_hot",        "", "Inside temperature too high",            "", "0", 0, nullptr },
    { "alarm_too_cold",       "", "Inside temperature too low",             "", "0", 0, nullptr },
    { "alarm_too_hum",        "", "Relative humidity too high",             "", "0", 0, nullptr },
    { "alarm_tmp_str",        "", "Temp. stratification too high",          "", "0", 0, nullptr },
    { "alarm_wtr_lk",         "", "Leak in irrigation circuit",             "", "0", 0, nullptr },
    { "alarm_wtr_bar",        "", "Water pressure too low",                 "", "0", 0, nullptr },
    { "alarm_wtr_clog",       "", "Failing to drain a tray",                "", "0", 0, nullptr },
    { "alarm_io_drop",        "", "Logical I/O drops",                      "", "0", 0, nullptr },
    { "alarm_log_fld",        "", "Logs flooding",                          "", "0", 0, nullptr },
    { "alarm_wh_storm",       "", "Current storm conditions",               "", "0", 0, nullptr },
    { "sys_supply_lockout", "", "supply unhealthy: actuators held off", "", "0", 0, nullptr },
};
const uint16_t g_internal_digital_count =
    (uint16_t)(sizeof(g_internal_digital) / sizeof(g_internal_digital[0]));

/** @brief Symbolic index per computed digital. */
enum IdId : uint16_t {
    ID_AC_GRID, ID_DC_PS1, ID_GEN_FLT, ID_AC_LOCAL,
    ID_DC_PS2, ID_DC_UPS, ID_DC_LOCAL, ID_GEN_BATT,
    ID_PLC_FLT, ID_NET_FLT, ID_NTP_TIM, ID_ACT_FLT,
    ID_SNS_FLT, ID_TOO_HOT, ID_TOO_COLD, ID_TOO_HUM,
    ID_TMP_STR, ID_WTR_LK, ID_WTR_BAR, ID_WTR_CLOG,
    ID_IO_DROP, ID_LOG_FLD, ID_WH_STORM,
    ID_SYS_SUPPLY_LOCKOUT,
    ID_COUNT
};

/** @} */

#undef MTR
#undef COIL

/** @name Value stores (RAM, parallel to the tables above)
 *  @{ */

extern AnalogValue  g_analog_in_values[AI_COUNT];
extern DigitalValue g_digital_out_values[DO_COUNT];
extern AnalogValue  g_internal_analog_values[IA_COUNT];
extern DigitalValue g_internal_digital_values[ID_COUNT];

/** @} */

/** @brief The tables bound to their stores, as a brace-initialiser.
 *
 *  A macro rather than a function so the site's SiteDescriptor stays a
 *  compile-time constant in flash: any function call in that initialiser makes
 *  it dynamic and moves the whole descriptor into .data.
 *
 *  Analog outputs and digital inputs are empty: the AI-1 / AO-1 modules are
 *  wired but unmapped, so those tables wait until channels are assigned. */
#define SITE_IO_TABLES { \
    g_analog_in,       g_analog_in_values,       AI_COUNT, \
    nullptr,           nullptr,                  0, \
    nullptr,           nullptr,                  0, \
    g_digital_out,     g_digital_out_values,     DO_COUNT, \
    g_internal_analog, g_internal_analog_values, IA_COUNT, \
    g_internal_digital,g_internal_digital_values,ID_COUNT }

// The enums must stay in step with the tables they index; a mismatch would
// silently read the wrong signal.
static_assert(AI_COUNT == sizeof(g_analog_in)   / sizeof(g_analog_in[0]),
              "AiId does not match g_analog_in");
static_assert(DO_COUNT == sizeof(g_digital_out) / sizeof(g_digital_out[0]),
              "DoId does not match g_digital_out");

}  // namespace Site

#endif // SITE_IO_H
