(* cooling_controller program
   ------------------------------------------------------------------------ *)

PROGRAM cooling_controller
VAR_EXTERNAL CONSTANT
  cli_temp_in         : REAL;
  cli_temp_in_qlt     : QUALITY;
  cli_targetTemp_degC : REAL;
  cli_windowStartDem  : REAL;
  cli_windowFullDem   : REAL;
  cli_windowPark      : REAL;
  cli_coolKp          : REAL;
  cli_coolTi_sec      : REAL;
  cli_coolTd_sec      : REAL;
  sys_now_ms          : UDINT;
END_VAR
VAR_EXTERNAL
  cli_coolDem     : REAL;
  cli_coolDem_qlt : QUALITY;
  cli_windowDem   : REAL;
  cli_windowDem_qlt : QUALITY;
END_VAR
VAR
  loop : PID;
END_VAR
VAR_TEMP
  measured : BOOL;
  ki       : REAL;
END_VAR
  (* The house's own temperature, the hotter of the two levels: the worst case
     for cooling, voted once where the probes are. Without a measured one the
     read substitutes the row's DECLARED value - a cold house, so the ladder
     asks for no cooling - and cli_coolDem carries cli_temp_in's own verdict on
     to the gates and the screen. *)
  measured := GOOD(cli_temp_in_qlt);

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
  loop.SP      := cli_targetTemp_degC;
  loop.PV      := cli_temp_in;
  loop.KP      := cli_coolKp;
  loop.KI      := ki;
  loop.KD      := cli_coolKp * cli_coolTd_sec;
  loop.OUT_MIN := -1.0;
  loop.OUT_MAX := 1.0;
  loop.D_TAU_S := 0.0;
  loop.REVERSE := TRUE;
  loop.RESET   := NOT measured;
  loop.NOW     := sys_now_ms;
  loop();

  cli_coolDem     := loop.Y;
  cli_coolDem_qlt := cli_temp_in_qlt;

  (* Where the ladder wants the windows, published as the one INTENDED opening:
     the windows apply the interlocks to it, and an operator can see what was
     asked for beside where each ram actually is.

     Without a measured house the roof PARKS, and the park position is
     published as an opening like any other: windows_controller takes this
     number whatever the house is doing. *)
  IF measured THEN
    cli_windowDem := LADDER(V := loop.Y, LO := cli_windowStartDem,
                            HI := cli_windowFullDem);
  ELSE
    cli_windowDem := cli_windowPark;
  END_IF;
  (* The PARK opening is a real demand a law chose, so the row is believable
     either way - what is not believable is the house behind it. *)
  cli_windowDem_qlt := cli_temp_in_qlt;
END_PROGRAM
