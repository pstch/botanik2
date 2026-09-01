(* The demand ladders, and the votes a redundant chain reduces with
   ------------------------------------------------------------------------ *)

(* Written once so three subsystems cannot disagree about what "half demand"
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
    SIGNED_LADDER := -LADDER(V := -V, LO := -NEG_START, HI := -NEG_FULL);
  ELSE
    SIGNED_LADDER := 0.0;
  END_IF;
END_FUNCTION


FUNCTION MEDIAN3 : REAL
(* The middle of three readings, so one stuck high or stuck low can never be
   the one retained. Both extremes are discarded, not averaged in. *)
VAR_INPUT
  A : REAL;
  B : REAL;
  C : REAL;
END_VAR
  IF   (A <= B AND B <= C) OR (C <= B AND B <= A) THEN MEDIAN3 := B;
  ELSIF (B <= A AND A <= C) OR (C <= A AND A <= B) THEN MEDIAN3 := A;
  ELSE MEDIAN3 := C;
  END_IF;
END_FUNCTION

FUNCTION AVG2 : REAL
(* The mean of two readings: what is left when there is no middle to keep. *)
VAR_INPUT
  A : REAL;
  B : REAL;
END_VAR
  AVG2 := 0.5 * (A + B);
END_FUNCTION
