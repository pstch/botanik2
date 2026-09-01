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
  HOLD  : BOOL;    (* keep the clock, fold nothing: there is no sample *)
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
  (* HOLD rather than not being CALLED, and the difference is the whole reason
     it is an input. This block reads the CLOCK, so a caller that skips it
     while its input is out leaves `last` behind: the first sample after the
     outage arrives with the whole outage as dt, alpha goes to nearly 1, and
     the average SNAPS to the new reading instead of rejoining where it left.
     Five minutes against a 30 s tau is alpha 0.91. EN would do the same thing
     - matiec's guard returns before every statement, this one included. *)
  IF HOLD THEN
    last := NOW;
  ELSIF NOT seeded THEN
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


FUNCTION_BLOCK VALIDATED_EWMA
(* A vote, filtered, clamped to the row's band, and told whether this beat
   measured it. What every redundant-probe chain does with its vote, so the
   rule for a chain that goes out is written once. *)
VAR_INPUT
  X     : REAL;
  OK_IN : BOOL;    (* something believable fed X this beat *)
  TAU_S : REAL;
  NOW   : UDINT;
  LO    : REAL;
  HI    : REAL;
END_VAR
VAR_OUTPUT
  Y  : REAL;
  OK : BOOL;       (* this beat MEASURED it *)
END_VAR
VAR
  filt   : EWMA;
  seeded : BOOL := FALSE;
END_VAR
  (* CALLED every scan, folding only when something answered: a filter skipped
     for the length of an outage snaps to the first reading back rather than
     rejoining the average it left. @see EWMA.HOLD *)
  filt(X := X, TAU_S := TAU_S, HOLD := NOT OK_IN, NOW := NOW);
  OK := OK_IN;
  IF OK_IN THEN
    seeded := TRUE;
    Y := LIMIT(LO, filt.Y, HI);
  ELSIF NOT seeded THEN
    (* Nothing has EVER measured this. The band's floor rather than a default
       zero, which on a temperature reads like a measurement of a freezing
       house. Once seeded, Y keeps the last average and OK says it is stale. *)
    Y := LO;
  END_IF;
END_FUNCTION_BLOCK
