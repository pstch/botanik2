(* climate_watch program
   ------------------------------------------------------------------------ *)

PROGRAM climate_watch
VAR_EXTERNAL CONSTANT
  cli_temp_hi           : REAL;
  cli_dpMargin_degC     : REAL;
  cli_temp_hi_qlt       : BOOL;
  cli_dpMargin_degC_qlt : BOOL;
  cli_dTStrat_degC      : REAL;
  cli_dTStrat_degC_qlt  : BOOL;
  cli_tooHot_degC       : REAL;
  cli_tooCold_degC      : REAL;
  cli_dehumFull_degC    : REAL;
  cli_dTStratHigh_degC  : REAL;
  alarm_sns_ceiling     : BOOL;
  alarm_sns_floor       : BOOL;
  alarm_sns_crawlspace  : BOOL;
  alarm_sns_exterior    : BOOL;
END_VAR
VAR_EXTERNAL
  alarm_too_hot      : BOOL;
  alarm_too_cold     : BOOL;
  alarm_too_humid    : BOOL;
  alarm_strat        : BOOL;
  alarm_sensor_fault : BOOL;
END_VAR
  (* Rolled up from the four zones: "a sensor is faulty" is a fact about the
     plant that a supervisor wants without knowing there are four of them. *)
  alarm_sensor_fault := alarm_sns_ceiling OR alarm_sns_floor
                     OR alarm_sns_crawlspace OR alarm_sns_exterior;

  (* Each alarm is gated on the quality of the row IT reads, and no other: a
     dead ceiling probe must not silence an alarm the floor can still see. *)
  IF cli_temp_hi_qlt THEN
    alarm_too_hot  := cli_temp_hi > cli_tooHot_degC;
    alarm_too_cold := cli_temp_hi < cli_tooCold_degC;
  ELSE
    (* Nothing is raised on a reading nobody has. The quorum alarm is what says
       the plant cannot be seen, and it is already raised. *)
    alarm_too_hot  := FALSE;
    alarm_too_cold := FALSE;
  END_IF;

  (* A MARGIN, not an RH: air at 90% and 5 degC is drier than air at 60% and
     25, and the alarm has to mean one thing at both. The line is the
     dehumidify arm's own saturation point, so the alarm cannot disagree with
     the actuator it is about. *)
  IF cli_dpMargin_degC_qlt THEN
    alarm_too_humid := cli_dpMargin_degC < cli_dehumFull_degC;
  ELSE
    alarm_too_humid := FALSE;
  END_IF;

  (* Past the band the fans are already flat out on: the gap is not something
     destratification is going to close on its own. Gated like the rest, so a
     gap nobody measured cannot raise it. *)
  IF cli_dTStrat_degC_qlt THEN
    alarm_strat := cli_dTStrat_degC > cli_dTStratHigh_degC;
  ELSE
    alarm_strat := FALSE;
  END_IF;
END_PROGRAM
