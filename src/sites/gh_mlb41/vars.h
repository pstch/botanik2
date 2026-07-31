#ifndef SITE_VARS_H
#define SITE_VARS_H

/**
 * @file vars.h
 * @brief GENERATED - every gh_mlb41 variable, in one table.
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
 * channels are unmapped, so the site has no XP_LOCAL_ANALOG variables yet, and
 * a later mapping is purely additive - one more row, one more enumerator.
 *
 * @warning Binding::reg holds the FIELD-DEVICE address: the coil number on the
 * relay boards, and the device-profile OBJECT INDEX on the meters. The raw
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

// A meter reading. Bands are generous on purpose - they exist to catch a dead
// or unplugged meter reporting a rail, not to police the process. `dgt` is the
// display precision: a power factor to 0 decimals is useless, a watt to 2 is
// noise.
#define MTR(nm, unit, dgt, dev, obj, lo, hi, desc) \
    { Scalar::F(lo), Scalar::F(hi), Scalar::F(0.0f), nm, unit, desc, nullptr, \
      { XP_MODBUS, T_F32, dev, 3, obj, 2, 1.0f, 0.0f }, \
      5000, 0, 0, T_F32, ROLE_INPUT, dgt, STR_NONE, F_BOUNDED, {} }

// A relay coil, written with function code 5.
#define COIL(nm, dev, coil, desc) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(false), nm, "", desc, nullptr, \
      { XP_MODBUS, T_BOOL, dev, 5, coil, 1, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_OUTPUT, 0, STR_NONE, 0, {} }

// A computed boolean: an alarm condition or a latched state flag.
#define IBOOL(nm, desc) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(false), nm, "", desc, nullptr, \
      { XP_NONE, T_BOOL, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_INTERNAL, 0, STR_NONE, 0, {} }

// A computed real.
#define IREAL(nm, unit, dgt, desc) \
    { Scalar::F(0.0f), Scalar::F(0.0f), Scalar::F(0.0f), nm, unit, desc, nullptr, \
      { XP_NONE, T_F32, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_F32, ROLE_INTERNAL, dgt, STR_NONE, 0, {} }

/** @brief Every variable this site declares, in one table.
 *
 *  Plain `const`, so each including TU gets its own copy; two TUs include this
 *  site in the firmware build, so the table costs its ~7 KB twice. Moving the
 *  rows into vars.cpp behind an `extern const` would fix that, and is deferred
 *  only because it restructures the generated file. */
