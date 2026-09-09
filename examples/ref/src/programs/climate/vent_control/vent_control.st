(* vent_control program
   ------------------------------------------------------------------------ *)

PROGRAM vent_control
VAR_EXTERNAL CONSTANT
  air_temp_hi1       : REAL;
  air_temp_hi2       : REAL;
  air_targetMin_degC : REAL;
  air_targetMax_degC : REAL;
  air_ventFullDem    : REAL;
END_VAR
VAR_EXTERNAL
  air_temp_in : REAL;
  vent_tgt    : REAL;
  vent_pos    : REAL;
  vent_drive  : REAL;
  vent_drive2 : REAL;
  vent_lock   : BOOL;
  air_tooHot  : BOOL;
  prog_vent_health : HEALTH;
  air_temp_in_qlt  : QUALITY;
  vent_tgt_qlt     : QUALITY;
  vent_pos_qlt     : QUALITY;
  vent_drive_qlt   : QUALITY;
  vent_drive2_qlt  : QUALITY;
  vent_lock_qlt    : QUALITY;
END_VAR
VAR_EXTERNAL CONSTANT
  air_temp_hi1_qlt : QUALITY;
  air_temp_hi2_qlt : QUALITY;
END_VAR
  (* The WARMER of the two levels. A vent that cools to the average leaves the
     top of the house over target for as long as the stratification lasts. *)
  IF air_temp_hi1 > air_temp_hi2 THEN
    air_temp_in := air_temp_hi1;
  ELSE
    air_temp_in := air_temp_hi2;
  END_IF;

  (* Working to the ceiling: the floor is what the heat would hold to, and
     there is none here. *)
  vent_tgt := air_targetMax_degC;
  IF vent_tgt < air_targetMin_degC THEN
    vent_tgt := air_targetMin_degC;
  END_IF;

  (* Proportional over the band, saturating at both ends. air_ventFullDem is
     how far over target counts as wide open. *)
  IF air_temp_in <= vent_tgt THEN
    vent_pos := 0.0;
  ELSIF air_temp_in >= vent_tgt + air_ventFullDem THEN
    vent_pos := 1.0;
  ELSE
    vent_pos := (air_temp_in - vent_tgt) / air_ventFullDem;
  END_IF;

  (* Both actuators take the same command: they are one vent with two rams. *)
  vent_drive  := vent_pos;
  vent_drive2 := vent_pos;

  (* The interlock releases only while the vent is actually being
     driven: a stationary ram should not sit unlocked. *)
  vent_lock := vent_pos > 0.0;

  (* Wide open and still over target: the vent has nothing left. The
     row's own dwell decides whether that annunciates. *)
  air_tooHot := (vent_pos >= 1.0) AND (air_temp_in > vent_tgt);

  (* Opting into quality means owing one per row this law writes, on every
     path. Everything downstream is only as good as the level it was picked
     from, so they all carry the same verdict. *)
  IF air_temp_hi1_qlt = Q_GOOD OR air_temp_hi2_qlt = Q_GOOD THEN
    air_temp_in_qlt := Q_GOOD;
  ELSE
    air_temp_in_qlt := Q_NONE;
  END_IF;
  vent_tgt_qlt    := air_temp_in_qlt;
  vent_pos_qlt    := air_temp_in_qlt;
  vent_drive_qlt  := air_temp_in_qlt;
  vent_drive2_qlt := air_temp_in_qlt;
  vent_lock_qlt   := air_temp_in_qlt;

  (* Written on EVERY path, which is what opting into a health row costs. The
     two levels should sit a stratification apart; further than that and one of
     them is telling a story, so the answer is still given but not trusted. *)
  IF ABS(air_temp_hi1 - air_temp_hi2) > 10.0 THEN
    prog_vent_health := DEGRADED;
  ELSE
    prog_vent_health := HEALTHY;
  END_IF;
END_PROGRAM
