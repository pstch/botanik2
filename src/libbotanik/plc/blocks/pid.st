(* The PID loop
   ------------------------------------------------------------------------ *)

(* Ours, not the standard's: matiec's lib/*.txt are GPL and its C runtime is
   LGPL, so nothing here derives from either. The block takes NOW - the scan
   clock - rather than an elapsed time, because a block that reads a clock
   cannot be tested at a hundred times real speed, and one handed dt cannot be
   moved between tasks. *)

FUNCTION_BLOCK PID
(* Holds a reading at a setpoint. Direct-acting by default: the output rises as
   the reading FALLS (heating, filling). Three departures from the textbook sum,
   all affecting tuning: derivative on the MEASUREMENT rather than the error, so
   a typed setpoint does not slam the output; conditional integration, so there
   is no back-calculation gain to tune; and the integral clamped to the output
   SPAN rather than to its limits.

   RESET clears the history in place - the loop still runs this scan, on a cold
   integral. That is what a law does when it stops being able to see the plant:
   effort wound up against a house nobody is measuring is a claim about a past
   the loop no longer has. *)
VAR_INPUT
  SP      : REAL;
  PV      : REAL;
  KP      : REAL;
  KI      : REAL;    (* per (error x second); 0 is NO integral action *)
  KD      : REAL;
  OUT_MIN : REAL;
  OUT_MAX : REAL;
  D_TAU_S : REAL;    (* derivative lowpass; 0 is unfiltered *)
  REVERSE : BOOL;    (* output rises as the reading does *)
  RESET   : BOOL;
  NOW     : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Y         : REAL;
  ERR       : REAL;   (* sign-corrected: positive asks for more output *)
  INTEGRAL  : REAL;   (* in OUTPUT units - what to watch when a loop misbehaves *)
  SATURATED : BOOL;
END_VAR
VAR
  last    : UDINT;
  seeded  : BOOL := FALSE;
  prev_pv : REAL;
  deriv   : REAL;
END_VAR
VAR_TEMP
  dt_s      : REAL;
  d_raw     : REAL;
  alpha     : REAL;
  p_term    : REAL;
  d_term    : REAL;
  trial     : REAL;
  winding   : BOOL;
  unclamped : REAL;
  i_lo      : REAL;
  i_hi      : REAL;
END_VAR
  IF RESET THEN
    INTEGRAL := 0.0;
    prev_pv  := 0.0;
    deriv    := 0.0;
    seeded   := FALSE;
  END_IF;

  (* The first scan adopts the reading, so the derivative starts at zero rather
     than at PV/dt, which is a very large number. *)
  IF seeded THEN
    dt_s := UDINT_TO_REAL(NOW - last) / 1000.0;
  ELSE
    dt_s      := 0.0;
    prev_pv := PV;
    seeded  := TRUE;
  END_IF;
  last := NOW;

  IF REVERSE THEN ERR := PV - SP; ELSE ERR := SP - PV; END_IF;

  IF dt_s > 0.0 THEN
    d_raw := (PV - prev_pv) / dt_s;
    IF D_TAU_S > 0.0 THEN
      alpha := dt_s / (D_TAU_S + dt_s);
      deriv := deriv + alpha * (d_raw - deriv);
    ELSE
      deriv := d_raw;
    END_IF;
    prev_pv := PV;
  END_IF;

  (* Derivative on the measurement opposes the reading's movement, so it enters
     with the sign the action gives it. *)
  p_term := KP * ERR;
  IF REVERSE THEN d_term := KD * deriv; ELSE d_term := -(KD * deriv); END_IF;

  IF KI = 0.0 THEN
    (* No integral ACTION means no integral TERM, not a frozen one: KI is
       operator-editable, and whatever had accumulated would go on commanding
       the output for ever with nothing able to remove it. *)
    INTEGRAL := 0.0;
  ELSIF dt_s > 0.0 THEN
    (* Conditional integration: only when it does not push further into a limit
       the output is already against. *)
    trial := p_term + INTEGRAL + d_term;
    winding := (trial >= OUT_MAX AND ERR > 0.0)
            OR (trial <= OUT_MIN AND ERR < 0.0);
    IF NOT winding THEN
      INTEGRAL := INTEGRAL + KI * ERR * dt_s;
      (* Clamped so the integral can SPAN the output, NOT to the output's own
         limits: with a non-zero floor, LIMIT(20, i, 100) would clamp a fresh
         integral of 0.01 UP to 20 and the winding test would pin it there for
         good. Zero has to stay reachable from either side. *)
      IF OUT_MIN < 0.0 THEN i_lo := OUT_MIN; ELSE i_lo := 0.0; END_IF;
      IF OUT_MAX > 0.0 THEN i_hi := OUT_MAX; ELSE i_hi := 0.0; END_IF;
      INTEGRAL := LIMIT(i_lo, INTEGRAL, i_hi);
    END_IF;
  END_IF;

  unclamped   := p_term + INTEGRAL + d_term;
  SATURATED := (unclamped > OUT_MAX) OR (unclamped < OUT_MIN);
  Y         := LIMIT(OUT_MIN, unclamped, OUT_MAX);
END_FUNCTION_BLOCK
