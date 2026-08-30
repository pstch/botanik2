/**
 * @file programs.h
 * @brief The site's programs, in EXECUTION order: one X-macro, expanded
 *        three times by site.h so the order is written once.
 */

#ifndef SITE_PROGRAMS_H
#define SITE_PROGRAMS_H

/**
 * @brief How the site's programs reach each other: through the table,
 *        never directly. In EXECUTION order.
 */

// A row one program produces and another reads. Terminal rows - a
// totalizer, a position, an alarm - go to the pages and the bus, not here.
//
// [sense]
//   ac_grid_watch    sys_grid_ok           -> ac_local_watch
//   dc_watch         sys_supply_lockout    -> roof_lockout
//   wx_sensors       cli_wind_mps          -> roof_lockout, weather_watch
//   wx_sensors       cli_windGust_mps      -> roof_lockout
//   wx_sensors       cli_rain_mmph         -> roof_lockout, weather_watch
//   sun_watch        sol_fromRise_min      -> day_phase, irr_schedule
//   sun_watch        sol_fromSet_min       -> day_phase
//   temp_rh_sensors  cli_temp_hi           -> climate_watch, cooling, humidity, strat, temp_rh_zone
//   temp_rh_sensors  cli_rh_hi             -> humidity, temp_rh_zone
//   temp_rh_sensors  cli_temp_lo           -> cooling, strat, temp_rh_zone
//   temp_rh_sensors  cli_rh_lo             -> temp_rh_zone
//   temp_rh_sensors  cli_temp_cs           -> cooling
//   temp_rh_sensors  cli_temp_ext          -> humidity, irr_schedule
//   temp_rh_sensors  cli_rh_ext            -> humidity
//   temp_rh_sensors  alarm_sns_ceiling     -> climate_watch
//   temp_rh_sensors  alarm_sns_floor       -> climate_watch
//   temp_rh_sensors  alarm_sns_crawlspace  -> climate_watch
//   temp_rh_sensors  alarm_sns_exterior    -> climate_watch
//   temp_rh_zone     cli_temp_in           -> cooling, day_phase
//   temp_rh_zone     cli_dpMargin_degC     -> climate_watch, humidity, skirt
//   temp_rh_zone     cli_vpd_kPa           -> irr_modulation
//   temp_rh_zone     alarm_rot             -> humidity
//   strat            cli_dTStrat_degC      -> climate_watch, destrat
//
// [climate]
//   day_phase        cli_phase             -> fans, irr_schedule, screen, target_temp
//   day_phase        cli_coldBankArm       -> target_temp
//   day_phase        cli_heatBankArm       -> target_temp
//   target_temp      cli_targetTemp_degC   -> cooling
//   cooling          cli_coolDem           -> fans, intractors, screen, skirt,
//                                            intake_gates
//   cooling          cli_windowDem         -> windows
//   intake_gates     cli_intakeCoolOk      -> intractors
//   intake_gates     cli_groundHeatOk      -> intractors
//   destrat          cli_stratDem          -> fans
//   humidity         cli_humidDem          -> intractors, skirt
//   humidity         cli_rotLockout        -> skirt
//   humidity         cli_rotVentDem        -> intractors
//   intake_dry       cli_intakeDryOk       -> intractors
//
// [irrigation]
//   irr_modulation   irr_mod               -> irr_schedule, irrigation
//   irr_mtr_input    irr_inTotal_L         -> irrigation
//   irr_mtr_output   irr_outFlow_Lpm       -> irrigation
//   irr_mtr_output   irr_outTotal_L        -> irrigation
//   irr_schedule     irr_dueTray           -> irrigation
//   irrigation       irr_state             -> fans, irr_mtr_input, irr_valves
//   irrigation       irr_activeTray        -> irr_valves
//
// [actuators]
//   roof_lockout     cli_windowCap         -> windows
//
// [sense]
//
// [actuators]

#include <cstdint>

#include "libbotanik.h"

// Every program, in EXECUTION order.
#include "programs/sense/ac_grid_watch/ac_grid_watch.h"
#include "programs/sense/ac_local_watch/ac_local_watch.h"
#include "programs/sense/dc_watch/dc_watch.h"
#include "programs/sense/sun_watch/sun_watch.h"
#include "programs/sense/wx_sensors/wx_sensors.h"
#include "programs/sense/temp_rh_sensors/temp_rh_sensors.h"
#include "programs/sense/temp_rh_zone/temp_rh_zone.h"
#include "programs/sense/strat_sensor/strat_sensor.h"
#include "programs/sense/climate_watch/climate_watch.h"
#include "programs/climate/day_phase_controller/day_phase_controller.h"
#include "programs/climate/target_temp_controller/target_temp_controller.h"
#include "programs/climate/cooling_controller/cooling_controller.h"
#include "programs/climate/intake_gates/intake_gates.h"
#include "programs/climate/humidity_controller/humidity_controller.h"
#include "programs/climate/intake_dry_gate/intake_dry_gate.h"
#include "programs/irrigation/irrigation_modulation/irrigation_modulation.h"
#include "programs/irrigation/irr_mtr_input/irr_mtr_input.h"
#include "programs/irrigation/irr_mtr_output/irr_mtr_output.h"
#include "programs/irrigation/irrigation_schedule/irrigation_schedule.h"
#include "programs/irrigation/irrigation_cycle/irrigation_cycle.h"
#include "programs/irrigation/irr_valves/irr_valves.h"
#include "programs/actuators/screen_controller/screen_controller.h"
#include "programs/actuators/roof_lockout/roof_lockout.h"
#include "programs/actuators/windows_controller/windows_controller.h"
#include "programs/sense/weather_watch/weather_watch.h"
#include "programs/actuators/intractor_controller/intractor_controller.h"
#include "programs/actuators/fan_controller/fan_controller.h"
#include "programs/actuators/skirt_controller/skirt_controller.h"

/** @name The site's programs, in EXECUTION order
 *  site.h expands this three times: the members, the constructor
 *  init, the registration.
 *  @{ */

#define SITE_PROGRAM_LIST(X)                  \
    X(AcGridWatch,          _ac_grid_watch)   \
    X(AcLocalWatch,         _ac_local_watch)  \
    X(DcWatch,              _dc_watch)        \
    X(SunWatch,             _sun_watch)       \
    X(WxSensors,            _wx_sensors)     \
    X(TempRHSensors,        _temp_rh_sensors) \
    X(TempRHZone,           _temp_rh_zone)    \
    X(StratSensor,          _strat)           \
    X(ClimateWatch,         _climate_watch)   \
    X(DayPhaseController,   _day_phase)       \
    X(TargetTempController, _target_temp)     \
    X(CoolingController,    _cooling)         \
    X(IntakeGates,          _intake_gates)   \
    X(HumidityController,   _humidity)        \
    X(IntakeDryGate,        _intake_dry)     \
    X(IrrigationModulation, _irr_modulation)  \
    X(IrrMtrInput,          _irr_mtr_input)   \
    X(IrrMtrOutput,         _irr_mtr_output)  \
    X(IrrigationSchedule,   _irr_schedule)    \
    X(IrrigationCycle,      _irrigation)      \
    X(IrrValves,            _irr_valves)      \
    X(ScreenController,     _screen)          \
    X(RoofLockout,          _roof_lockout)    \
    X(WindowsController,    _windows)         \
    X(WeatherWatch,         _weather_watch)   \
    X(IntractorController,  _intractors)      \
    X(FanController,        _fans)            \
    X(SkirtController,      _skirt)

/** @} */

#endif  // SITE_PROGRAMS_H
