(* The two-station weather vote: the worse reading, or the mean
   ------------------------------------------------------------------------ *)

(* TWO stations is a weaker vote than the three a zone gets: a disagreement can
   be seen but not resolved, because there is no third reading to say which
   station is lying. So there is no outlier rule here - the interlock
   quantities take the WORSE of the two, which is the only choice that cannot
   be argued into opening a roof on a station that has stuck low. *)

FUNCTION_BLOCK WX_WORSE
(* The WORSE reading - the higher - over the stations that are believable.
   An unbelievable station is EXCLUDED rather than voted on: it reads its
   declared value, a declared calm is the lower of the two and would win every
   time, and that is the one way a dead station could open a roof. *)
VAR_INPUT
  A : REAL;  A_OK : BOOL;
  B : REAL;  B_OK : BOOL;
END_VAR
VAR_OUTPUT
  Y    : REAL;
  USED : USINT;
END_VAR
  USED := 0;
  Y    := 0.0;
  IF A_OK THEN Y := A; USED := 1; END_IF;
  IF B_OK THEN
    IF USED = 0 OR B > Y THEN Y := B; END_IF;
    USED := USED + 1;
  END_IF;
END_FUNCTION_BLOCK


FUNCTION_BLOCK WX_MEAN
(* Their mean, for a quantity nothing controls on: the worst case has no
   meaning there and the average is the better estimate. *)
VAR_INPUT
  A : REAL;  A_OK : BOOL;
  B : REAL;  B_OK : BOOL;
END_VAR
VAR_OUTPUT
  Y    : REAL;
  USED : USINT;
END_VAR
  USED := 0;
  Y    := 0.0;
  IF A_OK AND B_OK THEN Y := AVG2(A := A, B := B); USED := 2;
  ELSIF A_OK       THEN Y := A;                    USED := 1;
  ELSIF B_OK       THEN Y := B;                    USED := 1;
  END_IF;
END_FUNCTION_BLOCK
