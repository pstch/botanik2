(* The one filter every measured row is published through
   ------------------------------------------------------------------------ *)

FUNCTION_BLOCK EWMA
(* An exponentially-weighted moving average of X, clamped to the row's band,
   and the verdict on whether this beat had anything to average. *)
VAR_INPUT
  X     : REAL;
  Q_IN  : QUALITY;   (* what fed X this beat is worth *)
  TAU_S : REAL;
  NOW   : UDINT;     (* sys_now_ms: the scan clock, not an elapsed time *)
  LO    : REAL;      (* the published band; HI <= LO publishes unclamped *)
  HI    : REAL;
END_VAR
VAR_OUTPUT
  Y : REAL;
  (* Q_GOOD this beat measured it; Q_HELD it holds an average of readings it
     did measure; Q_NONE nothing has ever fed it, so Y is the band's floor. *)
  Q : QUALITY;
END_VAR
VAR
  avg  : REAL;
  last : UDINT;
  primed : BOOL := FALSE;
END_VAR
VAR_TEMP
  dt_s  : REAL;
  alpha : REAL;
END_VAR
  (* Call it every scan and say so with Q_IN, rather than skipping it while
     the reading is out: skipped, the first sample after the outage arrives
     with the whole outage as its interval and the average SNAPS to it instead
     of rejoining where it left. Five minutes against a 30 s tau is alpha 0.91. *)
  Q := QUAL(GOOD(Q_IN), primed);
  IF NOT GOOD(Q_IN) THEN
    last := NOW;
    (* Primed, Y keeps the last average and Q says nothing measured it this
       beat. Never primed, nothing has EVER measured this: the band's floor
       rather than a default zero, which on a temperature reads like a
       measurement of a freezing house. *)
    IF NOT primed THEN Y := LO; END_IF;
    RETURN;
  END_IF;

  IF NOT primed THEN
    (* Starts AT its first reading rather than climbing to it from zero. The
       clock is adopted with it, so the first real interval is the second
       scan's and never the time since boot. *)
    avg    := X;
    primed := TRUE;
  ELSIF TAU_S <= 0.0 THEN
    avg := X;
  ELSE
    dt_s  := UDINT_TO_REAL(NOW - last) / 1000.0;
    alpha := dt_s / (TAU_S + dt_s);
    avg   := avg + alpha * (X - avg);
  END_IF;
  last := NOW;

  IF HI > LO THEN Y := LIMIT(LO, avg, HI); ELSE Y := avg; END_IF;
END_FUNCTION_BLOCK
