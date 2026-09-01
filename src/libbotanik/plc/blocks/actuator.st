(* Open/close coil pairs, driven to a fractional target by timing
   ------------------------------------------------------------------------ *)

(* Drives an open/close coil pair to a fractional target by TIMING the strokes.
   No position feedback: the position is an ESTIMATE, a stroke to either end
   runs SEAT_MS extra to re-datum, and it is UNKNOWN until one has.

   Three invariants the body enforces: the coils are never both energised, a
   reversal always passes through REVERSE_MS with both dropped (a contactor
   that makes before the other breaks shorts the motor), and a move under
   MIN_RUN_MS is not made at all.

   Names: the internals are lower case and the outputs upper, but IEC FOLDS
   CASE - so the estimate is `p` and not `pos`, and the state is `mode` and not
   `state`. Either would be the output it feeds. *)

FUNCTION ACT_WANT : USINT
(* The direction TGT asks for: 0 none, 1 open, 2 close. Stateless, so it is a
   FUNCTION - the body needs it twice, once mid-stroke and once when arming. *)
VAR_INPUT
  MODE : USINT;
  P    : REAL;
  TGT  : REAL;
  DB   : REAL;
END_VAR
VAR
  err : REAL;
END_VAR
  IF MODE = 0 THEN
    (* Nothing is known about where it sits, so there is no error to compute:
       drive to whichever end the target is nearer, and datum there. *)
    IF TGT >= 1.0 - DB THEN ACT_WANT := 1; ELSE ACT_WANT := 2; END_IF;
  ELSE
    err := TGT - P;
    IF   err >  DB       THEN ACT_WANT := 1;
    ELSIF err < -DB THEN ACT_WANT := 2;
    ELSE ACT_WANT := 0;
    END_IF;
  END_IF;
END_FUNCTION


FUNCTION_BLOCK ACTUATOR
VAR_INPUT
  TARGET       : REAL;    (* 0 closed .. 1 open; clamped *)
  OPEN_MS      : UDINT;
  CLOSE_MS     : UDINT;
  SEAT_MS      : UDINT;   (* extra run at an end, to re-datum *)
  MIN_RUN_MS   : UDINT;
  REVERSE_MS   : UDINT;
  DB           : REAL;    (* NOT `DEADBAND`: that is a block, and IEC folds case *)
  REDATUM      : REAL;    (* full strokes between datums; 0 disables *)
  HOLD         : BOOL;    (* keep the clock, decide nothing: this ram does
                             not have the bus this scan. Not the same as a
                             target of "stay": an UNKNOWN one would datum. *)
  PRESET_KNOWN : BOOL;    (* the first scan finds it at PRESET_POS *)
  PRESET_POS   : REAL;
  NOW          : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  OPEN_CMD  : BOOL;
  CLOSE_CMD : BOOL;    (* never true at the same time as OPEN_CMD *)
  POS       : REAL;    (* only meaningful once KNOWN *)
  KNOWN     : BOOL;
  MOVING    : BOOL;
  STATE     : USINT;   (* 0 UNKNOWN 1 CLOSING 2 CLOSED 3 OPENING 4 OPEN *)
  DRIFT     : REAL;    (* travel since the last datum, in full strokes *)
END_VAR
VAR
  p       : REAL;
  run_ms  : UDINT;
  wait_ms : UDINT;
  mode    : USINT;
  dir     : USINT;   (* 0 none, 1 open, 2 close *)
  seating : BOOL;    (* this stroke ends at a seat, so snap there *)
  datum   : BOOL;    (* ...and it is the one establishing position *)
  moved   : REAL;
  pending : REAL := -1.0;   (* where to go once the detour finishes, or -1 *)
  last    : UDINT;
  seeded  : BOOL := FALSE;
END_VAR
VAR_TEMP
  elapsed   : UDINT;
  tgt       : REAL;
  ran       : UDINT;
  span      : UDINT;
  delta     : REAL;
  want      : USINT;
  partial   : BOOL;
  is_datum  : BOOL;
  to_end    : BOOL;
  distance  : REAL;
  stroke_ms : UDINT;
  commit    : BOOL;
