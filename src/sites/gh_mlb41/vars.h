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

// Every row macro comes in two forms: the plain one, and an `_N` variant taking
// a trailing NOTES string right after the description. Notes are what the detail
// panels show under Description - the longer story a table column has no room
// for - and most rows do not need one, so the plain form passes nullptr rather
// than making 116 call sites carry an empty string.
//
// A meter reading. Bands are generous on purpose - they exist to catch a dead
// or unplugged meter reporting a rail, not to police the process. `dgt` is the
// display precision: a power factor to 0 decimals is useless, a watt to 2 is
// noise.
#define MTR_N(nm, unit, dgt, dev, obj, lo, hi, desc, notes) \
    { Scalar::F(lo), Scalar::F(hi), Scalar::F(0.0f), nm, unit, desc, notes, nullptr, \
      { XP_MODBUS, T_F32, dev, 3, obj, 2, 1.0f, 0.0f }, \
      5000, 0, 0, T_F32, ROLE_INPUT, dgt, STR_NONE, F_BOUNDED, {} }
#define MTR(nm, unit, dgt, dev, obj, lo, hi, desc) MTR_N(nm, unit, dgt, dev, obj, lo, hi, desc, nullptr)

// A relay coil, written with function code 5.
#define COIL_N(nm, dev, coil, desc, notes) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(false), nm, "", desc, notes, nullptr, \
      { XP_MODBUS, T_BOOL, dev, 5, coil, 1, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_OUTPUT, 0, STR_NONE, 0, {} }
#define COIL(nm, dev, coil, desc) COIL_N(nm, dev, coil, desc, nullptr)

// A computed boolean: an alarm condition or a latched state flag.
#define IBOOL_N(nm, desc, notes) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(false), nm, "", desc, notes, nullptr, \
      { XP_NONE, T_BOOL, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_INTERNAL, 0, STR_NONE, 0, {} }
#define IBOOL(nm, desc) IBOOL_N(nm, desc, nullptr)

// A computed real.
#define IREAL_N(nm, unit, dgt, desc, notes) \
    { Scalar::F(0.0f), Scalar::F(0.0f), Scalar::F(0.0f), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_F32, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_F32, ROLE_INTERNAL, dgt, STR_NONE, 0, {} }
#define IREAL(nm, unit, dgt, desc) IREAL_N(nm, unit, dgt, desc, nullptr)

/** @brief Text-parameter capacity, including the NUL. */
static const uint16_t STR_CAP = 64;
/** @brief 8 text parameters x STR_CAP. */
static const uint16_t STR_POOL_BYTES = 8 * STR_CAP;

// ---- Parameters --------------------------------------------------------
// Persisted and editable. They sit at the END of the table so the field-IO
// indices above never move when one is added. `def` is a typed literal, so a
// bad default is a compile error rather than a boot-time parse yielding zero.

/** @brief A group's commit serial: bump it to apply that group's staged edits.
 *  Named after the group, which is how the consuming component finds it. */
