(* destrat_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/climate/destrat_controller/. *)

FUNCTION_BLOCK STAIRCASE3
VAR_INPUT
  IN   : REAL;
  LO   : REAL;
  MED  : REAL;
  HI   : REAL;
  HYST : REAL;
END_VAR
VAR_OUTPUT
  Q : USINT;
END_VAR
VAR
  band : USINT := 0;
END_VAR
  (* Each rung engages AT its gap and is released a hysteresis below it. *)
  IF   IN >= HI  THEN band := 3;
  ELSIF IN >= MED THEN band := 2;
  ELSIF IN >= LO  THEN band := 1;
  ELSIF IN <  LO - HYST THEN band := 0;
  END_IF;
  Q := band;
END_FUNCTION_BLOCK

PROGRAM destrat
VAR_INPUT
  strat    : REAL;
  low      : REAL;
  med      : REAL;
  high     : REAL;
  hyst     : REAL;
  fan_low  : REAL;
  fan_med  : REAL;
  fan_high : REAL;
END_VAR
VAR_OUTPUT
  demand : REAL;
END_VAR
VAR
  bands : STAIRCASE3;
END_VAR
  bands(IN := strat, LO := low, MED := med, HI := high, HYST := hyst);
  CASE bands.Q OF
    1: demand := fan_low;
    2: demand := fan_med;
    3: demand := fan_high;
  ELSE
    demand := 0.0;
  END_CASE;
END_PROGRAM

CONFIGURATION gh_mlb41
  VAR_GLOBAL
    cli_dTStrat_degC     : REAL;
    cli_dTStratLow_degC  : REAL := 2.0;
    cli_dTStratMed_degC  : REAL := 4.0;
    cli_dTStratHigh_degC : REAL := 6.0;
    cli_dTStratHyst_degC : REAL := 0.5;
    cli_fanLowDem        : REAL := 0.3;
    cli_fanMedDem        : REAL := 0.6;
    cli_fanHighDem       : REAL := 1.0;
    cli_stratDem         : REAL;
  END_VAR

  RESOURCE plc ON OPTA
    TASK slow (INTERVAL := T#100ms, PRIORITY := 1);
    PROGRAM destrat_inst WITH slow : destrat(
        strat    := cli_dTStrat_degC,
        low      := cli_dTStratLow_degC,
        med      := cli_dTStratMed_degC,
        high     := cli_dTStratHigh_degC,
        hyst     := cli_dTStratHyst_degC,
        fan_low  := cli_fanLowDem,
        fan_med  := cli_fanMedDem,
        fan_high := cli_fanHighDem,
        demand   => cli_stratDem);
  END_RESOURCE
END_CONFIGURATION
