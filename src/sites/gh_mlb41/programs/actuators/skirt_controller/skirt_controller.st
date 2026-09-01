(* skirt_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/actuators/skirt_controller/. *)

PROGRAM skirt_controller
VAR_EXTERNAL CONSTANT
  cli_coolDem           : REAL;
  cli_coolDem_qlt       : BOOL;
  cli_humidDem          : REAL;
  cli_skirtCoolDem      : REAL;
  cli_skirtDryMin_degC  : REAL;
  cli_dpMargin_degC     : REAL;
  cli_dpMargin_degC_qlt : BOOL;
  cli_rotLockout        : BOOL;
  cli_skirtPeriod_sec   : UINT;
  cli_skirtMinOn_sec    : UINT;
  sys_now_ms            : UDINT;
END_VAR
VAR_EXTERNAL
  cli_skirtValve : BOOL;
  cli_skirtDuty  : REAL;
END_VAR
VAR
  pulse : VALVE_PULSE;
END_VAR
VAR_TEMP
  humid : REAL;
  cool  : REAL;
  want  : REAL;
  dry   : BOOL;
END_VAR
  (* PROPORTIONAL on the humidify arm: the skirt IS the humidifier, and the
     ladder's effort is the on-time. The dehumidify arm is the intractors'. *)
  humid := LIMIT(0.0, cli_humidDem, 1.0);

  (* STAGED on the cooling axis, and gated on the air being dry enough for a
     wet pad to cool at all - in damp air it only adds water.

     BOTH companions, because both of these used to arrive as substitutions:
     an unreadable demand declared 0.20, under cli_skirtCoolDem, and an
     unreadable margin declared 2.0, under cli_skirtDryMin_degC. Each left the
     valve shut. Their producers are ST now and always publish, so what said
     "nobody measured this" has to be asked for. *)
  dry := cli_dpMargin_degC_qlt AND cli_dpMargin_degC >= cli_skirtDryMin_degC;
  IF cli_coolDem_qlt AND dry THEN
    cool := LADDER(V := cli_coolDem, LO := cli_skirtCoolDem, HI := 1.0);
  ELSE
    cool := 0.0;
  END_IF;

  want := MAX(humid, cool);

  (* Wetting air that is already condensing is the one thing this valve must
     never do, so the lockout is resolved before any demand reaches the coil -
     and the pulse phase starts again from shut. *)
  IF cli_rotLockout THEN want := 0.0; END_IF;

  pulse.DUTY_IN   := want;
  pulse.PERIOD_MS := SEC_MS(cli_skirtPeriod_sec);
  pulse.MIN_ON_MS := SEC_MS(cli_skirtMinOn_sec);
  pulse.RESET     := cli_rotLockout;
  pulse.NOW       := sys_now_ms;
  pulse();

  IF cli_rotLockout THEN
    cli_skirtValve := FALSE;
    cli_skirtDuty  := 0.0;
  ELSE
    cli_skirtValve := pulse.Q;
    cli_skirtDuty  := pulse.SERVED;
  END_IF;
END_PROGRAM
