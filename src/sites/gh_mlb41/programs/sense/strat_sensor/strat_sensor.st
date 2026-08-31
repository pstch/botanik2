(* strat_sensor, as ST. Mirrors
   src/sites/gh_mlb41/programs/sense/strat_sensor/. *)

PROGRAM strat_sensor
VAR_EXTERNAL CONSTANT
  cli_temp_hi      : REAL;
  cli_temp_lo      : REAL;
  cli_temp_hi_qlt  : BOOL;
  cli_temp_lo_qlt  : BOOL;
END_VAR
VAR_EXTERNAL
  cli_dTStrat_degC     : REAL;
  cli_dTStrat_degC_qlt : BOOL;
END_VAR
VAR_EXTERNAL CONSTANT
  cli_dTStratLow_degC : REAL;
END_VAR
  (* ABSOLUTE: destratification mixes the air whichever way the profile has
     gone, and an inverted one - a cold roof at night over a warm floor - is as
     much a case for the fans as the usual way round. The sign would only
     matter to a law that wanted to move heat in one direction, and none does.

     A gap computed from a level nothing is measuring is a number about a
     place, so an unmeasured profile publishes the LOW RUNG instead: that keeps
     the fans mixing without claiming a profile nobody saw. The substitution
     a C++ producer used to get from the row's declared value has to be written
     out here, because an ST-written row is always valid. *)
  IF cli_temp_hi_qlt AND cli_temp_lo_qlt THEN
    cli_dTStrat_degC := ABS(cli_temp_hi - cli_temp_lo);
  ELSE
    cli_dTStrat_degC := cli_dTStratLow_degC;
  END_IF;
  cli_dTStrat_degC_qlt := cli_temp_hi_qlt AND cli_temp_lo_qlt;
END_PROGRAM
