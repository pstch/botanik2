(* Time arithmetic, as ST. Mirrors src/libbotanik/plc/functions/time.h.

   A parameter an operator sets is in SECONDS; every timer input is in
   milliseconds. This is that seam, named once, so no law spells the factor
   out and none of them can spell it differently. *)

FUNCTION SEC_MS : UDINT
(* Seconds as milliseconds. UINT because that is what a `_sec` parameter row
   is; the widest it holds is 65535 s, which does not overflow a UDINT. *)
VAR_INPUT
  S : UINT;
END_VAR
  SEC_MS := UINT_TO_UDINT(S) * 1000;
END_FUNCTION
