(* The site's CONFIGURATION: which rows ST may name, and which programs run
   together.

   VAR_GLOBAL is a THIN restatement of the site table - a name and a type,
   nothing else. Every row's unit, band, digits, register, transport, timeout
   and enum renderer stay in vars.cpp, where matiec never sees them.

   ONE RESOURCE PER RATE GROUP, each declaring the base tick, so every
   resource compiles to `tick % 1` and runs whenever it is called. StRuntime
   calls each at its group's real period through Tasks::Registry, which is
   what keeps sys_dt_ms the group's ACTUAL elapsed time and stops the period
   being declared in two places. *)

CONFIGURATION gh_mlb41

  VAR_GLOBAL
    sys_dt_ms            : UDINT;
    cli_dTStrat_degC     : REAL;
    cli_dTStratLow_degC  : REAL;
    cli_dTStratMed_degC  : REAL;
    cli_dTStratHigh_degC : REAL;
    cli_dTStratHyst_degC : REAL;
    cli_fanLowDem        : REAL;
    cli_fanMedDem        : REAL;
    cli_fanHighDem       : REAL;
    cli_stratDem         : REAL;

    cli_rain_mmph         : REAL;
    cli_rain_mmph_qlt     : BOOL;
    cli_wind_mps          : REAL;
    cli_wind_mps_qlt      : BOOL;
    cli_windGust_mps      : REAL;
    cli_windShutBegin_mps : REAL;
    cli_windShutEnd_mps   : REAL;
    cli_windShutMax       : REAL;
    cli_rainLockout_sec   : UINT;
    sys_supply_lockout    : BOOL;
    cli_rainLockout       : BOOL;
    cli_windLockout       : BOOL;
    cli_windowLockout     : BOOL;
    cli_windowCap         : REAL;
  END_VAR

  RESOURCE slow ON OPTA
    TASK beat (INTERVAL := T#10ms, PRIORITY := 1);
    PROGRAM destrat WITH beat : destrat_controller;
    PROGRAM roof    WITH beat : roof_lockout;
  END_RESOURCE

END_CONFIGURATION
