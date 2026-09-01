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
//   (the SUN TRACKER publishes sol_* itself - library rows, no program)
//   sol_fromRise_min                       -> day_phase, irr_schedule
//   sol_fromSet_min                        -> day_phase
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

// EVERY program is ST now: the site declares them in tables/programs.csv and
// matiec generates the roster into generated/plant.cpp. There is no C++
// program list left, and nothing includes a program header.
//
// What remains here is the NETWORK above - which row a law produces and which
// laws read it - because that is the one thing neither the CSV nor the ST
// says: a VAR_EXTERNAL names a row, not the program on the other end of it.

#endif  // SITE_PROGRAMS_H