const Variable g_vars[] = {
/** @name Modbus meter readings (43)
 *  Engineering values: the driver applies scale/offset, so control logic
 *  never sees counts. min/max are the plausibility band - a reading outside
 *  it marks the variable invalid, since a dead sensor usually reads a rail.
 *  @{ */
    // -- SRR/TAC/MTR-1, Eastron SDM630, addr 5: three-phase power meter --
    MTR("pwr_L1_V", "V", 1, 5, 0, 0.0f, 500.0f, "L1 phase voltage"),
    MTR("pwr_L2_V", "V", 1, 5, 1, 0.0f, 500.0f, "L2 phase voltage"),
    MTR("pwr_L3_V", "V", 1, 5, 2, 0.0f, 500.0f, "L3 phase voltage"),
    MTR("pwr_L1_A", "A", 1, 5, 3, 0.0f, 200.0f, "L1 current"),
    MTR("pwr_L2_A", "A", 1, 5, 4, 0.0f, 200.0f, "L2 current"),
    MTR("pwr_L3_A", "A", 1, 5, 5, 0.0f, 200.0f, "L3 current"),
    MTR("pwr_L1_VA", "VA", 0, 5, 6, -1e6f, 1e6f, "L1 apparent power"),
    MTR("pwr_L2_VA", "VA", 0, 5, 7, -1e6f, 1e6f, "L2 apparent power"),
    MTR("pwr_L3_VA", "VA", 0, 5, 8, -1e6f, 1e6f, "L3 apparent power"),
    MTR("pwr_L1_VAr", "VAr", 0, 5, 9, -1e6f, 1e6f, "L1 reactive power"),
    MTR("pwr_L2_VAr", "VAr", 0, 5, 10, -1e6f, 1e6f, "L2 reactive power"),
    MTR("pwr_L3_VAr", "VAr", 0, 5, 11, -1e6f, 1e6f, "L3 reactive power"),
    MTR("pwr_P1_PF", "", 3, 5, 12, -1.0f, 1.0f, "L1 power factor"),
    MTR("pwr_P2_PF", "", 3, 5, 13, -1.0f, 1.0f, "L2 power factor"),
    MTR("pwr_P3_PF", "", 3, 5, 14, -1.0f, 1.0f, "L3 power factor"),
    MTR("pwr_P1_PA", "deg", 1, 5, 15, -180.0f, 180.0f, "L1 phase angle"),
    MTR("pwr_P2_PA", "deg", 1, 5, 16, -180.0f, 180.0f, "L2 phase angle"),
    MTR("pwr_P3_PA", "deg", 1, 5, 17, -180.0f, 180.0f, "L3 phase angle"),
    MTR("pwr_LN_V", "V", 1, 5, 18, 0.0f, 500.0f, "average line-to-neutral voltage"),
    MTR("pwr_LL_V", "V", 1, 5, 19, 0.0f, 900.0f, "average line-to-line voltage"),
    MTR("pwr_avg_A", "A", 1, 5, 20, 0.0f, 200.0f, "average current"),
    MTR("pwr_total_A", "A", 1, 5, 21, 0.0f, 600.0f, "total current"),
    MTR("pwr_total_W", "W", 0, 5, 22, -1e6f, 1e6f, "total active power"),
    MTR("pwr_total_VA", "VA", 0, 5, 23, -1e6f, 1e6f, "total apparent power"),
    MTR("pwr_total_VAR", "VAr", 0, 5, 24, -1e6f, 1e6f, "total reactive power"),
    MTR("pwr_total_PF", "", 3, 5, 25, -1.0f, 1.0f, "total power factor"),
    MTR("pwr_total_PA", "deg", 1, 5, 26, -180.0f, 180.0f, "total phase angle"),
    MTR("pwr_Hz", "Hz", 2, 5, 27, 0.0f, 100.0f, "line frequency"),
    MTR("pwr_12_V", "V", 1, 5, 28, 0.0f, 900.0f, "L1-L2 voltage"),
    MTR("pwr_L23_V", "V", 1, 5, 29, 0.0f, 900.0f, "L2-L3 voltage"),
    MTR("pwr_L31_V", "V", 1, 5, 30, 0.0f, 900.0f, "L3-L1 voltage"),
    MTR("pwr_LL_avg_THD", "%", 1, 5, 31, 0.0f, 100.0f, "line-to-line average THD"),
    MTR("pwr_LN_avg_THD", "%", 1, 5, 32, 0.0f, 100.0f, "line-to-neutral average THD"),
    MTR("pwr_N_A", "A", 1, 5, 33, 0.0f, 200.0f, "neutral current"),
    MTR("pwr_L1_W", "W", 0, 5, 34, -1e6f, 1e6f, "L1 active power"),
    MTR("pwr_L2_W", "W", 0, 5, 35, -1e6f, 1e6f, "L2 active power"),
    MTR("pwr_L3_W", "W", 0, 5, 36, -1e6f, 1e6f, "L3 active power"),
    MTR("ats_V", "V", 1, 6, 0, 0.0f, 500.0f, "ATS voltage"),
    MTR("ats_A", "A", 1, 6, 1, 0.0f, 200.0f, "ATS current"),
    MTR("ats_W", "W", 0, 6, 2, 0.0f, 1000.0f, "ATS active power"),
    MTR("ats_VAr", "VAr", 0, 6, 3, -1e6f, 1e6f, "ATS reactive power"),
    MTR("ats_VA", "VA", 0, 6, 4, -1e6f, 1e6f, "ATS apparent power"),
    MTR("ats_Hz", "Hz", 2, 6, 5, 0.0f, 100.0f, "ATS frequency"),
 /** @} */

/** @name Relay coils (28)
 *  Safe state false: every actuator de-energises on a fault, which for
 *  windows, valves and fans is the direction that does no harm.
 *  @{ */
    // -- SRR/TDC/RO, Waveshare 26517, addr 31 --
    COIL("cli_window1_opn", 31, 0, "window 1 open"),
    COIL("cli_window1_clo", 31, 1, "window 1 close"),
    COIL("irr_input_ro1", 31, 2, "irrigation input valve 1"),
    COIL("irr_output_ro1", 31, 3, "irrigation output valve 1"),
    COIL("irr_tray1", 31, 4, "tray 1 valve"),
    COIL("irr_tray2", 31, 5, "tray 2 valve"),
    // -- SRR/TDC/RO, Waveshare 26517, addr 32 --
    COIL("cli_window2_opn", 32, 0, "window 2 open"),
    COIL("cli_window2_clo", 32, 1, "window 2 close"),
    COIL("irr_input_ro2", 32, 2, "irrigation input valve 2"),
    COIL("irr_output_ro2", 32, 3, "irrigation output valve 2"),
    COIL("irr_tray3", 32, 4, "tray 3 valve"),
    COIL("irr_tray4", 32, 5, "tray 4 valve"),
    // -- SRR/TDC/RO, Waveshare 26517, addr 33 --
    COIL("cli_window3_opn", 33, 0, "window 3 open"),
    COIL("cli_window3_clo", 33, 1, "window 3 close"),
    COIL("cli_destrat1_speed1", 33, 2, "destratification fan 1 speed 1"),
    COIL("cli_destrat1_speed2", 33, 3, "destratification fan 1 speed 2"),
    COIL("cli_destrat1_speed3", 33, 4, "destratification fan 1 speed 3"),
    COIL("cli_screen", 33, 5, "thermal screen"),
    COIL("irr_tray5", 33, 6, "tray 5 valve"),
    COIL("irr_tray6", 33, 7, "tray 6 valve"),
    // -- SRR/TDC/RO, Waveshare 26517, addr 34 --
    COIL("cli_window4_opn", 34, 0, "window 4 open"),
    COIL("cli_window4_clo", 34, 1, "window 4 close"),
    COIL("cli_destrat2_speed1", 34, 2, "destratification fan 2 speed 1"),
    COIL("cli_destrat2_speed2", 34, 3, "destratification fan 2 speed 2"),
    COIL("cli_destrat2_speed3", 34, 4, "destratification fan 2 speed 3"),
    COIL("cli_skirtValve", 34, 5, "wet skirt valve"),
    COIL("irr_tray7", 34, 6, "tray 7 valve"),
    COIL("irr_tray8", 34, 7, "tray 8 valve"),
 /** @} */

/** @name Computed internals (24)
 *  Nothing drives these from the field; the control programs write them.
 *  The alarm rows come first and in alarms.h order - those two files are
 *  generated together and each alarm row names its variable by index.
 *  @{ */
    IBOOL("alarm_ac_grid", "Grid AC out-of-spec/offline"),
    IBOOL("alarm_dc_ps1", "DC PS1 volts too low"),
    IBOOL("alarm_gen_flt", "Generator failed to start"),
    IBOOL("alarm_ac_local", "Local AC out of spec or offline"),
    IBOOL("alarm_dc_ps2", "DC PS2 volts too low"),
    IBOOL("alarm_dc_ups", "DC UPS on battery"),
    IBOOL("alarm_dc_local", "Local DC volts too low"),
    IBOOL("alarm_gen_batt", "Gen. start battery low"),
    IBOOL("alarm_plc_flt", "Failed to synchronize with PLC"),
    IBOOL("alarm_net_flt", "Network error"),
    IBOOL("alarm_ntp_tim", "NTP sync timeout"),
    IBOOL("alarm_act_flt", "Actioner failure"),
    IBOOL("alarm_sns_flt", "Sensor fault"),
    IBOOL("alarm_too_hot", "Inside temperature too high"),
    IBOOL("alarm_too_cold", "Inside temperature too low"),
    IBOOL("alarm_too_hum", "Relative humidity too high"),
    IBOOL("alarm_tmp_str", "Temp. stratification too high"),
    IBOOL("alarm_wtr_lk", "Leak in irrigation circuit"),
    IBOOL("alarm_wtr_bar", "Water pressure too low"),
    IBOOL("alarm_wtr_clog", "Failing to drain a tray"),
    IBOOL("alarm_io_drop", "Logical I/O drops"),
    IBOOL("alarm_log_fld", "Logs flooding"),
    IBOOL("alarm_wh_storm", "Current storm conditions"),
    IBOOL("sys_supply_lockout", "supply unhealthy: actuators held off"),
    IREAL("sys_supply_hz", "Hz", 2, "deadbanded incoming supply frequency"),
 /** @} */
};
const uint16_t g_var_count = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));

