(* The ST source for Staircase3, beside the C++ it replaces.
 *
 * Ours, not the standard's: matiec's lib/*.txt are GPL and its C runtime is
 * LGPL, so nothing here derives from either. The interface follows IEC's
 * shape where there is one, but the block takes DT_MS - the scan's elapsed
 * milliseconds - rather than reading a clock, because the scan already
 * threads dt everywhere and a block that reads a clock cannot be tested at
 * a hundred times real speed.
 *
 * The variable DESCRIPTORS stay in C++. ST names a variable; what a name
 * means - its unit, band, register, transport and timeout - is the table's,
 * and matiec never sees it. *)

(* BAND_LATCH: latches ON at its rising edge and OFF below the falling one,
   holding between the two. Mirrors Plc::Hysteresis.

   NOT named HYSTERESIS, and not for taste: matiec ships a standard FB of that
   name, and redefining it is an internal compiler error rather than a
   diagnostic - "Internal compiler error in file ../util/symtable.cc". *)
FUNCTION_BLOCK BAND_LATCH
VAR_INPUT
  IN  : REAL;
  RISE : REAL;   (* engages at or above *)
  FALL : REAL;   (* releases AT or below, as Plc::Hysteresis does *)
END_VAR
VAR_OUTPUT
  Q : BOOL;
END_VAR
  IF   IN >= RISE THEN Q := TRUE;
  ELSIF IN <= FALL THEN Q := FALSE;
  END_IF;
END_FUNCTION_BLOCK

(* Three rungs, each holding its OWN release band. Evaluated all three rather
   than only the one above the reading: a rung that stopped being asked keeps
   whichever side of itself the reading was last on. Mirrors Plc::Staircase3. *)
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
  held   : REAL;
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
  V_OK   : BOOL;
  LO     : REAL;    (* the floor the rail is judged against *)
  AVG_MS : UINT;
  BAND   : REAL;
  NOW    : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Y       : REAL;
  OK      : BOOL;   (* this beat MEASURED it *)
  HEALTHY : BOOL;
END_VAR
VAR
  avg  : EWMA;
  db   : DEADBAND;
  seen : BOOL := FALSE;
END_VAR
  OK := V_OK;
  (* Only a MEASURED sample is folded in. The row reads its declared value
     while the bus is still coming up, and folding THAT seeds the average at
     zero: the rail then reads under its floor for as long as the filter takes
     to climb out, which is a supply alarm on every boot.
     CALLED either way, though: the filter reads the clock. @see EWMA.HOLD *)
  avg(X := V, TAU_S := UINT_TO_REAL(AVG_MS) / 1000.0, HOLD := NOT V_OK,
      NOW := NOW);
  IF V_OK THEN
    db(V := avg.Y, BAND := BAND);
    seen := TRUE;
  END_IF;
  (* A rail nobody is measuring has no voltage to publish. Its LAST average
     stays in the filter, so a divider that comes back rejoins where it left. *)
  Y := db.Y;

  (* A divider that has NEVER answered is the bus still coming up. One that has
     answered and then stopped is a rail nobody can see, which is a fault. *)
  IF V_OK THEN HEALTHY := avg.Y >= LO;
  ELSE         HEALTHY := NOT seen;
  END_IF;
END_FUNCTION_BLOCK
