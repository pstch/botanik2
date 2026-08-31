(* cooling_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/climate/cooling_controller/. *)

PROGRAM cooling_controller
VAR_EXTERNAL CONSTANT
  cli_temp_in         : REAL;
  cli_temp_in_qlt     : BOOL;
  cli_targetTemp_degC : REAL;
  cli_windowStartDem  : REAL;
  cli_windowFullDem   : REAL;
  cli_coolKp          : REAL;
  cli_coolTi_sec      : REAL;
  cli_coolTd_sec      : REAL;
  sys_now_ms          : UDINT;
END_VAR
VAR_EXTERNAL
  cli_coolDem     : REAL;
  cli_coolDem_qlt : BOOL;
  cli_windowDem   : REAL;
END_VAR
VAR
  loop : PID;
END_VAR
VAR_TEMP
  measured : BOOL;
  ki       : REAL;
END_VAR
  (* The house's own row, which is the hotter of the two levels: the spec's
     worst case for cooling, derived once where the probes are voted rather
     than again here. Without a measured one the ladder runs on the reading the
     house last had - temp_rh_zone writes the row every scan, so it holds a
     number - and cli_coolDem_qlt is what says the number is not about this
     greenhouse. The intake gates read that to know this law stopped having an
     opinion. *)
  measured := cli_temp_in_qlt;

  (* The parameters are in the TIME form an operator tunes in; PID takes gains.
     Ti of 0 means no integral action rather than an infinite one, which is
     what a divide would give. *)
  IF cli_coolTi_sec > 0.0 THEN
    ki := cli_coolKp / cli_coolTi_sec;
  ELSE
    ki := 0.0;
  END_IF;

  (* Reverse-acting: the demand RISES as the reading does, so above the target
     it is positive (cooling) and below it negative - the intractors drawing
     warm floor air, and lower still the thermal screen. *)
  loop(SP      := cli_targetTemp_degC,
       PV      := cli_temp_in,
       KP      := cli_coolKp,
       KI      := ki,
       KD      := cli_coolKp * cli_coolTd_sec,
       OUT_MIN := -1.0,
       OUT_MAX := 1.0,
       D_TAU_S := 0.0,
       REVERSE := TRUE,
       RESET   := NOT measured,
       NOW     := sys_now_ms);

  cli_coolDem     := loop.Y;
  cli_coolDem_qlt := measured;

  (* Where the ladder wants the windows, published as the one INTENDED opening:
     the windows apply the interlocks to it, and an operator can see what was
     asked for beside where each ram actually is.

     Without a measured house it is the row's DECLARED opening, written out.
     windows_controller has no companion to read here - it takes the number and
     runs its normal path on it - so it used to get that position from the
     bank SUBSTITUTING for an invalid row. An ST-written row is always valid,
     so the substitution is gone and the safe value has to be published. It is
     a real position and the one the roof parks at. *)
  IF measured THEN
    cli_windowDem := LADDER(V := loop.Y, LO := cli_windowStartDem,
                            HI := cli_windowFullDem);
  ELSE
    cli_windowDem := 0.25;
  END_IF;
END_PROGRAM