#define PSERIAL_N(nm, desc, notes) \
    { Scalar::U(0), Scalar::U(1000000000), Scalar::U(1), nm, "", desc, notes, nullptr, \
      { XP_NONE, T_U32, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_U32, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }
#define PSERIAL(nm, desc) PSERIAL_N(nm, desc, nullptr)

#define PBOOL_N(nm, dflt, desc, notes) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(dflt), nm, "", desc, notes, nullptr, \
      { XP_NONE, T_BOOL, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }
#define PBOOL(nm, dflt, desc) PBOOL_N(nm, dflt, desc, nullptr)

#define PU16_N(nm, dflt, lo, hi, unit, desc, notes) \
    { Scalar::U(lo), Scalar::U(hi), Scalar::U(dflt), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_U16, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_U16, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }
#define PU16(nm, dflt, lo, hi, unit, desc) PU16_N(nm, dflt, lo, hi, unit, desc, nullptr)

#define PU32_N(nm, dflt, lo, hi, unit, desc, notes) \
    { Scalar::U(lo), Scalar::U(hi), Scalar::U(dflt), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_U32, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_U32, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }
#define PU32(nm, dflt, lo, hi, unit, desc) PU32_N(nm, dflt, lo, hi, unit, desc, nullptr)

#define PU8_N(nm, dflt, lo, hi, unit, desc, notes) \
    { Scalar::U(lo), Scalar::U(hi), Scalar::U(dflt), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_U8, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_U8, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }
#define PU8(nm, dflt, lo, hi, unit, desc) PU8_N(nm, dflt, lo, hi, unit, desc, nullptr)

#define PI16_N(nm, dflt, lo, hi, unit, desc, notes) \
    { Scalar::I(lo), Scalar::I(hi), Scalar::I(dflt), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_I16, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_I16, ROLE_PARAM, 0, STR_NONE, F_BOUNDED, {} }
#define PI16(nm, dflt, lo, hi, unit, desc) PI16_N(nm, dflt, lo, hi, unit, desc, nullptr)

#define PF32_N(nm, dflt, lo, hi, dgt, unit, desc, notes) \
    { Scalar::F(lo), Scalar::F(hi), Scalar::F(dflt), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_F32, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_F32, ROLE_PARAM, dgt, STR_NONE, F_BOUNDED, {} }
#define PF32(nm, dflt, lo, hi, dgt, unit, desc) PF32_N(nm, dflt, lo, hi, dgt, unit, desc, nullptr)

/** @brief A text parameter. Its live value lives in the string pool at @p off;
 *  `def` carries the factory text, which init() copies into the pool. */
#define PSTR_N(nm, off, kind_, dflt, desc, notes) \
    { Scalar::U(0), Scalar::U(0), Scalar::S(dflt), nm, "", desc, notes, nullptr, \
      { XP_NONE, T_STR, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, off, STR_CAP, T_STR, ROLE_PARAM, 0, kind_, 0, {} }
#define PSTR(nm, off, kind_, dflt, desc) PSTR_N(nm, off, kind_, dflt, desc, nullptr)

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
    IBOOL("alarm_gen", "Generator failed to start"),
    IBOOL("alarm_ac_local", "Local AC out of spec or offline"),
    IBOOL("alarm_dc_ps2", "DC PS2 volts too low"),
    IBOOL("alarm_dc_ups", "DC UPS on battery"),
    IBOOL("alarm_dc_local", "Local DC volts too low"),
    IBOOL("alarm_generator_battery", "Gen. start battery low"),
    IBOOL("alarm_plc", "Failed to synchronize with PLC"),
    IBOOL("alarm_net_fault", "Network error"),
    IBOOL("alarm_ntp_timeout", "NTP sync timeout"),
    IBOOL("alarm_actuator_fault", "Actioner failure"),
    IBOOL("alarm_sensor_fault", "Sensor fault"),
    IBOOL("alarm_too_hot", "Inside temperature too high"),
    IBOOL("alarm_too_cold", "Inside temperature too low"),
    IBOOL("alarm_too_humid", "Relative humidity too high"),
    IBOOL("alarm_tmp_str", "Temp. stratification too high"),
    IBOOL("alarm_water_leak", "Leak in irrigation circuit"),
    IBOOL("alarm_water_pressure", "Water pressure too low"),
    IBOOL("alarm_water_clog", "Failing to drain a tray"),
    IBOOL("alarm_io_drop", "Logical I/O drops"),
    IBOOL("alarm_log_flood", "Logs flooding"),
    IBOOL("alarm_weather_storm", "Current storm conditions"),
    IBOOL("sys_supply_lockout", "supply unhealthy: actuators held off"),
    IREAL("sys_supply_hz", "Hz", 2, "deadbanded incoming supply frequency"),
 /** @} */

    // -- net --
    PSERIAL_N("net_serial",
              "Network config commit", "Edits to the net_* parameters only stage; bump this to apply them."),
    PBOOL_N("net_dhcp", 0,
            "Enable DHCP IP configuration", "With DHCP on, the static net_ip/mask/gw rows are ignored."),
    PSTR("net_ip", 0, STR_IPV4, "192.168.1.42", "Static IP address"),
    PSTR("net_mask", 64, STR_IPV4, "255.255.255.0", "Static IP netmask"),
    PSTR("net_dns1", 128, STR_IPV4, "8.8.8.8", "Static DNS resolver 1"),
    PSTR_N("net_dns2", 192, STR_IPV4_OPT, "4.4.4.4",
           "Static DNS resolver 2", "Optional: leave empty to use net_dns1 alone."),
    PSTR("net_gw", 256, STR_IPV4, "192.168.1.1", "Static IP gateway"),
    // -- ntp --
    PSERIAL("ntp_serial", "NTP config commit"),
    PBOOL("ntp_enable", 1, "Enable NTP time synchronization"),
    PU32("ntp_poll_sec", 3600, 10, 86400, "sec", "NTP poll interval, in seconds"),
    PSTR("ntp_srv1", 320, STR_HOST, "0.fr.pool.ntp.org", "NTP server 1"),
    PSTR_N("ntp_srv2", 384, STR_HOST_OPT, "1.fr.pool.ntp.org",
           "NTP server 2", "Tried only after srv1 fails; empty means no fallback."),
    // -- mqtt --
    PSERIAL("mqtt_serial", "MQTT config commit"),
    PBOOL("mqtt_enable", 1, "Enable MQTT"),
    PSTR("mqtt_host", 448, STR_HOST, "192.168.1.43", "MQTT broker host"),
    PU16("mqtt_port", 58443, 0, 65535, "", "MQTT broker port"),
    PU16_N("mqtt_ka_sec", 20, 10, 1800, "sec",
           "MQTT keep-alive interval", "Also how long a dead broker goes unnoticed - `net` shows the age\nof the last contact."),
    // -- geo --
    PSERIAL("geo_serial", "Geo config commit"),
    PF32_N("geo_lat_deg", 48.07f, -90.0f, 90.0f, 2, "deg",
           "Geographical latitude, in degrees", "Set according to location"),
    PF32_N("geo_lon_deg", 1.32f, -180.0f, 180.0f, 2, "deg",
           "Geographical longitude, in degrees", "Set according to location"),
    // -- cli --
    PF32("cli_coldBankOffset_degC", 6.0f, 0.0f, 15.0f, 1, "degC",
         "How much cooler to run before a hot day"),
    PF32("cli_heatBankOffset_degC", 7.0f, 0.0f, 15.0f, 1, "degC",
         "How much warmer to run before cold night"),
    PF32("cli_heatBankTrig_degC", 10.0f, 6.0f, 18.0f, 1, "degC",
         "If yesterday's lowest temp below this, we bank heat before sunset"),
    PF32("cli_coldBankTrig_degC", 24.0f, 12.0f, 40.0f, 1, "degC",
         "If yesterday's highest temp above this, we pre-chill before sunrise"),
    PF32_N("cli_coolOn_degC", 20.0f, 10.0f, 30.0f, 1, "degC",
           "Base cooling target temperature", "All cooling is based on this target temperature."),
    PF32_N("cli_coolKp", 0.08f, 0.0f, 1.0f, 2, "",
           "Cooling PID proportional factor", "Leave as set"),
    PF32_N("cli_coolTi_sec", 200.0f, 0.0f, 3600.0f, 0, "sec",
           "Cooling PID integral factor", "Leave as set"),
    PF32_N("cli_coolTd_sec", 0.0f, 0.0f, 10.0f, 0, "sec",
           "Cooling PID derivative factor", "Leave at zero unless you're completely crazy"),
    PF32("cli_ventMargin_degC", 1.0f, 0.0f, 15.0f, 1, "degC",
         "Intake air must be this much cooler"),
    PF32("cli_skirtAvail_degC", 4.0f, 0.0f, 10.0f, 1, "degC",
         "Air must be this dry for the pad to cool"),
    PF32_N("cli_thermalScreenDem", -0.2f, -1.0f, 0.0f, 2, "",
           "Cooling effort at which we deploy the thermal screen", "Negative demand; effect is heating"),
    PF32_N("cli_groundHeatDem", -0.1f, -1.0f, 0.0f, 2, "",
           "Cooling effort at which we try to take hot air from below", "Negative demand; effect is heating"),
    PF32("cli_winOpenDem", 0.1f, 0.0f, 1.0f, 2, "",
         "Cooling effort where we begin opening windows"),
    PF32("cli_intakeStartDem", 0.3f, 0.0f, 1.0f, 2, "", "Cooling effort where intake fans start"),
    PF32("cli_winFullDem", 0.5f, 0.0f, 1.0f, 2, "", "Cooling effort at fully open windows"),
    PF32("cli_skirtEnableDem", 0.7f, 0.0f, 1.0f, 2, "",
         "Cooling effort where the wet skirt starts"),
    PF32("cli_fanDemLow", 0.2f, 0.0f, 1.0f, 2, "", "Cooling effort for fan speed 1"),
    PF32_N("cli_fanDemMed", 0.4f, 0.0f, 1.0f, 2, "",
           "Cooling effort for fan speed 2", "Must stay above cli_fanDemLow."),
    PF32_N("cli_fanDemHigh", 0.8f, 0.0f, 1.0f, 2, "",
           "Cooling effort for fan speed 3", "Must stay above cli_fanDemMed."),
    PF32("cli_intakeFullDem", 0.9f, 0.0f, 1.0f, 2, "", "Cooling effort at full speed intake fans"),
    PF32("cli_dTStratLo_degC", 2.0f, 0.0f, 10.0f, 1, "degC",
         "Top-to-bottom stratification for fan speed 1"),
    PF32("cli_dTStratMed_degC", 4.0f, 0.0f, 10.0f, 1, "degC",
         "Top-to-bottom stratification for fan speed 2"),
    PF32("cli_dTStratHi_degC", 6.0f, 0.0f, 10.0f, 1, "degC",
         "Top-to-bottom stratification for fan speed 3"),
    PF32("cli_dTStratHyst_degC", 2.0f, 0.5f, 5.0f, 1, "degC",
         "Gap change needed to switch fan speed"),
    PF32("cli_rot_dpLow_degC", 3.0f, 0.0f, 10.0f, 1, "degC",
         "Dryness margin where dehumidifying is fully activated"),
    PF32_N("cli_rot_dpHigh_degC", 4.5f, 0.0f, 10.0f, 1, "degC",
           "Dryness margin where dehumidifying starts", "Must stay above cli_rot_dpLow_degC."),
    PF32_N("cli_humidLo_degC", 18.0f, 0.0f, 30.0f, 1, "degC",
           "Dryness margin where humidifying starts", "Must stay above cli_rot_dpHigh_degC."),
    PF32_N("cli_humidHi_degC", 28.0f, 0.0f, 40.0f, 1, "degC",
           "Dryness margin where humidifying is fully activated", "Must stay above cli_humidLo_degC."),
    PF32("cli_humidSkirtDem", 0.5f, 0.0f, 1.0f, 2, "",
         "Dryness level where we humidify through the skirt"),
    PF32("cli_rhCap", 0.6f, 0.0f, 1.0f, 2, "", "Humidity that also starts rot defence"),
    PF32("cli_dpVentMargin_degC", 1.0f, 0.0f, 5.0f, 1, "degC",
         "Outside air must be this much drier to vent"),
    PF32_N("cli_tooCold_degC", 4.0f, 0.0f, 10.0f, 1, "degC",
           "Alarm: greenhouse is dangerously cold", "Set according to gas-heater thermostat setting"),
    PF32("cli_tooHot_degC", 35.0f, 20.0f, 50.0f, 1, "degC", "Alarm: greenhouse is dangerously hot"),
    PF32_N("cli_rotFlt_degC", 1.0f, 0.0f, 5.0f, 1, "degC",
           "Alarm: greenhouse is dangerously humid", "Set according to autonomous dehumidifier setting"),
    PF32_N("cli_intractor_rawMin_mV", 0.0f, 0.0f, 24000.0f, 0, "mV",
           "Intractor raw low-end value (zero flow)", "Set according to intractor characteristics"),
    PF32_N("cli_intractor_rawMax_mV", 10000.0f, 0.0f, 24000.0f, 0, "mV",
           "Intractor raw high-end value (max flow)", "Set according to intractor characteristics"),
    PF32_N("cli_intractor_engMin", 0.0f, 0.0f, 1.0f, 2, "",
           "Intractor speed at the low-end of the range", "Set according to intractor characteristics"),
    PF32("cli_intractor_engMax", 1.0f, 0.0f, 1.0f, 2, "",
         "Intractor speed at the high-end of the range"),
    PF32("cli_windLockout_mps", 5.0f, 0.0f, 50.0f, 1, "mps", "Wind speed that forces windows shut"),
    PU16_N("cli_windowTravel_sec", 15, 1, 60, "sec",
           "Nominal time to fully open/close a window", "Set according to actuator characteristics"),
    PU16_N("cli_windowTimeout_sec", 20, 1, 90, "sec",
           "Safe time to fully open/close a window", "Set according to actuator characteristics"),
    PU16_N("cli_screenTravel_sec", 60, 1, 300, "sec",
           "Nominal time to deploy/remove the thermal screen", "Set according to actuator characteristics"),
    PU16_N("cli_screenTimeout_sec", 80, 1, 400, "sec",
           "Safe time to deploy/remove the thermal screen", "Set according to actuator characteristics"),
    PU16("cli_rainLockout_sec", 900, 0, 3600, "sec", "Keep windows shut this long after rain"),
    PU16("cli_skirtValvePeriod_sec", 60, 10, 600, "sec",
         "Period for the skirt humidifier valve modulation"),
    PU16_N("cli_skirtValveMinOn_sec", 10, 1, 600, "sec",
           "Shortest allowed pulse length for the skirt humidifier", "Upper bound is cli_skirtValvePeriod_sec."),
    PF32("cli_screenDeploy_degC", 13.0f, 0.0f, 50.0f, 1, "degC",
         "Night temperature that deploys the thermal screens"),
    // -- sol --
    PI16("sol_coldBankSunrise_min", -30, -180, 60, "min",
         "Begin time of cold banking, minutes from sunrise"),
    PI16("sol_dawnBreezeSunrise_min", 30, -60, 180, "min",
         "Begin time of dawn breeze, minutes from sunrise"),
    PI16("sol_daySunrise_min", 60, 0, 180, "min",
         "Begin time of day operation, minutes from sunrise"),
    PI16("sol_heatBankSunset_min", -180, -300, 0, "min",
         "Begin time of heat banking, minutes from sunset"),
    PI16("sol_nightSunset_min", 30, 0, 180, "min",
         "Begin time of night operation, minutes from sunset"),
    // -- irr --
    PBOOL("irr_forceNow", 0, "Run a watering cycle immediately, once"),
    PU16("irr_liters_tray1", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray1", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray1", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray1", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray1", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray2", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray2", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray2", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray2", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray2", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray3", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray3", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray3", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray3", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray3", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray4", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray4", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray4", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray4", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray4", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray5", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray5", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray5", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray5", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray5", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray6", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray6", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray6", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray6", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray6", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray7", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray7", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray7", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray7", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray7", 0, "Force skipping this tray at the next cycle"),
    PU16("irr_liters_tray8", 150, 0, 500, "L", "Litres to flood this tray in summer"),
    PU8("irr_days_tray8", 14, 1, 60, "days", "Days between waterings in summer"),
    PU32("irr_last_tray8", 0, 0, 100000, "days", "Last watering (days since 1 Jan 1970)"),
    PBOOL("irr_forceOn_tray8", 0, "Force watering this tray at the next cycle"),
    PBOOL("irr_forceOff_tray8", 0, "Force skipping this tray at the next cycle"),
    PU16_N("irr_dryEwmaDays", 3, 1, 30, "days",
           "Days of dryness history to modulate the watering", "Recorded every 2 hours"),
    PF32("irr_dryLo_kPa", 0.15f, 0.0f, 2.0f, 2, "kPa", "Air dryness giving the least watering"),
    PF32_N("irr_dryHi_kPa", 1.2f, 0.0f, 5.0f, 2, "kPa",
           "Air dryness giving the most watering", "Must stay above irr_dryLo_kPa."),
    PF32_N("irr_modMin", 0.25f, 0.1f, 1.0f, 2, "",
           "Minimum fraction of water given in very humid weather", "The dullest air still gets this share of the summer dose"),
    PF32("irr_modGamma", 1.0f, 1.0f, 4.0f, 2, "", "How fast watering ramps up with dryness"),
    PF32_N("irr_volFreqSplit", 0.5f, 0.0f, 1.0f, 2, "",
           "Whether we reduce volume or increase period", "0 = only period, 1 = only volume"),
    PU16("irr_traysPerDay", 2, 1, 5, "", "Most trays watered in one day"),
    PF32("irr_dawnGate_degC", 12.0f, 5.0f, 30.0f, 1, "degC",
         "Skip pre-dawn watering below this temp"),
    PU16("irr_maxFill_sec", 300, 100, 1800, "sec", "Alarm if a tray takes longer to fill"),
    PU16("irr_drainWindow_sec", 300, 100, 1800, "sec", "Time allowed for a tray to drain"),
    PF32("irr_expDrainFrac", 0.6f, 0.0f, 1.0f, 2, "",
         "How far below expected drain fraction is a fault"),
    PF32("irr_drainMargin", 0.3f, 0.0f, 1.0f, 2, "",
         "Drain volume fraction below which we trigger a drain fault"),
    PU16("irr_flush_sec", 10, 5, 300, "sec",
         "Duration of the flush after/before the irrigation cycle"),
    PF32_N("irr_input_qtyPerImp", 1.0f, 0.01f, 10.0f, 2, "L",
           "Litres per pulse from the water meter", "Set according to meter characteristics"),
    PU16_N("irr_output_rawMin_uA", 0, 0, 24000, "uA",
           "uA at zero output flow", "Set according to output meter characteristics"),
    PU16_N("irr_output_rawMax_uA", 10000, 0, 24000, "uA",
           "uA at full output scale", "Set according to output meter characteristics"),
    PF32_N("irr_output_engMin_Lpm", 0.0f, 0.0f, 500.0f, 1, "Lpm",
           "Output litres/min at zero signal", "Set according to output meter characteristics"),
    PF32_N("irr_output_engMax_Lpm", 50.0f, 0.0f, 500.0f, 1, "Lpm",
           "Output litres/min at full scale", "Set according to output meter characteristics"),
    PU16_N("irr_output_avgTime_ms", 200, 0, 10000, "ms",
           "Rolling average for filtering this flow meter's output", "Set according to output meter characteristics"),
    // -- pwr --
    PU16_N("pwr_dc1_rawMin_mV", 4000, 0, 10000, "mV",
           "Low-end of the raw measurement", "Set according to voltage divider setup"),
    PU16_N("pwr_dc1_rawMax_mV", 6000, 0, 10000, "mV",
           "High-end of the raw measurement", "Set according to voltage divider setup"),
    PF32_N("pwr_dc1_engMin_V", 20.0f, 0.0f, 48.0f, 1, "V",
           "Low-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PF32_N("pwr_dc1_engMax_V", 30.0f, 0.0f, 48.0f, 1, "V",
           "High-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PU16_N("pwr_dc1_avgTime_ms", 1000, 10, 10000, "ms",
           "Rolling average for filtering this DC bus voltage", "Set according to voltage divider setup"),
    PU16_N("pwr_dc2_rawMin_mV", 4000, 0, 10000, "mV",
           "Low-end of the raw measurement", "Set according to voltage divider setup"),
    PU16_N("pwr_dc2_rawMax_mV", 6000, 0, 10000, "mV",
           "High-end of the raw measurement", "Set according to voltage divider setup"),
    PF32_N("pwr_dc2_engMin_V", 20.0f, 0.0f, 48.0f, 1, "V",
           "Low-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PF32_N("pwr_dc2_engMax_V", 30.0f, 0.0f, 48.0f, 1, "V",
           "High-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PU16_N("pwr_dc2_avgTime_ms", 1000, 10, 10000, "ms",
           "Rolling average for filtering this DC bus voltage", "Set according to voltage divider setup"),
    PU16_N("pwr_ups_bat_rawMin_mV", 4000, 0, 10000, "mV",
           "Low-end of the raw measurement", "Set according to voltage divider setup"),
    PU16_N("pwr_ups_bat_rawMax_mV", 6000, 0, 10000, "mV",
           "High-end of the raw measurement", "Set according to voltage divider setup"),
    PF32_N("pwr_ups_bat_engMin_V", 20.0f, 0.0f, 48.0f, 1, "V",
           "Low-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PF32_N("pwr_ups_bat_engMax_V", 30.0f, 0.0f, 48.0f, 1, "V",
           "High-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PU16_N("pwr_ups_bat_avgTime_ms", 1000, 10, 10000, "ms",
           "Rolling average for filtering this DC bus voltage", "Set according to voltage divider setup"),
    PU16_N("pwr_gen_bat_rawMin_mV", 5000, 0, 10000, "mV",
           "Low-end of the raw measurement", "Set according to voltage divider setup"),
    PU16_N("pwr_gen_bat_rawMax_mV", 7500, 0, 10000, "mV",
           "High-end of the raw measurement", "Set according to voltage divider setup"),
    PF32_N("pwr_gen_bat_engMin_V", 20.0f, 10.0f, 48.0f, 1, "V",
           "Low-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PF32_N("pwr_gen_bat_engMax_V", 30.0f, 15.0f, 48.0f, 1, "V",
           "High-end of the actual DC voltage band", "Set according to voltage divider setup"),
    PU16_N("pwr_gen_bat_avgTime_ms", 1000, 10, 10000, "ms",
           "Rolling average for filtering this DC bus voltage", "Set according to voltage divider setup"),
};
const uint16_t g_var_count = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));

