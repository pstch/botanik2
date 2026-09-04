(* target_temp_controller program
   ------------------------------------------------------------------------ *)

PROGRAM target_temp_controller
VAR_EXTERNAL CONSTANT
  cli_targetMin_degC   : REAL;
  cli_targetMax_degC   : REAL;
  cli_phase            : DayPhase;
  cli_phase_qlt        : QUALITY;
  cli_coldBankArm      : BOOL;
  cli_heatBankArm      : BOOL;
  cli_coolSet_degC     : REAL;
  cli_coldBankOfs_degC : REAL;
  cli_heatBankOfs_degC : REAL;
  cli_targetTau_s      : UINT;
  sys_now_ms           : UDINT;
END_VAR
VAR_EXTERNAL
  cli_targetTemp_degC : REAL;
END_VAR
VAR
  smooth : EWMA;
END_VAR
VAR_TEMP
  held : REAL;
END_VAR
  (* The chain STOPS here. Every cooling stage keys off this row, so a target
     nobody could place must not be published as one nobody can use: the base
     setpoint is a proper value on its own - it is what the plant runs at for
     most of the day - and the phase only ever shifts it. An unplaced phase
     takes the base unshifted. *)
  held := cli_coolSet_degC;

  (* A bank phase shifts the base only when yesterday justified it: pre-cool
     before dawn after a hot day, bank heat at dusk after a cold night. In
     force but not armed, the phase runs at the ordinary target. *)
  IF GOOD(cli_phase_qlt) THEN
    IF cli_phase = PHASE_COLD_BANK AND cli_coldBankArm THEN
      held := held - cli_coldBankOfs_degC;
    ELSIF cli_phase = PHASE_HEAT_BANK AND cli_heatBankArm THEN
      held := held + cli_heatBankOfs_degC;
    END_IF;
  END_IF;

  (* A phase boundary is a STEP, and every consumer of the target answers a gap
     proportionally: unfiltered, dawn moves the windows, the fans and the screen
     in one beat. The filter adopts its first reading whole, so an outage does
     not ease up from whatever it held before. *)
  smooth.X     := held;
  smooth.Q_IN  := Q_GOOD;
  smooth.TAU_S := UINT_TO_REAL(cli_targetTau_s);
  smooth.NOW   := sys_now_ms;
  smooth.LO    := 0.0;
  smooth.HI    := 0.0;
  smooth();

  (* The clamp is LAST, and what keeps an aggressively-set offset from pushing
     the greenhouse somewhere dangerous, whatever the phase asked for: a rail an
     operator has just moved has to bite now, not one time constant later. *)
  cli_targetTemp_degC := LIMIT(cli_targetMin_degC, smooth.Y, cli_targetMax_degC);
END_PROGRAM
