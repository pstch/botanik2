(* The pulse-width valve driver, with a minimum on-time
   ------------------------------------------------------------------------ *)

(* The FAIL DIRECTION is not here. The C++ block read it off the coil row's
   declared default so the table stayed the one place it was stated - but the
   only caller that ever used it was the skirt's rot lockout, and what that
   branch means is "this valve must never wet condensing air", which is a LAW
   and not a wiring fact. The law says shut; the row says shut; they agree
   without either one deriving the other. *)

FUNCTION_BLOCK VALVE_PULSE
(* A fractional demand as an ON-TIME within a repeating period. Outputs the
   coil state rather than driving it: what a valve is FOR is the program's, and
   a block that wrote the row would need a VAR_EXTERNAL of its own for every
   valve a site has. *)
VAR_INPUT
  DUTY_IN   : REAL;
  PERIOD_MS : UDINT;
  MIN_ON_MS : UDINT;
  RESET     : BOOL;   (* drop the phase, so the next period starts from shut *)
  NOW       : UDINT;  (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Q      : BOOL;
  SERVED : REAL;   (* the on-time fraction actually in force *)
END_VAR
VAR
  phase_ms : UDINT;
  last     : UDINT;
  seeded   : BOOL := FALSE;
END_VAR
VAR_TEMP
  want    : REAL;
  period  : UDINT;
  on_ms   : UDINT;
  serve   : UDINT;
  elapsed : UDINT;
END_VAR
  IF seeded THEN elapsed := NOW - last;
  ELSE elapsed := 0; seeded := TRUE;
  END_IF;
  last := NOW;

  IF RESET THEN phase_ms := 0; END_IF;

  want   := LIMIT(0.0, DUTY_IN, 1.0);
  period := PERIOD_MS;
  IF period = 0 THEN period := 1; END_IF;

  on_ms := REAL_TO_UDINT(want * UDINT_TO_REAL(period));
  (* A pulse under the minimum does not lift the seat, so it is not made:
     rounding it up instead would deliver water nobody asked for. *)
  IF on_ms < MIN_ON_MS THEN serve := 0; ELSE serve := on_ms; END_IF;

  (* MOD, not a loop: a scan arriving after several periods is a scheduling
     fact, not a reason to iterate. *)
  phase_ms := (phase_ms + elapsed) MOD period;

  Q      := phase_ms < serve;
  SERVED := UDINT_TO_REAL(serve) / UDINT_TO_REAL(period);
END_FUNCTION_BLOCK