#undef MTR
#undef MTR_N
#undef COIL
#undef COIL_N
#undef IBOOL
#undef IBOOL_N
#undef IREAL
#undef IREAL_N
#undef PSERIAL
#undef PSERIAL_N
#undef PBOOL
#undef PBOOL_N
#undef PU16
#undef PU16_N
#undef PF32
#undef PF32_N
#undef PU32
#undef PU32_N
#undef PU8
#undef PU8_N
#undef PI16
#undef PI16_N
#undef PSTR
#undef PSTR_N

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
    V_ATS_VAR, V_ATS_VA,
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
    V_ALARM_DC_PS1, V_ALARM_GEN, V_ALARM_AC_LOCAL,
    V_ALARM_DC_PS2, V_ALARM_DC_UPS, V_ALARM_DC_LOCAL,
    V_ALARM_GENERATOR_BATTERY, V_ALARM_PLC, V_ALARM_NET_FAULT,
    V_ALARM_NTP_TIMEOUT, V_ALARM_ACTUATOR_FAULT, V_ALARM_SENSOR_FAULT,
    V_ALARM_TOO_HOT, V_ALARM_TOO_COLD, V_ALARM_TOO_HUMID,
    V_ALARM_TMP_STR, V_ALARM_WATER_LEAK, V_ALARM_WATER_PRESSURE,
    V_ALARM_WATER_CLOG, V_ALARM_IO_DROP, V_ALARM_LOG_FLOOD,
    V_ALARM_WEATHER_STORM, V_SYS_SUPPLY_LOCKOUT, V_SYS_SUPPLY_HZ,
    V_NET_SERIAL,
    V_NET_DHCP,
    V_NET_IP,
    V_NET_MASK,
    V_NET_DNS1,
    V_NET_DNS2,
    V_NET_GW,
    V_NTP_SERIAL,
    V_NTP_ENABLE,
    V_NTP_POLL_SEC,
    V_NTP_SRV1,
    V_NTP_SRV2,
    V_MQTT_SERIAL,
    V_MQTT_ENABLE,
    V_MQTT_HOST,
    V_MQTT_PORT,
    V_MQTT_KA_SEC,
    V_GEO_SERIAL,
    V_GEO_LAT_DEG,
    V_GEO_LON_DEG,
    V_CLI_COLDBANKOFFSET_DEGC,
    V_CLI_HEATBANKOFFSET_DEGC,
    V_CLI_HEATBANKTRIG_DEGC,
    V_CLI_COLDBANKTRIG_DEGC,
    V_CLI_COOLON_DEGC,
    V_CLI_COOLKP,
    V_CLI_COOLTI_SEC,
    V_CLI_COOLTD_SEC,
    V_CLI_VENTMARGIN_DEGC,
    V_CLI_SKIRTAVAIL_DEGC,
    V_CLI_THERMALSCREENDEM,
    V_CLI_GROUNDHEATDEM,
    V_CLI_WINOPENDEM,
    V_CLI_INTAKESTARTDEM,
    V_CLI_WINFULLDEM,
    V_CLI_SKIRTENABLEDEM,
    V_CLI_FANDEMLOW,
    V_CLI_FANDEMMED,
    V_CLI_FANDEMHIGH,
    V_CLI_INTAKEFULLDEM,
    V_CLI_DTSTRATLO_DEGC,
    V_CLI_DTSTRATMED_DEGC,
    V_CLI_DTSTRATHI_DEGC,
    V_CLI_DTSTRATHYST_DEGC,
    V_CLI_ROT_DPLOW_DEGC,
    V_CLI_ROT_DPHIGH_DEGC,
    V_CLI_HUMIDLO_DEGC,
    V_CLI_HUMIDHI_DEGC,
    V_CLI_HUMIDSKIRTDEM,
    V_CLI_RHCAP,
    V_CLI_DPVENTMARGIN_DEGC,
    V_CLI_TOOCOLD_DEGC,
    V_CLI_TOOHOT_DEGC,
    V_CLI_ROTFLT_DEGC,
    V_CLI_INTRACTOR_RAWMIN_MV,
    V_CLI_INTRACTOR_RAWMAX_MV,
    V_CLI_INTRACTOR_ENGMIN,
    V_CLI_INTRACTOR_ENGMAX,
    V_CLI_WINDLOCKOUT_MPS,
    V_CLI_WINDOWTRAVEL_SEC,
    V_CLI_WINDOWTIMEOUT_SEC,
    V_CLI_SCREENTRAVEL_SEC,
    V_CLI_SCREENTIMEOUT_SEC,
    V_CLI_RAINLOCKOUT_SEC,
    V_CLI_SKIRTVALVEPERIOD_SEC,
    V_CLI_SKIRTVALVEMINON_SEC,
    V_CLI_SCREENDEPLOY_DEGC,
    V_SOL_COLDBANKSUNRISE_MIN,
    V_SOL_DAWNBREEZESUNRISE_MIN,
    V_SOL_DAYSUNRISE_MIN,
    V_SOL_HEATBANKSUNSET_MIN,
    V_SOL_NIGHTSUNSET_MIN,
    V_IRR_FORCENOW,
    V_IRR_LITERS_TRAY1,
    V_IRR_DAYS_TRAY1,
    V_IRR_LAST_TRAY1,
    V_IRR_FORCEON_TRAY1,
    V_IRR_FORCEOFF_TRAY1,
    V_IRR_LITERS_TRAY2,
    V_IRR_DAYS_TRAY2,
    V_IRR_LAST_TRAY2,
    V_IRR_FORCEON_TRAY2,
    V_IRR_FORCEOFF_TRAY2,
    V_IRR_LITERS_TRAY3,
    V_IRR_DAYS_TRAY3,
    V_IRR_LAST_TRAY3,
    V_IRR_FORCEON_TRAY3,
    V_IRR_FORCEOFF_TRAY3,
    V_IRR_LITERS_TRAY4,
    V_IRR_DAYS_TRAY4,
    V_IRR_LAST_TRAY4,
    V_IRR_FORCEON_TRAY4,
    V_IRR_FORCEOFF_TRAY4,
    V_IRR_LITERS_TRAY5,
    V_IRR_DAYS_TRAY5,
    V_IRR_LAST_TRAY5,
    V_IRR_FORCEON_TRAY5,
    V_IRR_FORCEOFF_TRAY5,
    V_IRR_LITERS_TRAY6,
    V_IRR_DAYS_TRAY6,
    V_IRR_LAST_TRAY6,
    V_IRR_FORCEON_TRAY6,
    V_IRR_FORCEOFF_TRAY6,
    V_IRR_LITERS_TRAY7,
    V_IRR_DAYS_TRAY7,
    V_IRR_LAST_TRAY7,
    V_IRR_FORCEON_TRAY7,
    V_IRR_FORCEOFF_TRAY7,
    V_IRR_LITERS_TRAY8,
    V_IRR_DAYS_TRAY8,
    V_IRR_LAST_TRAY8,
    V_IRR_FORCEON_TRAY8,
    V_IRR_FORCEOFF_TRAY8,
    V_IRR_DRYEWMADAYS,
    V_IRR_DRYLO_KPA,
    V_IRR_DRYHI_KPA,
    V_IRR_MODMIN,
    V_IRR_MODGAMMA,
    V_IRR_VOLFREQSPLIT,
    V_IRR_TRAYSPERDAY,
    V_IRR_DAWNGATE_DEGC,
    V_IRR_MAXFILL_SEC,
    V_IRR_DRAINWINDOW_SEC,
    V_IRR_EXPDRAINFRAC,
    V_IRR_DRAINMARGIN,
    V_IRR_FLUSH_SEC,
    V_IRR_INPUT_QTYPERIMP,
    V_IRR_OUTPUT_RAWMIN_UA,
    V_IRR_OUTPUT_RAWMAX_UA,
    V_IRR_OUTPUT_ENGMIN_LPM,
    V_IRR_OUTPUT_ENGMAX_LPM,
    V_IRR_OUTPUT_AVGTIME_MS,
    V_PWR_DC1_RAWMIN_MV,
    V_PWR_DC1_RAWMAX_MV,
    V_PWR_DC1_ENGMIN_V,
    V_PWR_DC1_ENGMAX_V,
    V_PWR_DC1_AVGTIME_MS,
    V_PWR_DC2_RAWMIN_MV,
    V_PWR_DC2_RAWMAX_MV,
    V_PWR_DC2_ENGMIN_V,
    V_PWR_DC2_ENGMAX_V,
    V_PWR_DC2_AVGTIME_MS,
    V_PWR_UPS_BAT_RAWMIN_MV,
    V_PWR_UPS_BAT_RAWMAX_MV,
    V_PWR_UPS_BAT_ENGMIN_V,
    V_PWR_UPS_BAT_ENGMAX_V,
    V_PWR_UPS_BAT_AVGTIME_MS,
    V_PWR_GEN_BAT_RAWMIN_MV,
    V_PWR_GEN_BAT_RAWMAX_MV,
    V_PWR_GEN_BAT_ENGMIN_V,
    V_PWR_GEN_BAT_ENGMAX_V,
    V_PWR_GEN_BAT_AVGTIME_MS,
    V_COUNT
};

