(* The ST source for Ewma, beside the C++ it replaces.
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

FUNCTION_BLOCK EWMA
VAR_INPUT
  X     : REAL;
  TAU_S : REAL;
  NOW   : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Y : REAL;
END_VAR
VAR
  last   : UDINT;
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  dt_s  : REAL;
  alpha : REAL;
END_VAR
  IF NOT seeded THEN
    (* Starts AT its first reading rather than climbing to it from zero. The
       clock is adopted with it, so the first real interval is the second
       scan's and never the time since boot. *)
    Y := X;
    last := NOW;
    seeded := TRUE;
  ELSIF TAU_S <= 0.0 THEN
    Y := X;
    last := NOW;
  ELSE
    dt_s  := UDINT_TO_REAL(NOW - last) / 1000.0;
    last  := NOW;
    alpha := dt_s / (TAU_S + dt_s);
    Y := Y + alpha * (X - Y);
  END_IF;
END_FUNCTION_BLOCK
