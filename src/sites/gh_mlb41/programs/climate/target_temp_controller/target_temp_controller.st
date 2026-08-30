(* target_temp_controller as ST, against st/lib/botanik.st.
   The phase ladder, the bank offsets, the ease and the clamp. *)

PROGRAM target_temp
VAR_INPUT
  phase         : USINT;
  cool_set      : REAL;
  cold_arm      : BOOL;
  heat_arm      : BOOL;
  cold_bank_ofs : REAL;
  heat_bank_ofs : REAL;
  tau_s         : UINT;
  t_min         : REAL;
  t_max         : REAL;
  first_scan    : BOOL;   (* the scan's own flag, handed in *)
  dt_ms         : UDINT;
END_VAR
VAR_OUTPUT
  target : REAL;
END_VAR
VAR
  smooth : EWMA;
END_VAR
VAR_TEMP
  held : REAL;
END_VAR
  (* PHASE_COLD_BANK = 4, PHASE_HEAT_BANK = 5 in the site's DayPhase. *)
  held := cool_set;
  IF   phase = 4 AND cold_arm THEN held := held - cold_bank_ofs;
  ELSIF phase = 5 AND heat_arm THEN held := held + heat_bank_ofs;
  END_IF;

  (* A phase boundary is a STEP and every consumer answers a gap
     proportionally, so it is eased. Adopted whole on the first scan: easing up
     from whatever the filter held before an outage would call for heat the
     house does not need. *)
  IF first_scan THEN
    smooth(X := held, TAU_S := 0.0, DT_MS := dt_ms);
  ELSE
    smooth(X := held, TAU_S := UINT_TO_REAL(tau_s), DT_MS := dt_ms);
  END_IF;

  (* The clamp is LAST: a rail an operator has just moved has to bite now, not
     one time constant later. *)
  target := LIMIT(t_min, smooth.Y, t_max);
END_PROGRAM
