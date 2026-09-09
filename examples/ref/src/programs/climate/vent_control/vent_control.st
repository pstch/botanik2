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

  vent_drive := vent_pos;
END_PROGRAM
