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
    cli_stratDem          : REAL;
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
    sys_now_ms             : UDINT;
    sys_supply_lockout    : BOOL;
  END_VAR

  RESOURCE plc ON OPTA
    TASK slow (INTERVAL := T#100ms, PRIORITY := 1);

    (* Execution order within the beat is the order they appear here. *)
    PROGRAM destrat WITH slow : destrat_controller;
    PROGRAM roof    WITH slow : roof_lockout;
    PROGRAM fans    WITH slow : fan_controller;
  END_RESOURCE

END_CONFIGURATION