#undef MTR
#undef COIL
#undef IBOOL
#undef IREAL

/** @brief Symbolic index per variable. The row order above is the enum order;
 *  a mismatch would silently address the wrong signal, so the count is
 *  asserted below. */
enum VarId : uint16_t {
    V_PWR_L1_V, V_PWR_L2_V, V_PWR_L3_V,
    V_PWR_L1_A, V_PWR_L2_A, V_PWR_L3_A,
    V_PWR_L1_VA, V_PWR_L2_VA, V_PWR_L3_VA,
    V_PWR_L1_VAR, V_PWR_L2_VAR, V_PWR_L3_VAR,
    V_PWR_P1_PF, V_PWR_P2_PF, V_PWR_P3_PF,
    V_PWR_P1_PA, V_PWR_P2_PA, V_PWR_P3_PA,
    V_PWR_LN_V, V_PWR_LL_V, V_PWR_AVG_A,
    V_PWR_TOTAL_A, V_PWR_TOTAL_W, V_PWR_TOTAL_VA,
    V_PWR_TOTAL_VAR, V_PWR_TOTAL_PF, V_PWR_TOTAL_PA,
    V_PWR_HZ, V_PWR_12_V, V_PWR_L23_V,
    V_PWR_L31_V, V_PWR_LL_AVG_THD, V_PWR_LN_AVG_THD,
    V_PWR_N_A, V_PWR_L1_W, V_PWR_L2_W,
    V_PWR_L3_W, V_ATS_V, V_ATS_A,
    V_ATS_W, V_ATS_VAR, V_ATS_VA,
    V_ATS_HZ, V_CLI_WINDOW1_OPN, V_CLI_WINDOW1_CLO,
    V_IRR_INPUT_RO1, V_IRR_OUTPUT_RO1, V_IRR_TRAY1,
    V_IRR_TRAY2, V_CLI_WINDOW2_OPN, V_CLI_WINDOW2_CLO,
    V_IRR_INPUT_RO2, V_IRR_OUTPUT_RO2, V_IRR_TRAY3,
    V_IRR_TRAY4, V_CLI_WINDOW3_OPN, V_CLI_WINDOW3_CLO,
    V_CLI_DESTRAT1_SPEED1, V_CLI_DESTRAT1_SPEED2, V_CLI_DESTRAT1_SPEED3,
    V_CLI_SCREEN, V_IRR_TRAY5, V_IRR_TRAY6,
    V_CLI_WINDOW4_OPN, V_CLI_WINDOW4_CLO, V_CLI_DESTRAT2_SPEED1,
    V_CLI_DESTRAT2_SPEED2, V_CLI_DESTRAT2_SPEED3, V_CLI_SKIRTVALVE,
    V_IRR_TRAY7, V_IRR_TRAY8, V_ALARM_AC_GRID,
    V_ALARM_DC_PS1, V_ALARM_GEN_FLT, V_ALARM_AC_LOCAL,
    V_ALARM_DC_PS2, V_ALARM_DC_UPS, V_ALARM_DC_LOCAL,
    V_ALARM_GEN_BATT, V_ALARM_PLC_FLT, V_ALARM_NET_FLT,
    V_ALARM_NTP_TIM, V_ALARM_ACT_FLT, V_ALARM_SNS_FLT,
    V_ALARM_TOO_HOT, V_ALARM_TOO_COLD, V_ALARM_TOO_HUM,
    V_ALARM_TMP_STR, V_ALARM_WTR_LK, V_ALARM_WTR_BAR,
    V_ALARM_WTR_CLOG, V_ALARM_IO_DROP, V_ALARM_LOG_FLD,
    V_ALARM_WH_STORM, V_SYS_SUPPLY_LOCKOUT, V_SYS_SUPPLY_HZ,
    V_COUNT
};

/** @name Value store (RAM, parallel to the table)
 *  @{ */

extern Value g_var_values[V_COUNT];
/** @brief T_STR live text. Empty until the parameters land here; a zero-size
 *  array is not valid C++, so it holds one byte. */
extern char  g_str_pool[1];

/** @} */

/** @brief The table bound to its store, as a brace-initialiser.
 *
 *  A macro rather than a function so the site's SiteDescriptor stays a
 *  compile-time constant in flash: any function call in that initialiser makes
 *  it dynamic and moves the whole descriptor into .data.
 *
 *  The trailing pair is the ROLE_PARAM block. It is empty for now - parameters
 *  still live in their own byte image - and when they arrive they append to the
 *  END of the table, so no existing index moves. */
#define SITE_VAR_TABLES { \
    g_vars, g_var_values, V_COUNT, \
    g_str_pool, (uint16_t)sizeof(g_str_pool), \
    0, 0 }

// The enum must stay in step with the table it indexes; a mismatch would
// silently read the wrong signal.
static_assert(sizeof(g_vars) / sizeof(g_vars[0]) == (size_t)V_COUNT,
              "g_vars and VarId disagree: an index would address the wrong variable");

}  // namespace Site

#endif // SITE_VARS_H
