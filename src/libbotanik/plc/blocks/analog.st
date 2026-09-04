(* Analog conditioning: a latch, a three-step ladder, a deadband, a DC rail
   ------------------------------------------------------------------------ *)

(* Latches ON at its rising edge and OFF below the falling one, holding
   between the two: a reading sitting on a threshold decides once. *)
FUNCTION_BLOCK BAND_LATCH
VAR_INPUT
  IN   : REAL;
  RISE : REAL;   (* engages at or above *)
  FALL : REAL;   (* releases at or below *)
END_VAR
VAR_OUTPUT
  Q : BOOL;
END_VAR
  IF   IN >= RISE THEN Q := TRUE;
  ELSIF IN <= FALL THEN Q := FALSE;
  END_IF;
END_FUNCTION_BLOCK

(* Three rungs, each holding its OWN release band. All three are evaluated: a
   rung that stopped being asked keeps whichever side of itself the reading
   was last on. *)
FUNCTION_BLOCK STAIRCASE3
VAR_INPUT
  IN   : REAL;
  LO   : REAL;
  MED  : REAL;
  HI   : REAL;
  HYST : REAL;
END_VAR
VAR_OUTPUT
  Q : USINT;
END_VAR
VAR
  rung1 : BAND_LATCH;
  rung2 : BAND_LATCH;
  rung3 : BAND_LATCH;
END_VAR
VAR_TEMP
  h : REAL;
END_VAR
  (* A negative hysteresis is no hysteresis, never an inverted band. *)
  IF HYST < 0.0 THEN h := 0.0; ELSE h := HYST; END_IF;

  rung1(IN := IN, RISE := LO,  FALL := LO  - h);
  rung2(IN := IN, RISE := MED, FALL := MED - h);
  rung3(IN := IN, RISE := HI,  FALL := HI  - h);

  Q := 0;
  IF rung1.Q THEN Q := 1; END_IF;
  IF rung2.Q THEN Q := 2; END_IF;
  IF rung3.Q THEN Q := 3; END_IF;
END_FUNCTION_BLOCK

FUNCTION_BLOCK DEADBAND
(* Report a value only once it has MOVED more than BAND, so a reading that
   twitches in its last digit does not cost a status write per scan. Cold, the
   first reading is reported whatever it is rather than suppressed against a
   held value from before the gap. *)
VAR_INPUT
  V    : REAL;
  BAND : REAL;
END_VAR
VAR_OUTPUT
  Y : REAL;
END_VAR
VAR
  held : REAL;
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  diff : REAL;
END_VAR
  diff := V - held;
  IF NOT seeded OR ABS(diff) > BAND THEN
    held   := V;
    seeded := TRUE;
  END_IF;
  Y := held;
END_FUNCTION_BLOCK

FUNCTION_BLOCK DC_RAIL
(* One DC rail off a divider: averaged, then deadbanded. The first says what
   the rail is doing, the second says when that is worth a status write. *)
VAR_INPUT
  V      : REAL;
  V_Q    : QUALITY;
  LO     : REAL;    (* the floor the rail is judged against *)
  AVG_MS : UINT;
  BAND   : REAL;
  NOW    : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Y       : REAL;
  Q       : QUALITY;
  HEALTHY : BOOL;
END_VAR
VAR
  avg : EWMA;
  db  : DEADBAND;
  seen : BOOL := FALSE;
END_VAR
  (* Only a MEASURED sample is folded in: the row reads its declared value
     while the bus is coming up, and folding that seeds the average at zero,
     so the rail reads under its floor until the filter climbs out - a supply
     alarm on every boot. CALLED either way, since the filter reads the clock.
     The band is left open: LO is what HEALTHY is judged against, not a clamp. *)
  avg.X     := V;
  avg.Q_IN  := V_Q;
  avg.TAU_S := UINT_TO_REAL(AVG_MS) / 1000.0;
  avg.NOW   := NOW;
  avg.LO    := 0.0;
  avg.HI    := 0.0;
  avg();
  IF GOOD(V_Q) THEN
    db(V := avg.Y, BAND := BAND);
    seen := TRUE;
  END_IF;
  (* A rail nobody is measuring has no voltage to publish. Its LAST average
     stays in the filter, so a divider that comes back rejoins where it left. *)
  Y := db.Y;
  Q := avg.Q;

  (* A divider that has NEVER answered is the bus still coming up. One that has
     answered and then stopped is a rail nobody can see, which is a fault. *)
  IF GOOD(V_Q) THEN HEALTHY := avg.Y >= LO;
  ELSE              HEALTHY := NOT seen;
  END_IF;
END_FUNCTION_BLOCK
