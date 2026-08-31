(* The site's CONFIGURATION: which rows ST may name, and which programs run
   together.

   VAR_GLOBAL is a THIN restatement of the site table - a name and a type,
   nothing else. Every row's unit, band, digits, register, transport, timeout
   and enum renderer stay in vars.cpp, where matiec never sees them.

   NO PROGRAM IS BOUND TO A TASK, and that is deliberate. StRuntime calls each
   body itself, at the period the roster in st_plant.cpp states, so that a
   program keeps its own registry entry: its own health row, its own cost and
   missed counts, its own writer slot, and its own INVALID when a name it
   binds resolves to nothing. matiec's own dispatch would run them as one
   fixed sequence with no hook in it.

   The single TASK below is a formality: matiec refuses a RESOURCE with no
   periodic task at all. Nothing binds to it and nothing reads its interval.

   What this file IS for is telling matiec which rows exist and which programs
   to instantiate. *)

CONFIGURATION gh_mlb41

  VAR_GLOBAL
    alarm_sensor_fault    : BOOL;
    alarm_sns_ceiling     : BOOL;
    alarm_sns_crawlspace  : BOOL;
    alarm_sns_exterior    : BOOL;
    alarm_sns_floor       : BOOL;
    alarm_strat           : BOOL;
    alarm_too_cold        : BOOL;
    alarm_too_hot         : BOOL;
    alarm_too_humid       : BOOL;
    alarm_weather_storm   : BOOL;
    cli_breezeDem         : REAL;
    cli_coldBankArm       : BOOL;
    cli_coldBankOfs_degC  : REAL;
    cli_coldBankTrg_degC  : REAL;
    cli_coolDem           : REAL;
    cli_coolSet_degC      : REAL;
    cli_dTStratHigh_degC  : REAL;
    cli_dTStratHyst_degC  : REAL;
    cli_dTStratLow_degC   : REAL;
    cli_dTStratMed_degC   : REAL;
    cli_dTStrat_degC      : REAL;
    cli_dTStrat_degC_qlt  : BOOL;
    cli_dehumFull_degC    : REAL;
    cli_destrat1_speed1   : BOOL;
    cli_destrat1_speed2   : BOOL;
    cli_destrat1_speed3   : BOOL;
    cli_destrat2_speed1   : BOOL;
    cli_destrat2_speed2   : BOOL;
    cli_destrat2_speed3   : BOOL;
    cli_dpMargin_degC     : REAL;
    cli_dpMargin_degC_qlt : BOOL;
    cli_fanDemHyst        : REAL;
    cli_fanHighDem        : REAL;
    cli_fanIrrDem         : REAL;
    cli_fanLowDem         : REAL;
    cli_fanMedDem         : REAL;
    cli_fanSpeed          : USINT;
    cli_groundHeatOk      : BOOL;
    cli_groundSpeed       : REAL;
    cli_heatBankArm       : BOOL;
    cli_heatBankOfs_degC  : REAL;
    cli_heatBankTrg_degC  : REAL;
    cli_humidDem          : REAL;
    cli_intakeCoolOk      : BOOL;
    cli_intakeDryOk       : BOOL;
    cli_intakeFullDem     : REAL;
    cli_intakeStartDem    : REAL;
    cli_intr1             : REAL;
    cli_intr2             : REAL;
    cli_intrSafeSpeed     : REAL;
    cli_phase             : USINT;
    cli_phase_qlt         : BOOL;
    cli_prevDayMax_degC   : REAL;
    cli_prevNightMin_degC : REAL;
    cli_rainLockout       : BOOL;
    cli_rainLockout_sec   : UINT;
    cli_rain_mmph         : REAL;
    cli_rain_mmph_qlt     : BOOL;
    cli_rh_cs             : REAL;
    cli_rh_cs1            : REAL;
    cli_rh_cs1_qlt        : BOOL;
    cli_rh_cs2            : REAL;
    cli_rh_cs2_qlt        : BOOL;
    cli_rh_cs3            : REAL;
    cli_rh_cs3_qlt        : BOOL;
    cli_rh_cs_qlt         : BOOL;
    cli_rh_ext            : REAL;
    cli_rh_ext1           : REAL;
    cli_rh_ext1_qlt       : BOOL;
    cli_rh_ext2           : REAL;
    cli_rh_ext2_qlt       : BOOL;
    cli_rh_ext3           : REAL;
    cli_rh_ext3_qlt       : BOOL;
    cli_rh_ext_qlt        : BOOL;
    cli_rh_hi             : REAL;
    cli_rh_hi1            : REAL;
    cli_rh_hi1_qlt        : BOOL;
    cli_rh_hi2            : REAL;
    cli_rh_hi2_qlt        : BOOL;
    cli_rh_hi3            : REAL;
    cli_rh_hi3_qlt        : BOOL;
    cli_rh_hi_qlt         : BOOL;
    cli_rh_lo             : REAL;
    cli_rh_lo1            : REAL;
    cli_rh_lo1_qlt        : BOOL;
    cli_rh_lo2            : REAL;
    cli_rh_lo2_qlt        : BOOL;
    cli_rh_lo3            : REAL;
    cli_rh_lo3_qlt        : BOOL;
    cli_rh_lo_qlt         : BOOL;
    cli_rotVentDem        : REAL;
    cli_sensorTau_ms      : UINT;
    cli_sinceRain_min     : UINT;
    cli_stratDem          : REAL;
    cli_targetMax_degC    : REAL;
    cli_targetMin_degC    : REAL;
    cli_targetTau_s       : UINT;
    cli_targetTemp_degC   : REAL;
    cli_temp_cs           : REAL;
    cli_temp_cs1          : REAL;
    cli_temp_cs1_qlt      : BOOL;
    cli_temp_cs2          : REAL;
    cli_temp_cs2_qlt      : BOOL;
    cli_temp_cs3          : REAL;
    cli_temp_cs3_qlt      : BOOL;
    cli_temp_cs_qlt       : BOOL;
    cli_temp_ext          : REAL;
    cli_temp_ext1         : REAL;
    cli_temp_ext1_qlt     : BOOL;
    cli_temp_ext2         : REAL;
    cli_temp_ext2_qlt     : BOOL;
    cli_temp_ext3         : REAL;
    cli_temp_ext3_qlt     : BOOL;
    cli_temp_ext_qlt      : BOOL;
    cli_temp_hi           : REAL;
    cli_temp_hi1          : REAL;
    cli_temp_hi1_qlt      : BOOL;
    cli_temp_hi2          : REAL;
    cli_temp_hi2_qlt      : BOOL;
    cli_temp_hi3          : REAL;
    cli_temp_hi3_qlt      : BOOL;
    cli_temp_hi_qlt       : BOOL;
    cli_temp_in           : REAL;
    cli_temp_lo           : REAL;
    cli_temp_lo1          : REAL;
    cli_temp_lo1_qlt      : BOOL;
    cli_temp_lo2          : REAL;
    cli_temp_lo2_qlt      : BOOL;
    cli_temp_lo3          : REAL;
    cli_temp_lo3_qlt      : BOOL;
    cli_temp_lo_qlt       : BOOL;
    cli_tooCold_degC      : REAL;
    cli_tooHot_degC       : REAL;
    cli_windGust_mps      : REAL;
    cli_windLockout       : BOOL;
    cli_windShutBegin_mps : REAL;
    cli_windShutEnd_mps   : REAL;
    cli_windShutMax       : REAL;
    cli_wind_mps          : REAL;
    cli_wind_mps_qlt      : BOOL;
    cli_windowCap         : REAL;
    cli_windowLockout     : BOOL;
    irr_state             : USINT;
    sol_breezeRise_min    : INT;
    sol_coldBankRise_min  : INT;
    sol_dayRise_min       : INT;
    sol_duskSet_min       : INT;
    sol_fromRise_min      : INT;
    sol_fromSet_min       : INT;
    sol_heatBankSet_min   : INT;
    sol_nightSet_min      : INT;
    sol_ready             : BOOL;
    sys_now_ms            : UDINT;
    sys_supply_lockout    : BOOL;
  END_VAR

  RESOURCE plc ON OPTA
    (* Required by matiec, used by nothing: no program is bound to it. *)
    TASK unbound (INTERVAL := T#100ms, PRIORITY := 1);

    (* The instance names are what st_plant.cpp's roster refers to. Execution
       ORDER is the roster's, not this list's. *)
    PROGRAM sensors    : temp_rh_sensors;
    PROGRAM strat      : strat_sensor;
    PROGRAM climwatch  : climate_watch;
    PROGRAM wxwatch    : weather_watch;
    PROGRAM dayphase   : day_phase_controller;
    PROGRAM target     : target_temp_controller;
    PROGRAM destrat    : destrat_controller;
    PROGRAM roof       : roof_lockout;
    PROGRAM intractors : intractor_controller;
    PROGRAM fans       : fan_controller;
  END_RESOURCE

END_CONFIGURATION