/** @name Value store (RAM, parallel to the table)
 *  @{ */

extern Value g_var_values[V_COUNT];
/** @brief T_STR live text: one STR_CAP slot per text parameter, addressed by
 *  Variable::str_off. The one place a byte offset survives, sized exactly by
 *  the generator and asserted below. */
extern char  g_str_pool[STR_POOL_BYTES];

/** @} */

/** @brief The table bound to its store, as a brace-initialiser.
 *
 *  A macro rather than a function so the site's SiteDescriptor stays a
 *  compile-time constant in flash: any function call in that initialiser makes
 *  it dynamic and moves the whole descriptor into .data.
 *
 *  The trailing pair is the ROLE_PARAM block: a contiguous run at the end of
 *  the table, so the parameter pages address it with a dense 0-based index and
 *  one add, and adding a parameter never moves a field-IO index. */
#define SITE_VAR_TABLES { \
    g_vars, g_var_values, V_COUNT, \
    g_str_pool, STR_POOL_BYTES, \
    V_NET_SERIAL, (uint16_t)(V_COUNT - V_NET_SERIAL) }

// The enum must stay in step with the table it indexes; a mismatch would
// silently read the wrong signal.
static_assert(sizeof(g_vars) / sizeof(g_vars[0]) == (size_t)V_COUNT,
              "g_vars and VarId disagree: an index would address the wrong variable");

// The pool must cover every text parameter's slot; a short pool would let one
// silently overlap its neighbour.
static_assert(STR_POOL_BYTES >= 8 * STR_CAP, "the string pool is too small");

}  // namespace Site

#endif // SITE_VARS_H
