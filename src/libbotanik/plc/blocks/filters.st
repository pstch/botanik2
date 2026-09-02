(* The one filter every measured row is published through
   ------------------------------------------------------------------------ *)

FUNCTION_BLOCK EWMA
(* An exponentially-weighted moving average of X, clamped to the row's band,
   and the verdict on whether this beat had anything to average. *)
VAR_INPUT
  X     : REAL;
  OK_IN : BOOL;    (* something believable fed X this beat *)
  TAU_S : REAL;
  NOW   : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
  LO    : REAL;    (* the published band; HI <= LO publishes unclamped *)
  HI    : REAL;
END_VAR
VAR_OUTPUT
  Y  : REAL;
  OK : BOOL;   (* this beat MEASURED it *)
END_VAR
VAR
  avg  : REAL;
  last : UDINT;
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  dt_s  : REAL;
  alpha : REAL;
END_VAR
  (* Call it every scan and say so with OK_IN, rather than skipping it while
     the reading is out: skipped, the first sample after the outage arrives
     with the whole outage as its interval and the average SNAPS to it instead
     of rejoining where it left. Five minutes against a 30 s tau is alpha 0.91. *)
  OK := OK_IN;
  IF NOT OK_IN THEN
    last := NOW;
    (* Seeded, Y keeps the last average and OK says it is stale. Never seeded,
       nothing has EVER measured this: the band's floor rather than a default
       zero, which on a temperature reads like a measurement of a freezing
       house. *)
    IF NOT seeded THEN Y := LO; END_IF;
    RETURN;
  END_IF;

  IF NOT seeded THEN
    (* Starts AT its first reading rather than climbing to it from zero. The
       clock is adopted with it, so the first real interval is the second
       scan's and never the time since boot. *)
    avg    := X;
    seeded := TRUE;
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
