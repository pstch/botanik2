(* target_temp_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/climate/target_temp_controller/. *)

PROGRAM target_temp_controller
VAR_EXTERNAL CONSTANT
  cli_targetMin_degC   : REAL;
  cli_targetMax_degC   : REAL;
  cli_phase            : USINT;
  cli_phase_qlt        : BOOL;
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
     force but not armed, the phase runs at the ordinary target.
     1 is PHASE_COLD_BANK, 4 is PHASE_HEAT_BANK. *)
  IF cli_phase_qlt THEN
    IF cli_phase = 1 AND cli_coldBankArm THEN
      held := held - cli_coldBankOfs_degC;
    ELSIF cli_phase = 4 AND cli_heatBankArm THEN
      held := held + cli_heatBankOfs_degC;
    END_IF;
  END_IF;

  (* A phase boundary is a STEP, and every consumer of the target answers a gap
     proportionally: unfiltered, dawn moves the windows, the fans and the screen
     in one beat. The filter adopts its first reading whole, so an outage does
     not ease up from whatever it held before. *)
  smooth(X := held, OK_IN := TRUE, TAU_S := UINT_TO_REAL(cli_targetTau_s),
         NOW := sys_now_ms, LO := 0.0, HI := 0.0);

  (* The clamp is LAST, and what keeps an aggressively-set offset from pushing
     the greenhouse somewhere dangerous, whatever the phase asked for: a rail an
     operator has just moved has to bite now, not one time constant later. *)
  cli_targetTemp_degC := LIMIT(cli_targetMin_degC, smooth.Y, cli_targetMax_degC);
END_PROGRAM
