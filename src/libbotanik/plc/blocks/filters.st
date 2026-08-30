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
