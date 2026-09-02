(* Seconds as milliseconds, the one place the factor is written
   ------------------------------------------------------------------------ *)

(* A parameter an operator sets is in SECONDS; every timer input is in
   milliseconds. This is that seam, named once, so no law spells the factor
   out and none of them can spell it differently. *)

FUNCTION SEC_MS : UDINT
VAR_INPUT
  S : UINT;
END_VAR
  SEC_MS := UINT_TO_UDINT(S) * 1000;
END_FUNCTION