END_VAR
  IF seeded THEN
    elapsed := NOW - last;
  ELSE
    elapsed := 0;
    seeded  := TRUE;
    (* Declaring the position known, skipping the datum stroke: only where
       something else actually establishes it. The screen's coil is HELD rather
       than driven and deploys de-energised, so a controller that was not
       driving anything left the fabric out. *)
    IF PRESET_KNOWN THEN
      p := LIMIT(0.0, PRESET_POS, 1.0);
      IF p <= 0.0 THEN mode := 2; ELSE mode := 4; END_IF;
    END_IF;
  END_IF;
  last := NOW;

  IF NOT HOLD THEN

    tgt := LIMIT(0.0, TARGET, 1.0);

    (* A re-datum detour owns the target for the WHOLE scan, not just where the
       stroke is chosen. The mid-stroke interrupt below compares the target
       against the direction being driven, and the caller's target is by
       definition the other way - it would abort the detour on the next scan. *)
    IF pending >= 0.0 THEN
      IF mode = 2 THEN
        tgt     := pending;
        pending := -1.0;
      ELSE
        tgt := 0.0;
      END_IF;
    END_IF;

    IF run_ms > 0 THEN
      (* Only the part of this scan the coil was actually energised for counts as
         travel; the rest belongs to the next decision. *)
      IF elapsed < run_ms THEN ran := elapsed; ELSE ran := run_ms; END_IF;
      IF dir = 1 THEN span := OPEN_MS; ELSE span := CLOSE_MS; END_IF;
      (* A zero travel time is a misconfiguration, not a divide: treat the
         actuator as instant rather than producing an infinity. *)
      IF span > 0 THEN delta := UDINT_TO_REAL(ran) / UDINT_TO_REAL(span);
      ELSE delta := 1.0;
      END_IF;
      (* Every stroke adds to the uncertainty, whichever way it went; only
         reaching a stop takes it back. *)
      moved := moved + delta;
      IF dir = 1 THEN p := LIMIT(0.0, p + delta, 1.0);
      ELSE            p := LIMIT(0.0, p - delta, 1.0);
      END_IF;
      run_ms := run_ms - ran;

      IF run_ms = 0 THEN
        (* The stroke ran past the mechanical stop, so the estimate is a fact
           rather than an integration. *)
        IF seating THEN
          IF dir = 1 THEN p := 1.0; ELSE p := 0.0; END_IF;
          moved := 0.0;
        END_IF;
        dir     := 0;
        seating := FALSE;
        datum   := FALSE;
        (* Dead time after EVERY stroke, not only an interrupted one: the move
           that follows one which just seated can only be the opposite
           direction, and it would otherwise start in this very scan. *)
        wait_ms := REVERSE_MS;
        IF p <= 0.0 THEN mode := 2; ELSE mode := 4; END_IF;
      ELSIF NOT datum THEN
        (* The target moved the other way mid-stroke. A stroke is otherwise
           COMMITTED - a seating run must be allowed to overrun the estimate,
           which is the whole point of it - so only the opposite direction
           interrupts, never "we are close enough now". *)
        want := ACT_WANT(MODE := mode, P := p, TGT := tgt, DB := DB);
        IF want <> 0 AND want <> dir THEN
          run_ms  := 0;
          seating := FALSE;
          dir     := 0;
          wait_ms := REVERSE_MS;
          IF p <= 0.0 THEN mode := 2; ELSE mode := 4; END_IF;
        END_IF;
      END_IF;
    ELSIF wait_ms > 0 THEN
      IF wait_ms > elapsed THEN wait_ms := wait_ms - elapsed; ELSE wait_ms := 0; END_IF;
    END_IF;

    IF run_ms = 0 AND wait_ms = 0 THEN
      want := ACT_WANT(MODE := mode, P := p, TGT := tgt, DB := DB);
      (* Nothing is wanted, so nothing happens - INCLUDING a re-datum. Letting
         one fire on a steady target would send the actuator to the seat and back
         while nobody asked it to move at all. *)
      IF want <> 0 THEN
        (* Only for a move that ENDS PARTWAY. One to either end runs onto the
           stop and re-datums by itself, so a detour would be a wasted stroke. *)
        partial := tgt > DB AND tgt < 1.0 - DB;
        IF pending < 0.0 AND REDATUM > 0.0 AND moved >= REDATUM
           AND mode <> 0 AND partial THEN
          pending := tgt;
          tgt     := 0.0;
          (* Recomputed: deciding the direction from the caller's target and then
             redirecting the stroke would drive the wrong way. *)
          want := ACT_WANT(MODE := mode, P := p, TGT := tgt, DB := DB);
          IF want = 0 THEN pending := -1.0; END_IF;
        END_IF;

        IF want <> 0 THEN
          is_datum := mode = 0;
          to_end   := is_datum OR tgt <= DB OR tgt >= 1.0 - DB;
          IF want = 1 THEN span := OPEN_MS; ELSE span := CLOSE_MS; END_IF;
          (* A datum stroke assumes the worst case, a full travel away. *)
          IF is_datum THEN distance := 1.0; ELSE distance := tgt - p; END_IF;
          distance := ABS(distance);
          stroke_ms := REAL_TO_UDINT(distance * UDINT_TO_REAL(span));
          commit  := TRUE;
          IF to_end THEN
            stroke_ms := stroke_ms + SEAT_MS;      (* overrun onto the stop, re-datuming *)
          ELSIF stroke_ms < MIN_RUN_MS THEN
            commit := FALSE;               (* not worth a twitch *)
          END_IF;
          IF commit THEN
            IF stroke_ms = 0 THEN stroke_ms := 1; END_IF;   (* always make progress *)
            dir     := want;
            run_ms  := stroke_ms;
            seating := to_end;
            datum   := is_datum;
            IF want = 1 THEN mode := 3; ELSE mode := 1; END_IF;
          END_IF;
        END_IF;
      END_IF;
    END_IF;
  END_IF;

  OPEN_CMD  := dir = 1 AND run_ms > 0;
  CLOSE_CMD := dir = 2 AND run_ms > 0;
  MOVING    := OPEN_CMD OR CLOSE_CMD;
  POS       := p;
  KNOWN     := mode <> 0;
  STATE     := mode;
  DRIFT     := moved;
END_FUNCTION_BLOCK
