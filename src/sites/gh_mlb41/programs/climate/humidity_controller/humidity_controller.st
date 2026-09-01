(* humidity_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/climate/humidity_controller/. *)

PROGRAM humidity_controller
VAR_EXTERNAL CONSTANT
  cli_rotRelease_sec    : UINT;
  cli_dpMargin_degC     : REAL;
  cli_dpMargin_degC_qlt : BOOL;
  cli_dehumFull_degC    : REAL;
  cli_dehumStart_degC   : REAL;
  cli_humidStart_degC   : REAL;
  cli_humidFull_degC    : REAL;
  cli_rotMargin_degC    : REAL;
  alarm_rot             : BOOL;
  sys_now_ms            : UDINT;
END_VAR
VAR_EXTERNAL
  cli_humidDem   : REAL;
  cli_rotLockout : BOOL;
  cli_rotVentDem : REAL;
END_VAR
VAR
  lockout_for : TOF_MS;
END_VAR

  (* Four bands and a dead zone, and the signed ladder IS that shape: below
     dehumStart the margin is closing on condensation (negative, dehumidify);
     above humidStart the air is drier than the plants want (positive); in
     between nothing moves. *)
  cli_humidDem := SIGNED_LADDER(V         := cli_dpMargin_degC,
                                NEG_FULL  := cli_dehumFull_degC,
                                NEG_START := cli_dehumStart_degC,
                                POS_START := cli_humidStart_degC,
                                POS_FULL  := cli_humidFull_degC);

  IF cli_dpMargin_degC_qlt THEN
    (* A FALLING ramp, negated so the one ladder serves both directions:
       nothing at the limit, everything at the dew point itself. The span IS
       the operator's setting - cli_rotMargin_degC sits on its own floor by
       default, so a ramp to that floor would be a step. *)
    cli_rotVentDem := LADDER(V  := -cli_dpMargin_degC,
                             LO := -cli_rotMargin_degC,
                             HI := 0.0);
  ELSE
    (* No margin, no verdict on it - and the guard fails toward ACTING:
       ventilating costs energy, not ventilating costs the collection. *)
    cli_rotVentDem := 1.0;
  END_IF;

  (* An OVERRUN on the ALARM, not on being blind: humidifying into air that has
     only just dried out is what puts the margin back where it was. A margin
     nobody has YET is bring-up, and feeding that into the overrun cost five
     minutes of locked-out skirt after every restart. Blind is a live interlock
     instead, letting go the moment the margin is believable. *)
  lockout_for(IN := alarm_rot, PT := SEC_MS(cli_rotRelease_sec),
              NOW := sys_now_ms);
  cli_rotLockout := lockout_for.Q OR NOT cli_dpMargin_degC_qlt;
END_PROGRAM
