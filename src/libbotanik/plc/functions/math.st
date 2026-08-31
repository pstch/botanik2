(* The demand ladders, as ST. Mirrors src/libbotanik/plc/functions/math.h.

   Written once so three subsystems cannot disagree about what "half demand"
   means. Stateless, so FUNCTIONs rather than blocks. *)

FUNCTION LADDER : REAL
(* V mapped onto 0..1 across [LO, HI] and clamped. A degenerate or inverted
   span reads as fully on at or above HI, so a mis-set pair fails toward
   ACTING rather than toward doing nothing. *)
VAR_INPUT
  V  : REAL;
  LO : REAL;
  HI : REAL;
END_VAR
  IF HI <= LO THEN
    IF V >= HI THEN LADDER := 1.0; ELSE LADDER := 0.0; END_IF;
  ELSE
    LADDER := LIMIT(0.0, (V - LO) / (HI - LO), 1.0);
  END_IF;
END_FUNCTION

FUNCTION SIGNED_LADDER : REAL
(* The same either side of zero, with a dead band between. Reads 0 inside
   [NEG_START, POS_START], falls to -1 by NEG_FULL and rises to +1 by
   POS_FULL. One signed number, negative meaning the opposite process. *)
VAR_INPUT
  V         : REAL;
  NEG_FULL  : REAL;
  NEG_START : REAL;
  POS_START : REAL;
  POS_FULL  : REAL;
END_VAR
  IF V >= POS_START THEN
    SIGNED_LADDER := LADDER(V := V, LO := POS_START, HI := POS_FULL);
  ELSIF V <= NEG_START THEN
    SIGNED_LADDER := 0.0 - LADDER(V := 0.0 - V, LO := 0.0 - NEG_START,
                                  HI := 0.0 - NEG_FULL);
  ELSE
    SIGNED_LADDER := 0.0;
  END_IF;
END_FUNCTION
