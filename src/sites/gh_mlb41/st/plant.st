(* The site's CONFIGURATION: which rows ST may name, and which programs run
   together.

   VAR_GLOBAL is a THIN restatement of the site table - a name and a type,
   nothing else. Every row's unit, band, digits, register, transport, timeout
   and enum renderer stay in vars.cpp, where matiec never sees them.

   ONE RESOURCE, and the TASK intervals are the real ones. matiec computes
   their GCD as `common_ticktime__` and divides by it, so this file is the
   single source of truth for what runs how often. StRuntime registers at
   `common_ticktime__` and calls `config_run__(tick)`; nothing on the C++ side
   picks a rate.

   That works because a block reads the CLOCK rather than a delta: it
   differences sys_now_ms against its own last reading, so it does not matter
   which task it is on or whether our side knows the task ran. *)

CONFIGURATION gh_mlb41

  VAR_GLOBAL
    alarm_sns_ceiling     : BOOL;
    alarm_sns_crawlspace  : BOOL;
    alarm_sns_exterior    : BOOL;
    alarm_sns_floor       : BOOL;
    cli_breezeDem         : REAL;
    cli_coolDem           : REAL;
    cli_dTStratHigh_degC  : REAL;
    cli_dTStratHyst_degC  : REAL;
    cli_dTStratLow_degC   : REAL;
    cli_dTStratMed_degC   : REAL;
    cli_dTStrat_degC      : REAL;
    cli_destrat1_speed1   : BOOL;
    cli_destrat1_speed2   : BOOL;
    cli_destrat1_speed3   : BOOL;
    cli_destrat2_speed1   : BOOL;
    cli_destrat2_speed2   : BOOL;
    cli_destrat2_speed3   : BOOL;
    cli_fanDemHyst        : REAL;
    cli_fanHighDem        : REAL;
    cli_fanIrrDem         : REAL;
    cli_fanLowDem         : REAL;
    cli_fanMedDem         : REAL;
    cli_fanSpeed          : USINT;
    cli_phase             : USINT;
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
    cli_sensorTau_ms      : UINT;
    cli_stratDem          : REAL;
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
    cli_temp_lo           : REAL;
    cli_temp_lo1          : REAL;
    cli_temp_lo1_qlt      : BOOL;
    cli_temp_lo2          : REAL;
    cli_temp_lo2_qlt      : BOOL;
    cli_temp_lo3          : REAL;
    cli_temp_lo3_qlt      : BOOL;
    cli_temp_lo_qlt       : BOOL;
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
    sys_now_ms            : UDINT;
    sys_supply_lockout    : BOOL;
  END_VAR

  RESOURCE plc ON OPTA
    TASK slow (INTERVAL := T#100ms, PRIORITY := 1);

    (* Execution order within the beat is the order they appear here. *)
    (* Conditioning first: everything below reads what it publishes. *)
    PROGRAM sensors WITH slow : temp_rh_sensors;

    PROGRAM destrat WITH slow : destrat_controller;
    PROGRAM roof    WITH slow : roof_lockout;
    PROGRAM fans    WITH slow : fan_controller;
  END_RESOURCE

END_CONFIGURATION
