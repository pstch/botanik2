(* intake_dry_gate, as ST. Mirrors
   src/sites/gh_mlb41/programs/climate/intake_dry_gate/. *)

PROGRAM intake_dry_gate
VAR_EXTERNAL CONSTANT
  cli_temp_hi          : REAL;
  cli_rh_hi            : REAL;
  cli_temp_ext         : REAL;
  cli_rh_ext           : REAL;
  cli_temp_hi_qlt      : BOOL;
  cli_rh_hi_qlt        : BOOL;
  cli_temp_ext_qlt     : BOOL;
  cli_rh_ext_qlt       : BOOL;
  cli_ventDryMgn_degC  : REAL;
  cli_ventHyst_degC    : REAL;
END_VAR
VAR_EXTERNAL
  cli_intakeDryOk : BOOL;
END_VAR
VAR
  gate : BAND_LATCH;
END_VAR
VAR_TEMP
  have : BOOL;
  diff : REAL;
END_VAR
  have := cli_temp_hi_qlt AND cli_rh_hi_qlt
      AND cli_temp_ext_qlt AND cli_rh_ext_qlt;

  IF have THEN
    (* DEW POINTS, not humidities: air at 90% and 5 degC is drier than air at
       60% and 25, and swapping one for the other is what ventilating does.
       How much drier the outside is, in degrees of margin. *)
    diff := DEW_POINT(T_DEGC := cli_temp_hi,  RH := cli_rh_hi)
          - DEW_POINT(T_DEGC := cli_temp_ext, RH := cli_rh_ext);
    (* Opens AT its margin and closes a band under it, so two converging dew
       points hold the gate where it is instead of flipping it every beat. *)
    gate.IN   := diff;
    gate.RISE := cli_ventDryMgn_degC;
    gate.FALL := cli_ventDryMgn_degC - cli_ventHyst_degC;
    gate();
    cli_intakeDryOk := gate.Q;
  ELSE
    (* Shut, and said: opening on a guess would mean drawing in air that may be
       wetter than what is already here, which is the one thing this gate
       exists to prevent. *)
    cli_intakeDryOk := FALSE;
  END_IF;
END_PROGRAM
