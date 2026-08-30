(* botanik's function block library, as ST.
 *
 * Ours, not the standard's: matiec's lib/*.txt are GPL and its C runtime
 * timers are LGPL, so nothing here is derived from either. The interfaces
 * follow IEC's shape (IN/PT/Q) where there is one, but every block takes DT_MS -
 * the scan's elapsed milliseconds - as an input rather than reading a clock.
 * The scan already threads dt everywhere, and a block that asks for it cannot
 * silently change its time constant when a beat slips.
 *)

(* Off-delay: follows IN up at once, holds the fall for PT. *)
FUNCTION_BLOCK TOF_MS
VAR_INPUT
  IN : BOOL;
  PT : UDINT;   (* ms *)
  DT_MS : UDINT;   (* ms elapsed this scan *)
END_VAR
VAR_OUTPUT
  Q  : BOOL;
  ET : UDINT;
END_VAR
  IF IN THEN
    ET := 0;
    Q  := TRUE;
  ELSIF Q THEN
    (* Saturating, so a scan that arrives after PT cannot wrap the counter. *)
    IF ET > UDINT#4000000000 - DT_MS THEN ET := UDINT#4000000000;
    ELSE ET := ET + DT_MS;
    END_IF;
    IF ET >= PT THEN Q := FALSE; END_IF;
  END_IF;
END_FUNCTION_BLOCK

(* First-order lag. The blend is DT_MS / (TAU + DT_MS), not DT_MS / TAU: the exact
 * first-order response, and one that cannot exceed 1 when a scan comes in
 * later than the time constant. Adopts its first sample whole. *)
FUNCTION_BLOCK EWMA
VAR_INPUT
  X     : REAL;
  TAU_S : REAL;
  DT_MS    : UDINT;   (* ms *)
END_VAR
VAR_OUTPUT
  Y : REAL;
END_VAR
VAR
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  dt_s  : REAL;
  alpha : REAL;
END_VAR
  IF NOT seeded THEN
    Y := X;
    seeded := TRUE;
  ELSIF TAU_S <= 0.0 THEN
    Y := X;
  ELSE
    dt_s  := UDINT_TO_REAL(DT_MS) / 1000.0;
    alpha := dt_s / (TAU_S + dt_s);
    Y := Y + alpha * (X - Y);
  END_IF;
END_FUNCTION_BLOCK

(* Three rungs on one staircase, each engaging AT its threshold and released a
 * common hysteresis below it. *)
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
  IF    IN >= HI  THEN Q := 3;
  ELSIF IN >= MED THEN Q := 2;
  ELSIF IN >= LO  THEN Q := 1;
  ELSIF IN <  LO - HYST THEN Q := 0;
  END_IF;
END_FUNCTION_BLOCK
