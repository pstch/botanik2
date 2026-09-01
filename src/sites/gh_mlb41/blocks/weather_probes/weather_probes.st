(* The ST source for WeatherProbes, beside the C++ it replaces.

   TWO stations is a weaker vote than the three a zone gets: a disagreement can
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
  IF A_OK THEN Y := A;     USED := 1; END_IF;
  IF B_OK THEN Y := Y + B; USED := USED + 1; END_IF;
  IF USED = 2 THEN Y := Y * 0.5; END_IF;
END_FUNCTION_BLOCK


FUNCTION_BLOCK WX_PUBLISH
(* Filter a vote and clamp it to the row's own declared band, so a filter fed a
   spike cannot publish past what the instrument can report.

   With nothing measured the row keeps the last average the weather could make
   and OK says it is not current. The filter keeps its state and seeding is not
   re-armed, so a station coming back rejoins the average it left. *)
VAR_INPUT
  V     : REAL;
  USED  : USINT;
  TAU_S : REAL;
  NOW   : UDINT;
  LO    : REAL;
  HI    : REAL;
END_VAR
VAR_OUTPUT
  Y  : REAL;
  OK : BOOL;    (* this beat MEASURED it *)
END_VAR
VAR
  filt   : EWMA;
  seeded : BOOL := FALSE;
END_VAR
  (* CALLED every scan, folding only when there is something to fold: the
     filter reads the clock, and one that is skipped for an outage snaps to the
     first reading back instead of rejoining. @see EWMA.HOLD *)
  filt(X := V, TAU_S := TAU_S, HOLD := USED = 0, NOW := NOW);
  IF USED = 0 THEN
    OK := FALSE;
    (* Nothing has ever measured the sky: the row's own declared value is what
       it keeps, and this leaves it alone. *)
    IF seeded THEN Y := LIMIT(LO, filt.Y, HI); END_IF;
  ELSE
    seeded := TRUE;
    Y  := LIMIT(LO, filt.Y, HI);
    OK := TRUE;
  END_IF;
END_FUNCTION_BLOCK
