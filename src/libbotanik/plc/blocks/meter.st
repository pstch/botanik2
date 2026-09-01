(* The ST source for Integral and Derivative, beside the C++ they replace.

   Both take NOW - the scan clock - and difference it themselves, so they are
   rate-independent and survive being moved between tasks. HOLD is "keep the
   clock, count nothing": see EWMA for why a clocked block needs one rather
   than being skipped or disabled with EN. *)

FUNCTION_BLOCK INTEGRAL
(* Count RATE for as long as it has been in force. Needs the REAL elapsed: a
   program that ran 400 ms late delivered 400 ms of water. *)
VAR_INPUT
  RATE_PER_S : REAL;
  CUTOFF     : REAL;   (* below this the rate counts as zero *)
  HOLD       : BOOL;
  SEED       : REAL;   (* the total the FIRST scan starts from: a retained row
                          carries what was metered before this run, and a
                          totaliser that restarts at zero unmakes it *)
  NOW        : UDINT;
END_VAR
VAR_OUTPUT
  TOTAL : REAL;
  RATE  : REAL;   (* the rate actually counted, after the cutoff *)
END_VAR
VAR
  last   : UDINT;
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  elapsed : UDINT;
  r       : REAL;
END_VAR
  IF seeded THEN
    elapsed := NOW - last;
  ELSE
    elapsed := 0;
    seeded  := TRUE;
    TOTAL   := SEED;
  END_IF;
  last := NOW;

  IF NOT HOLD THEN
    r := RATE_PER_S;
    (* A meter idling on noise must not total a plausible volume with the pump
       off, so anything inside the cutoff counts as nothing at all. *)
    IF CUTOFF > 0.0 AND r < CUTOFF AND r > (0.0 - CUTOFF) THEN r := 0.0; END_IF;
    RATE  := r;
    TOTAL := TOTAL + r * (UDINT_TO_REAL(elapsed) / 1000.0);
  END_IF;
END_FUNCTION_BLOCK


FUNCTION_BLOCK DERIVATIVE
(* The rate a quantity is changing at, over a WINDOW rather than per scan: a
   pulse meter counts in whole litres, so scan to scan it reads either zero or
   one pulse's worth and never the flow. *)
VAR_INPUT
  QUANTITY  : REAL;
  WINDOW_MS : UDINT;   (* 0 differentiates every scan *)
  PRESET    : BOOL;    (* adopt QUANTITY without reporting a rate *)
  NOW       : UDINT;
END_VAR
VAR_OUTPUT
  RATE : REAL;   (* per second; the last COMPLETED window, held between them *)
END_VAR
VAR
  prev    : REAL;
  window  : UDINT;   (* time accumulated into this window *)
  last    : UDINT;
  seeded  : BOOL := FALSE;
END_VAR
VAR_TEMP
  elapsed : UDINT;
END_VAR
  IF seeded THEN elapsed := NOW - last;
  ELSE elapsed := 0;
  END_IF;
  last := NOW;

  IF PRESET OR NOT seeded THEN
    (* Adopt the reading rather than differencing against a zero: the first
       window would otherwise report the whole counter as a rate. *)
    prev   := QUANTITY;
    window := 0;
    seeded := TRUE;
    RATE   := 0.0;
  ELSE
    window := window + elapsed;
    IF window > 0 AND window >= WINDOW_MS THEN
      (* Over the time actually accumulated, not the nominal window: a late
         scan overshoots it, and the nominal figure would report a rate that
         never happened. *)
      RATE   := (QUANTITY - prev) / (UDINT_TO_REAL(window) / 1000.0);
      prev   := QUANTITY;
      window := 0;
    END_IF;
  END_IF;
END_FUNCTION_BLOCK
