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

// Every row macro ends with a NOTES string - the longer story the detail panels
// show under Description, which a table column has no room for. It is not
// optional: an empty "" says "nothing more to add" in the same shape as a real
// note, where a second _N macro per row type meant two ways to spell every row
// and a reader having to know which one they were looking at.
//
// A meter reading. Bands are generous on purpose - they exist to catch a dead
// or unplugged meter reporting a rail, not to police the process. `dgt` is the
// display precision: a power factor to 0 decimals is useless, a watt to 2 is
// noise.
#define MTR(nm, unit, dgt, dev, obj, lo, hi, desc, notes) \
    { Scalar::F(lo), Scalar::F(hi), Scalar::F(0.0f), nm, unit, desc, notes, nullptr, \
      { XP_MODBUS, T_F32, dev, 3, obj, 2, 1.0f, 0.0f }, \
      5000, 0, 0, T_F32, ROLE_INPUT, dgt, STR_NONE, F_BOUNDED, {} }

// A relay coil, written with function code 5.
#define COIL(nm, dev, coil, desc, notes) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(false), nm, "", desc, notes, nullptr, \
      { XP_MODBUS, T_BOOL, dev, 5, coil, 1, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_OUTPUT, 0, STR_NONE, 0, {} }

// A computed boolean: an alarm condition or a latched state flag.
#define IBOOL(nm, desc, notes) \
    { Scalar::B(false), Scalar::B(true), Scalar::B(false), nm, "", desc, notes, nullptr, \
      { XP_NONE, T_BOOL, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_BOOL, ROLE_INTERNAL, 0, STR_NONE, 0, {} }

// A computed real.
#define IREAL(nm, unit, dgt, desc, notes) \
    { Scalar::F(0.0f), Scalar::F(0.0f), Scalar::F(0.0f), nm, unit, desc, notes, nullptr, \
      { XP_NONE, T_F32, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, 0, 0, T_F32, ROLE_INTERNAL, dgt, STR_NONE, 0, {} }

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
/** @brief Every parameter, one shape. The eight type-specific macros it
 *  replaced each had their own column set, so the block could not be read as a
 *  single table and every row type needed its own heading.
 *
 *  Columns that do not apply to a type are still written: `dgt` is display
 *  decimals (T_F32 only), `off`/`kind` are the string pool slot and its edit
 *  validation (T_STR only). They cost a literal each and buy one layout.
 *
 *  PU/PI/PF/PS build the typed Scalar - a bare literal cannot, since the union
 *  takes uint64/int64/float/const char* equally well. */
#define PU(v) Scalar::U(v)
#define PI(v) Scalar::I(v)
#define PF(v) Scalar::F(v)
#define PS(v) Scalar::S(v)
#define PARAM(nm, ty, dflt, lo, hi, dgt, unit, off, kind, desc, notes) \
    { lo, hi, dflt, nm, unit, desc, notes, nullptr, \
      { XP_NONE, ty, 0, 0, 0, 0, 1.0f, 0.0f }, \
      0, off, STR_CAP, ty, ROLE_PARAM, dgt, kind, F_BOUNDED, {} }


/** @brief Every variable this site declares, in one table.
 *
 *  Plain `const`, so each including TU gets its own copy; two TUs include this
 *  site in the firmware build, so the table costs its ~7 KB twice. Moving the
 *  rows into vars.cpp behind an `extern const` would fix that, and is deferred
 *  only because it restructures the generated file. */
const Variable g_vars[] = {


/** @name Parameters - persisted and editable; the PARAMS page and `set` write these
 *  @{ */
    //     name                         type    dflt                     lo           hi              dgt  unit    off  kind          description                                                            notes
    PARAM("net_serial",                T_U32,  PU(1),                   PU(0),       PU(1000000000), 0,   "",     0,   STR_NONE,     "Network config commit",                                               "bump to apply staged net_* edits"),
    PARAM("net_dhcp",                  T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Enable DHCP IP configuration",                                        "ignores the static ip/mask/gw rows"),
    PARAM("net_ip",                    T_STR,  PS("192.168.1.42"),      PU(0),       PU(0),          0,   "",     0,   STR_IPV4,     "Static IP address",                                                   ""),
    PARAM("net_mask",                  T_STR,  PS("255.255.255.0"),     PU(0),       PU(0),          0,   "",     64,  STR_IPV4,     "Static IP netmask",                                                   ""),
    PARAM("net_dns1",                  T_STR,  PS("8.8.8.8"),           PU(0),       PU(0),          0,   "",     128, STR_IPV4,     "Static DNS resolver 1",                                               ""),
    PARAM("net_dns2",                  T_STR,  PS("4.4.4.4"),           PU(0),       PU(0),          0,   "",     192, STR_IPV4_OPT, "Static DNS resolver 2",                                               "empty = use dns1 alone"),
    PARAM("net_gw",                    T_STR,  PS("192.168.1.1"),       PU(0),       PU(0),          0,   "",     256, STR_IPV4,     "Static IP gateway",                                                   ""),
    PARAM("ntp_serial",                T_U32,  PU(1),                   PU(0),       PU(1000000000), 0,   "",     0,   STR_NONE,     "NTP config commit",                                                   ""),
    PARAM("ntp_enable",                T_BOOL, PU(1),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Enable NTP time synchronization",                                     ""),
    PARAM("ntp_poll_sec",              T_U32,  PU(3600),                PU(10),      PU(86400),      0,   "sec",  0,   STR_NONE,     "NTP poll interval, in seconds",                                       ""),
    PARAM("ntp_srv1",                  T_STR,  PS("0.fr.pool.ntp.org"), PU(0),       PU(0),          0,   "",     320, STR_HOST,     "NTP server 1",                                                        ""),
    PARAM("ntp_srv2",                  T_STR,  PS("1.fr.pool.ntp.org"), PU(0),       PU(0),          0,   "",     384, STR_HOST_OPT, "NTP server 2",                                                        "empty = no fallback"),
    PARAM("mqtt_serial",               T_U32,  PU(1),                   PU(0),       PU(1000000000), 0,   "",     0,   STR_NONE,     "MQTT config commit",                                                  ""),
    PARAM("mqtt_enable",               T_BOOL, PU(1),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Enable MQTT",                                                         ""),
    PARAM("mqtt_host",                 T_STR,  PS("192.168.1.43"),      PU(0),       PU(0),          0,   "",     448, STR_HOST,     "MQTT broker host",                                                    ""),
    PARAM("mqtt_port",                 T_U16,  PU(58443),               PU(0),       PU(65535),      0,   "",     0,   STR_NONE,     "MQTT broker port",                                                    ""),
    PARAM("mqtt_ka_sec",               T_U16,  PU(20),                  PU(10),      PU(1800),       0,   "sec",  0,   STR_NONE,     "MQTT keep-alive interval",                                            "also the dead-broker detection window"),
    PARAM("geo_serial",                T_U32,  PU(1),                   PU(0),       PU(1000000000), 0,   "",     0,   STR_NONE,     "Geo config commit",                                                   ""),
    PARAM("geo_lat_deg",               T_F32,  PF(48.07f),              PF(-90.0f),  PF(90.0f),      2,   "deg",  0,   STR_NONE,     "Geographical latitude, in degrees",                                   "per site location"),
    PARAM("geo_lon_deg",               T_F32,  PF(1.32f),               PF(-180.0f), PF(180.0f),     2,   "deg",  0,   STR_NONE,     "Geographical longitude, in degrees",                                  "per site location"),
    PARAM("cli_coldBankOffset_degC",   T_F32,  PF(6.0f),                PF(0.0f),    PF(15.0f),      1,   "degC", 0,   STR_NONE,     "How much cooler to run before a hot day",                             ""),
    PARAM("cli_heatBankOffset_degC",   T_F32,  PF(7.0f),                PF(0.0f),    PF(15.0f),      1,   "degC", 0,   STR_NONE,     "How much warmer to run before cold night",                            ""),
    PARAM("cli_heatBankTrig_degC",     T_F32,  PF(10.0f),               PF(6.0f),    PF(18.0f),      1,   "degC", 0,   STR_NONE,     "If yesterday's lowest temp below this, we bank heat before sunset",   ""),
    PARAM("cli_coldBankTrig_degC",     T_F32,  PF(24.0f),               PF(12.0f),   PF(40.0f),      1,   "degC", 0,   STR_NONE,     "If yesterday's highest temp above this, we pre-chill before sunrise", ""),
    PARAM("cli_coolOn_degC",           T_F32,  PF(20.0f),               PF(10.0f),   PF(30.0f),      1,   "degC", 0,   STR_NONE,     "Base cooling target temperature",                                     "every cooling stage keys off this"),
    PARAM("cli_coolKp",                T_F32,  PF(0.08f),               PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling PID proportional factor",                                     "Leave as set"),
    PARAM("cli_coolTi_sec",            T_F32,  PF(200.0f),              PF(0.0f),    PF(3600.0f),    0,   "sec",  0,   STR_NONE,     "Cooling PID integral factor",                                         "Leave as set"),
    PARAM("cli_coolTd_sec",            T_F32,  PF(0.0f),                PF(0.0f),    PF(10.0f),      0,   "sec",  0,   STR_NONE,     "Cooling PID derivative factor",                                       "leave at zero"),
    PARAM("cli_ventMargin_degC",       T_F32,  PF(1.0f),                PF(0.0f),    PF(15.0f),      1,   "degC", 0,   STR_NONE,     "Intake air must be this much cooler",                                 ""),
    PARAM("cli_skirtAvail_degC",       T_F32,  PF(4.0f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Air must be this dry for the pad to cool",                            ""),
    PARAM("cli_thermalScreenDem",      T_F32,  PF(-0.2f),               PF(-1.0f),   PF(0.0f),       2,   "",     0,   STR_NONE,     "Cooling effort at which we deploy the thermal screen",                "negative demand = heating"),
    PARAM("cli_groundHeatDem",         T_F32,  PF(-0.1f),               PF(-1.0f),   PF(0.0f),       2,   "",     0,   STR_NONE,     "Cooling effort at which we try to take hot air from below",           "negative demand = heating"),
    PARAM("cli_winOpenDem",            T_F32,  PF(0.1f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort where we begin opening windows",                       ""),
    PARAM("cli_intakeStartDem",        T_F32,  PF(0.3f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort where intake fans start",                              ""),
    PARAM("cli_winFullDem",            T_F32,  PF(0.5f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort at fully open windows",                                ""),
    PARAM("cli_skirtEnableDem",        T_F32,  PF(0.7f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort where the wet skirt starts",                           ""),
    PARAM("cli_fanDemLow",             T_F32,  PF(0.2f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort for fan speed 1",                                      ""),
    PARAM("cli_fanDemMed",             T_F32,  PF(0.4f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort for fan speed 2",                                      "> cli_fanDemLow"),
    PARAM("cli_fanDemHigh",            T_F32,  PF(0.8f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort for fan speed 3",                                      "> cli_fanDemMed"),
    PARAM("cli_intakeFullDem",         T_F32,  PF(0.9f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Cooling effort at full speed intake fans",                            ""),
    PARAM("cli_dTStratLo_degC",        T_F32,  PF(2.0f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Top-to-bottom stratification for fan speed 1",                        ""),
    PARAM("cli_dTStratMed_degC",       T_F32,  PF(4.0f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Top-to-bottom stratification for fan speed 2",                        ""),
    PARAM("cli_dTStratHi_degC",        T_F32,  PF(6.0f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Top-to-bottom stratification for fan speed 3",                        ""),
    PARAM("cli_dTStratHyst_degC",      T_F32,  PF(2.0f),                PF(0.5f),    PF(5.0f),       1,   "degC", 0,   STR_NONE,     "Gap change needed to switch fan speed",                               ""),
    PARAM("cli_rot_dpLow_degC",        T_F32,  PF(3.0f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Dryness margin where dehumidifying is fully activated",               ""),
    PARAM("cli_rot_dpHigh_degC",       T_F32,  PF(4.5f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Dryness margin where dehumidifying starts",                           "> cli_rot_dpLow_degC"),
    PARAM("cli_humidLo_degC",          T_F32,  PF(18.0f),               PF(0.0f),    PF(30.0f),      1,   "degC", 0,   STR_NONE,     "Dryness margin where humidifying starts",                             "> cli_rot_dpHigh_degC"),
    PARAM("cli_humidHi_degC",          T_F32,  PF(28.0f),               PF(0.0f),    PF(40.0f),      1,   "degC", 0,   STR_NONE,     "Dryness margin where humidifying is fully activated",                 "> cli_humidLo_degC"),
    PARAM("cli_humidSkirtDem",         T_F32,  PF(0.5f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Dryness level where we humidify through the skirt",                   ""),
    PARAM("cli_rhCap",                 T_F32,  PF(0.6f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Humidity that also starts rot defence",                               ""),
    PARAM("cli_dpVentMargin_degC",     T_F32,  PF(1.0f),                PF(0.0f),    PF(5.0f),       1,   "degC", 0,   STR_NONE,     "Outside air must be this much drier to vent",                         ""),
    PARAM("cli_tooCold_degC",          T_F32,  PF(4.0f),                PF(0.0f),    PF(10.0f),      1,   "degC", 0,   STR_NONE,     "Alarm: greenhouse is dangerously cold",                               "match the gas-heater thermostat"),
    PARAM("cli_tooHot_degC",           T_F32,  PF(35.0f),               PF(20.0f),   PF(50.0f),      1,   "degC", 0,   STR_NONE,     "Alarm: greenhouse is dangerously hot",                                ""),
    PARAM("cli_rotFlt_degC",           T_F32,  PF(1.0f),                PF(0.0f),    PF(5.0f),       1,   "degC", 0,   STR_NONE,     "Alarm: greenhouse is dangerously humid",                              "match the dehumidifier setting"),
    PARAM("cli_intractor_rawMin_mV",   T_F32,  PF(0.0f),                PF(0.0f),    PF(24000.0f),   0,   "mV",   0,   STR_NONE,     "Intractor raw low-end value (zero flow)",                             "per intractor datasheet"),
    PARAM("cli_intractor_rawMax_mV",   T_F32,  PF(10000.0f),            PF(0.0f),    PF(24000.0f),   0,   "mV",   0,   STR_NONE,     "Intractor raw high-end value (max flow)",                             "per intractor datasheet"),
    PARAM("cli_intractor_engMin",      T_F32,  PF(0.0f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Intractor speed at the low-end of the range",                         "per intractor datasheet"),
    PARAM("cli_intractor_engMax",      T_F32,  PF(1.0f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Intractor speed at the high-end of the range",                        ""),
    PARAM("cli_windLockout_mps",       T_F32,  PF(5.0f),                PF(0.0f),    PF(50.0f),      1,   "mps",  0,   STR_NONE,     "Wind speed that forces windows shut",                                 ""),
    PARAM("cli_windowTravel_sec",      T_U16,  PU(15),                  PU(1),       PU(60),         0,   "sec",  0,   STR_NONE,     "Nominal time to fully open/close a window",                           "per actuator datasheet"),
    PARAM("cli_windowTimeout_sec",     T_U16,  PU(20),                  PU(1),       PU(90),         0,   "sec",  0,   STR_NONE,     "Safe time to fully open/close a window",                              "per actuator datasheet"),
    PARAM("cli_screenTravel_sec",      T_U16,  PU(60),                  PU(1),       PU(300),        0,   "sec",  0,   STR_NONE,     "Nominal time to deploy/remove the thermal screen",                    "per actuator datasheet"),
    PARAM("cli_screenTimeout_sec",     T_U16,  PU(80),                  PU(1),       PU(400),        0,   "sec",  0,   STR_NONE,     "Safe time to deploy/remove the thermal screen",                       "per actuator datasheet"),
    PARAM("cli_rainLockout_sec",       T_U16,  PU(900),                 PU(0),       PU(3600),       0,   "sec",  0,   STR_NONE,     "Keep windows shut this long after rain",                              ""),
    PARAM("cli_skirtValvePeriod_sec",  T_U16,  PU(60),                  PU(10),      PU(600),        0,   "sec",  0,   STR_NONE,     "Period for the skirt humidifier valve modulation",                    ""),
    PARAM("cli_skirtValveMinOn_sec",   T_U16,  PU(10),                  PU(1),       PU(600),        0,   "sec",  0,   STR_NONE,     "Shortest allowed pulse length for the skirt humidifier",              "<= cli_skirtValvePeriod_sec"),
    PARAM("cli_screenDeploy_degC",     T_F32,  PF(13.0f),               PF(0.0f),    PF(50.0f),      1,   "degC", 0,   STR_NONE,     "Night temperature that deploys the thermal screens",                  ""),
    PARAM("sol_coldBankSunrise_min",   T_I16,  PI(-30),                 PI(-180),    PI(60),         0,   "min",  0,   STR_NONE,     "Begin time of cold banking, minutes from sunrise",                    ""),
    PARAM("sol_dawnBreezeSunrise_min", T_I16,  PI(30),                  PI(-60),     PI(180),        0,   "min",  0,   STR_NONE,     "Begin time of dawn breeze, minutes from sunrise",                     ""),
    PARAM("sol_daySunrise_min",        T_I16,  PI(60),                  PI(0),       PI(180),        0,   "min",  0,   STR_NONE,     "Begin time of day operation, minutes from sunrise",                   ""),
    PARAM("sol_heatBankSunset_min",    T_I16,  PI(-180),                PI(-300),    PI(0),          0,   "min",  0,   STR_NONE,     "Begin time of heat banking, minutes from sunset",                     ""),
    PARAM("sol_nightSunset_min",       T_I16,  PI(30),                  PI(0),       PI(180),        0,   "min",  0,   STR_NONE,     "Begin time of night operation, minutes from sunset",                  ""),
    PARAM("irr_forceNow",              T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Run a watering cycle immediately, once",                              ""),
    PARAM("irr_liters_tray1",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray1",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray1",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray1",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray1",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray2",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray2",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray2",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray2",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray2",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray3",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray3",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray3",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray3",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray3",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray4",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray4",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray4",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray4",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray4",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray5",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray5",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray5",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray5",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray5",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray6",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray6",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray6",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray6",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray6",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray7",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray7",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray7",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray7",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray7",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_liters_tray8",          T_U16,  PU(150),                 PU(0),       PU(500),        0,   "L",    0,   STR_NONE,     "Litres to flood this tray in summer",                                 ""),
    PARAM("irr_days_tray8",            T_U8,   PU(14),                  PU(1),       PU(60),         0,   "days", 0,   STR_NONE,     "Days between waterings in summer",                                    ""),
    PARAM("irr_last_tray8",            T_U32,  PU(0),                   PU(0),       PU(100000),     0,   "days", 0,   STR_NONE,     "Last watering (days since 1 Jan 1970)",                               ""),
    PARAM("irr_forceOn_tray8",         T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force watering this tray at the next cycle",                          ""),
    PARAM("irr_forceOff_tray8",        T_BOOL, PU(0),                   PU(0),       PU(1),          0,   "",     0,   STR_NONE,     "Force skipping this tray at the next cycle",                          ""),
    PARAM("irr_dryEwmaDays",           T_U16,  PU(3),                   PU(1),       PU(30),         0,   "days", 0,   STR_NONE,     "Days of dryness history to modulate the watering",                    "sampled every 2 h"),
    PARAM("irr_dryLo_kPa",             T_F32,  PF(0.15f),               PF(0.0f),    PF(2.0f),       2,   "kPa",  0,   STR_NONE,     "Air dryness giving the least watering",                               ""),
    PARAM("irr_dryHi_kPa",             T_F32,  PF(1.2f),                PF(0.0f),    PF(5.0f),       2,   "kPa",  0,   STR_NONE,     "Air dryness giving the most watering",                                "> irr_dryLo_kPa"),
    PARAM("irr_modMin",                T_F32,  PF(0.25f),               PF(0.1f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Minimum fraction of water given in very humid weather",               "the floor even in still, humid air"),
    PARAM("irr_modGamma",              T_F32,  PF(1.0f),                PF(1.0f),    PF(4.0f),       2,   "",     0,   STR_NONE,     "How fast watering ramps up with dryness",                             ""),
    PARAM("irr_volFreqSplit",          T_F32,  PF(0.5f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Whether we reduce volume or increase period",                         "0 = period, 1 = volume"),
    PARAM("irr_traysPerDay",           T_U16,  PU(2),                   PU(1),       PU(5),          0,   "",     0,   STR_NONE,     "Most trays watered in one day",                                       ""),
    PARAM("irr_dawnGate_degC",         T_F32,  PF(12.0f),               PF(5.0f),    PF(30.0f),      1,   "degC", 0,   STR_NONE,     "Skip pre-dawn watering below this temp",                              ""),
    PARAM("irr_maxFill_sec",           T_U16,  PU(300),                 PU(100),     PU(1800),       0,   "sec",  0,   STR_NONE,     "Alarm if a tray takes longer to fill",                                ""),
    PARAM("irr_drainWindow_sec",       T_U16,  PU(300),                 PU(100),     PU(1800),       0,   "sec",  0,   STR_NONE,     "Time allowed for a tray to drain",                                    ""),
    PARAM("irr_expDrainFrac",          T_F32,  PF(0.6f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "How far below expected drain fraction is a fault",                    ""),
    PARAM("irr_drainMargin",           T_F32,  PF(0.3f),                PF(0.0f),    PF(1.0f),       2,   "",     0,   STR_NONE,     "Drain volume fraction below which we trigger a drain fault",          ""),
    PARAM("irr_flush_sec",             T_U16,  PU(10),                  PU(5),       PU(300),        0,   "sec",  0,   STR_NONE,     "Duration of the flush after/before the irrigation cycle",             ""),
    PARAM("irr_input_qtyPerImp",       T_F32,  PF(1.0f),                PF(0.01f),   PF(10.0f),      2,   "L",    0,   STR_NONE,     "Litres per pulse from the water meter",                               "per meter datasheet"),
    PARAM("irr_output_rawMin_uA",      T_U16,  PU(0),                   PU(0),       PU(24000),      0,   "uA",   0,   STR_NONE,     "uA at zero output flow",                                              "per output-meter datasheet"),
    PARAM("irr_output_rawMax_uA",      T_U16,  PU(10000),               PU(0),       PU(24000),      0,   "uA",   0,   STR_NONE,     "uA at full output scale",                                             "per output-meter datasheet"),
    PARAM("irr_output_engMin_Lpm",     T_F32,  PF(0.0f),                PF(0.0f),    PF(500.0f),     1,   "Lpm",  0,   STR_NONE,     "Output litres/min at zero signal",                                    "per output-meter datasheet"),
    PARAM("irr_output_engMax_Lpm",     T_F32,  PF(50.0f),               PF(0.0f),    PF(500.0f),     1,   "Lpm",  0,   STR_NONE,     "Output litres/min at full scale",                                     "per output-meter datasheet"),
    PARAM("irr_output_avgTime_ms",     T_U16,  PU(200),                 PU(0),       PU(10000),      0,   "ms",   0,   STR_NONE,     "Rolling average for filtering this flow meter's output",              "per output-meter datasheet"),
    PARAM("pwr_dc1_rawMin_mV",         T_U16,  PU(4000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "Low-end of the raw measurement",                                      "per divider setup"),
    PARAM("pwr_dc1_rawMax_mV",         T_U16,  PU(6000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "High-end of the raw measurement",                                     "per divider setup"),
    PARAM("pwr_dc1_engMin_V",          T_F32,  PF(20.0f),               PF(0.0f),    PF(48.0f),      1,   "V",    0,   STR_NONE,     "Low-end of the actual DC voltage band",                               "per divider setup"),
    PARAM("pwr_dc1_engMax_V",          T_F32,  PF(30.0f),               PF(0.0f),    PF(48.0f),      1,   "V",    0,   STR_NONE,     "High-end of the actual DC voltage band",                              "per divider setup"),
    PARAM("pwr_dc1_avgTime_ms",        T_U16,  PU(1000),                PU(10),      PU(10000),      0,   "ms",   0,   STR_NONE,     "Rolling average for filtering this DC bus voltage",                   "per divider setup"),
    PARAM("pwr_dc2_rawMin_mV",         T_U16,  PU(4000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "Low-end of the raw measurement",                                      "per divider setup"),
    PARAM("pwr_dc2_rawMax_mV",         T_U16,  PU(6000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "High-end of the raw measurement",                                     "per divider setup"),
    PARAM("pwr_dc2_engMin_V",          T_F32,  PF(20.0f),               PF(0.0f),    PF(48.0f),      1,   "V",    0,   STR_NONE,     "Low-end of the actual DC voltage band",                               "per divider setup"),
    PARAM("pwr_dc2_engMax_V",          T_F32,  PF(30.0f),               PF(0.0f),    PF(48.0f),      1,   "V",    0,   STR_NONE,     "High-end of the actual DC voltage band",                              "per divider setup"),
    PARAM("pwr_dc2_avgTime_ms",        T_U16,  PU(1000),                PU(10),      PU(10000),      0,   "ms",   0,   STR_NONE,     "Rolling average for filtering this DC bus voltage",                   "per divider setup"),
    PARAM("pwr_ups_bat_rawMin_mV",     T_U16,  PU(4000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "Low-end of the raw measurement",                                      "per divider setup"),
    PARAM("pwr_ups_bat_rawMax_mV",     T_U16,  PU(6000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "High-end of the raw measurement",                                     "per divider setup"),
    PARAM("pwr_ups_bat_engMin_V",      T_F32,  PF(20.0f),               PF(0.0f),    PF(48.0f),      1,   "V",    0,   STR_NONE,     "Low-end of the actual DC voltage band",                               "per divider setup"),
    PARAM("pwr_ups_bat_engMax_V",      T_F32,  PF(30.0f),               PF(0.0f),    PF(48.0f),      1,   "V",    0,   STR_NONE,     "High-end of the actual DC voltage band",                              "per divider setup"),
    PARAM("pwr_ups_bat_avgTime_ms",    T_U16,  PU(1000),                PU(10),      PU(10000),      0,   "ms",   0,   STR_NONE,     "Rolling average for filtering this DC bus voltage",                   "per divider setup"),
    PARAM("pwr_gen_bat_rawMin_mV",     T_U16,  PU(5000),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "Low-end of the raw measurement",                                      "per divider setup"),
    PARAM("pwr_gen_bat_rawMax_mV",     T_U16,  PU(7500),                PU(0),       PU(10000),      0,   "mV",   0,   STR_NONE,     "High-end of the raw measurement",                                     "per divider setup"),
    PARAM("pwr_gen_bat_engMin_V",      T_F32,  PF(20.0f),               PF(10.0f),   PF(48.0f),      1,   "V",    0,   STR_NONE,     "Low-end of the actual DC voltage band",                               "per divider setup"),
    PARAM("pwr_gen_bat_engMax_V",      T_F32,  PF(30.0f),               PF(15.0f),   PF(48.0f),      1,   "V",    0,   STR_NONE,     "High-end of the actual DC voltage band",                              "per divider setup"),
    PARAM("pwr_gen_bat_avgTime_ms",    T_U16,  PU(1000),                PU(10),      PU(10000),      0,   "ms",   0,   STR_NONE,     "Rolling average for filtering this DC bus voltage",                   "per divider setup"),
/** @} */

/** @name Sensor inputs - read from the field, invalid past their timeout
 *  @{ */
    //  name               unit   dgt  dev  obj  lo       hi      description                        notes
    MTR("grid_L1_V",       "V",   1,   5,   0,   0.0f,    500.0f, "L1 phase voltage",                ""),
    MTR("grid_L2_V",       "V",   1,   5,   1,   0.0f,    500.0f, "L2 phase voltage",                ""),
    MTR("grid_L3_V",       "V",   1,   5,   2,   0.0f,    500.0f, "L3 phase voltage",                ""),
    MTR("grid_L1_A",       "A",   1,   5,   3,   0.0f,    200.0f, "L1 current",                      ""),
    MTR("grid_L2_A",       "A",   1,   5,   4,   0.0f,    200.0f, "L2 current",                      ""),
    MTR("grid_L3_A",       "A",   1,   5,   5,   0.0f,    200.0f, "L3 current",                      ""),
    MTR("grid_L1_VA",      "VA",  0,   5,   6,   -1e6f,   1e6f,   "L1 apparent power",               ""),
    MTR("grid_L2_VA",      "VA",  0,   5,   7,   -1e6f,   1e6f,   "L2 apparent power",               ""),
    MTR("grid_L3_VA",      "VA",  0,   5,   8,   -1e6f,   1e6f,   "L3 apparent power",               ""),
    MTR("grid_L1_VAr",     "VAr", 0,   5,   9,   -1e6f,   1e6f,   "L1 reactive power",               ""),
    MTR("grid_L2_VAr",     "VAr", 0,   5,   10,  -1e6f,   1e6f,   "L2 reactive power",               ""),
    MTR("grid_L3_VAr",     "VAr", 0,   5,   11,  -1e6f,   1e6f,   "L3 reactive power",               ""),
    MTR("grid_P1_PF",      "",    3,   5,   12,  -1.0f,   1.0f,   "L1 power factor",                 ""),
    MTR("grid_P2_PF",      "",    3,   5,   13,  -1.0f,   1.0f,   "L2 power factor",                 ""),
    MTR("grid_P3_PF",      "",    3,   5,   14,  -1.0f,   1.0f,   "L3 power factor",                 ""),
    MTR("grid_P1_PA",      "deg", 1,   5,   15,  -180.0f, 180.0f, "L1 phase angle",                  ""),
    MTR("grid_P2_PA",      "deg", 1,   5,   16,  -180.0f, 180.0f, "L2 phase angle",                  ""),
    MTR("grid_P3_PA",      "deg", 1,   5,   17,  -180.0f, 180.0f, "L3 phase angle",                  ""),
    MTR("grid_LN_V",       "V",   1,   5,   18,  0.0f,    500.0f, "average line-to-neutral voltage", ""),
    MTR("grid_LL_V",       "V",   1,   5,   19,  0.0f,    900.0f, "average line-to-line voltage",    ""),
    MTR("grid_avg_A",      "A",   1,   5,   20,  0.0f,    200.0f, "average current",                 ""),
    MTR("grid_total_A",    "A",   1,   5,   21,  0.0f,    600.0f, "total current",                   ""),
    MTR("grid_total_W",    "W",   0,   5,   22,  -1e6f,   1e6f,   "total active power",              ""),
    MTR("grid_total_VA",   "VA",  0,   5,   23,  -1e6f,   1e6f,   "total apparent power",            ""),
    MTR("grid_total_VAR",  "VAr", 0,   5,   24,  -1e6f,   1e6f,   "total reactive power",            ""),
    MTR("grid_total_PF",   "",    3,   5,   25,  -1.0f,   1.0f,   "total power factor",              ""),
    MTR("grid_total_PA",   "deg", 1,   5,   26,  -180.0f, 180.0f, "total phase angle",               ""),
    MTR("grid_Hz",         "Hz",  2,   5,   27,  0.0f,    100.0f, "line frequency",                  ""),
    MTR("grid_12_V",       "V",   1,   5,   28,  0.0f,    900.0f, "L1-L2 voltage",                   ""),
    MTR("grid_L23_V",      "V",   1,   5,   29,  0.0f,    900.0f, "L2-L3 voltage",                   ""),
    MTR("grid_L31_V",      "V",   1,   5,   30,  0.0f,    900.0f, "L3-L1 voltage",                   ""),
    MTR("grid_LL_avg_THD", "%",   1,   5,   31,  0.0f,    100.0f, "line-to-line average THD",        ""),
    MTR("grid_LN_avg_THD", "%",   1,   5,   32,  0.0f,    100.0f, "line-to-neutral average THD",     ""),
    MTR("grid_N_A",        "A",   1,   5,   33,  0.0f,    200.0f, "neutral current",                 ""),
    MTR("grid_L1_W",       "W",   0,   5,   34,  -1e6f,   1e6f,   "L1 active power",                 ""),
    MTR("grid_L2_W",       "W",   0,   5,   35,  -1e6f,   1e6f,   "L2 active power",                 ""),
    MTR("grid_L3_W",       "W",   0,   5,   36,  -1e6f,   1e6f,   "L3 active power",                 ""),
    MTR("ats_V",           "V",   1,   6,   0,   0.0f,    500.0f, "ATS voltage",                     ""),
    MTR("ats_A",           "A",   1,   6,   1,   0.0f,    200.0f, "ATS current",                     ""),
    MTR("ats_VAr",         "VAr", 0,   6,   3,   -1e6f,   1e6f,   "ATS reactive power",              ""),
    MTR("ats_VA",          "VA",  0,   6,   4,   -1e6f,   1e6f,   "ATS apparent power",              ""),
    MTR("ats_Hz",          "Hz",  2,   6,   5,   0.0f,    100.0f, "ATS frequency",                   ""),
/** @} */

/** @name Computed values - derived state, shown on the HMI only
 *  @{ */
    //    name                       description                        notes
    IBOOL("alarm_ac_grid",           "Grid AC out-of-spec/offline",     ""),
    IBOOL("alarm_dc_ps1",            "DC PS1 volts too low",            ""),
    IBOOL("alarm_gen",               "Generator failed to start",       ""),
    IBOOL("alarm_ac_local",          "Local AC out of spec or offline", ""),
    IBOOL("alarm_dc_ps2",            "DC PS2 volts too low",            ""),
    IBOOL("alarm_dc_ups",            "DC UPS on battery",               ""),
    IBOOL("alarm_dc_local",          "Local DC volts too low",          ""),
    IBOOL("alarm_generator_battery", "Gen. start battery low",          ""),
    IBOOL("alarm_plc",               "Failed to synchronize with PLC",  ""),
    IBOOL("alarm_net_fault",         "Network error",                   ""),
    IBOOL("alarm_ntp_timeout",       "NTP sync timeout",                ""),
    IBOOL("alarm_actuator_fault",    "Actioner failure",                ""),
    IBOOL("alarm_sensor_fault",      "Sensor fault",                    ""),
    IBOOL("alarm_too_hot",           "Inside temperature too high",     ""),
    IBOOL("alarm_too_cold",          "Inside temperature too low",      ""),
    IBOOL("alarm_too_humid",         "Relative humidity too high",      ""),
    IBOOL("alarm_tmp_str",           "Temp. stratification too high",   ""),
    IBOOL("alarm_water_leak",        "Leak in irrigation circuit",      ""),
    IBOOL("alarm_water_pressure",    "Water pressure too low",          ""),
    IBOOL("alarm_water_clog",        "Failing to drain a tray",         ""),
    IBOOL("alarm_io_drop",           "Logical I/O drops",               ""),
    IBOOL("alarm_log_flood",         "Logs flooding",                   ""),
    IBOOL("alarm_weather_storm",     "Current storm conditions",        ""),
/** @} */

/** @name Status values - derived state published over MQTT and the Modbus TCP slave
 *  @{ */
    //    name                  description                             notes
    IBOOL("sys_supply_lockout", "supply unhealthy: actuators held off", ""),

    //    name             unit  dgt  description                             notes
    IREAL("sys_supply_hz", "Hz", 2,   "deadbanded incoming supply frequency", ""),
/** @} */

/** @name Actuator outputs - driven to the field, dropped to safe when control is off
 *  @{ */
    //   name                   dev  coil  description                       notes
    COIL("cli_window1_opn",     31,  0,    "window 1 open",                  ""),
    COIL("cli_window1_clo",     31,  1,    "window 1 close",                 ""),
    COIL("irr_input_ro1",       31,  2,    "irrigation input valve 1",       ""),
    COIL("irr_output_ro1",      31,  3,    "irrigation output valve 1",      ""),
    COIL("irr_tray1",           31,  4,    "tray 1 valve",                   ""),
    COIL("irr_tray2",           31,  5,    "tray 2 valve",                   ""),
    COIL("cli_window2_opn",     32,  0,    "window 2 open",                  ""),
    COIL("cli_window2_clo",     32,  1,    "window 2 close",                 ""),
    COIL("irr_input_ro2",       32,  2,    "irrigation input valve 2",       ""),
    COIL("irr_output_ro2",      32,  3,    "irrigation output valve 2",      ""),
    COIL("irr_tray3",           32,  4,    "tray 3 valve",                   ""),
    COIL("irr_tray4",           32,  5,    "tray 4 valve",                   ""),
    COIL("cli_window3_opn",     33,  0,    "window 3 open",                  ""),
    COIL("cli_window3_clo",     33,  1,    "window 3 close",                 ""),
    COIL("cli_destrat1_speed1", 33,  2,    "destratification fan 1 speed 1", ""),
    COIL("cli_destrat1_speed2", 33,  3,    "destratification fan 1 speed 2", ""),
    COIL("cli_destrat1_speed3", 33,  4,    "destratification fan 1 speed 3", ""),
    COIL("cli_screen",          33,  5,    "thermal screen",                 ""),
    COIL("irr_tray5",           33,  6,    "tray 5 valve",                   ""),
    COIL("irr_tray6",           33,  7,    "tray 6 valve",                   ""),
    COIL("cli_window4_opn",     34,  0,    "window 4 open",                  ""),
    COIL("cli_window4_clo",     34,  1,    "window 4 close",                 ""),
    COIL("cli_destrat2_speed1", 34,  2,    "destratification fan 2 speed 1", ""),
    COIL("cli_destrat2_speed2", 34,  3,    "destratification fan 2 speed 2", ""),
    COIL("cli_destrat2_speed3", 34,  4,    "destratification fan 2 speed 3", ""),
    COIL("cli_skirtValve",      34,  5,    "wet skirt valve",                ""),
    COIL("irr_tray7",           34,  6,    "tray 7 valve",                   ""),
    COIL("irr_tray8",           34,  7,    "tray 8 valve",                   ""),
/** @} */
};
const uint16_t g_var_count = (uint16_t)(sizeof(g_vars) / sizeof(g_vars[0]));

#undef MTR
#undef COIL
#undef IBOOL
#undef IREAL
#undef PARAM
#undef PU
#undef PI
#undef PF
#undef PS

/** @brief Symbolic index per variable. The row order above is the enum order;
 *  a mismatch would silently address the wrong signal, so the count is
 *  asserted below. */
enum VarId : uint16_t {
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
    V_GRID_L1_V,
    V_GRID_L2_V,
    V_GRID_L3_V,
    V_GRID_L1_A,
    V_GRID_L2_A,
    V_GRID_L3_A,
    V_GRID_L1_VA,
    V_GRID_L2_VA,
    V_GRID_L3_VA,
    V_GRID_L1_VAR,
    V_GRID_L2_VAR,
    V_GRID_L3_VAR,
    V_GRID_P1_PF,
    V_GRID_P2_PF,
    V_GRID_P3_PF,
    V_GRID_P1_PA,
    V_GRID_P2_PA,
    V_GRID_P3_PA,
    V_GRID_LN_V,
    V_GRID_LL_V,
    V_GRID_AVG_A,
    V_GRID_TOTAL_A,
    V_GRID_TOTAL_W,
    V_GRID_TOTAL_VA,
    V_GRID_TOTAL_VAR,
    V_GRID_TOTAL_PF,
    V_GRID_TOTAL_PA,
    V_GRID_HZ,
    V_GRID_12_V,
    V_GRID_L23_V,
    V_GRID_L31_V,
    V_GRID_LL_AVG_THD,
    V_GRID_LN_AVG_THD,
    V_GRID_N_A,
    V_GRID_L1_W,
    V_GRID_L2_W,
    V_GRID_L3_W,
    V_ATS_V,
    V_ATS_A,
    V_ATS_VAR,
    V_ATS_VA,
    V_ATS_HZ,
    V_ALARM_AC_GRID,
    V_ALARM_DC_PS1,
    V_ALARM_GEN,
    V_ALARM_AC_LOCAL,
    V_ALARM_DC_PS2,
    V_ALARM_DC_UPS,
    V_ALARM_DC_LOCAL,
    V_ALARM_GENERATOR_BATTERY,
    V_ALARM_PLC,
    V_ALARM_NET_FAULT,
    V_ALARM_NTP_TIMEOUT,
    V_ALARM_ACTUATOR_FAULT,
    V_ALARM_SENSOR_FAULT,
    V_ALARM_TOO_HOT,
    V_ALARM_TOO_COLD,
    V_ALARM_TOO_HUMID,
    V_ALARM_TMP_STR,
    V_ALARM_WATER_LEAK,
    V_ALARM_WATER_PRESSURE,
    V_ALARM_WATER_CLOG,
    V_ALARM_IO_DROP,
    V_ALARM_LOG_FLOOD,
    V_ALARM_WEATHER_STORM,
    V_SYS_SUPPLY_LOCKOUT,
    V_SYS_SUPPLY_HZ,
    V_CLI_WINDOW1_OPN,
    V_CLI_WINDOW1_CLO,
    V_IRR_INPUT_RO1,
    V_IRR_OUTPUT_RO1,
    V_IRR_TRAY1,
    V_IRR_TRAY2,
    V_CLI_WINDOW2_OPN,
    V_CLI_WINDOW2_CLO,
    V_IRR_INPUT_RO2,
    V_IRR_OUTPUT_RO2,
    V_IRR_TRAY3,
    V_IRR_TRAY4,
    V_CLI_WINDOW3_OPN,
    V_CLI_WINDOW3_CLO,
    V_CLI_DESTRAT1_SPEED1,
    V_CLI_DESTRAT1_SPEED2,
    V_CLI_DESTRAT1_SPEED3,
    V_CLI_SCREEN,
    V_IRR_TRAY5,
    V_IRR_TRAY6,
    V_CLI_WINDOW4_OPN,
    V_CLI_WINDOW4_CLO,
    V_CLI_DESTRAT2_SPEED1,
    V_CLI_DESTRAT2_SPEED2,
    V_CLI_DESTRAT2_SPEED3,
    V_CLI_SKIRTVALVE,
    V_IRR_TRAY7,
    V_IRR_TRAY8,
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
 *  The trailing pair is the ROLE_PARAM block: a contiguous run, which is what
 *  lets the parameter pages address it with a dense 0-based index and one add.
 *  It now LEADS the table, so the count is simply the index of the first sensor
 *  input - derived, not written down twice.
 *
 *  The cost of leading rather than trailing: adding a parameter shifts every
 *  field-IO index after it. That is safe here only because the enum below is
 *  generated from the same row order and static_asserted against the table, so
 *  the two cannot drift - nothing addresses a row by a number written elsewhere. */
#define SITE_VAR_TABLES { \
    g_vars, g_var_values, V_COUNT, \
    g_str_pool, STR_POOL_BYTES, \
    0, V_GRID_L1_V }

// The enum must stay in step with the table it indexes; a mismatch would
// silently read the wrong signal.
static_assert(sizeof(g_vars) / sizeof(g_vars[0]) == (size_t)V_COUNT,
              "g_vars and VarId disagree: an index would address the wrong variable");

// The pool must cover every text parameter's slot; a short pool would let one
// silently overlap its neighbour.
static_assert(STR_POOL_BYTES >= 8 * STR_CAP, "the string pool is too small");

}  // namespace Site

#endif // SITE_VARS_H
