(* The ST source for Tof, beside the C++ it replaces.
 *
 * Ours, not the standard's: matiec's lib/*.txt are GPL and its C runtime is
 * LGPL, so nothing here derives from either. The interface follows IEC's
 * shape where there is one, but the block takes DT_MS - the scan's elapsed
 * milliseconds - rather than reading a clock, because the scan already
 * threads elapsed everywhere and a block that reads a clock cannot be tested at
 * a hundred times real speed.
 *
 * The variable DESCRIPTORS stay in C++. ST names a variable; what a name
 * means - its unit, band, register, transport and timeout - is the table's,
 * and matiec never sees it. *)

FUNCTION_BLOCK TOF_MS
VAR_INPUT
  IN  : BOOL;
  PT  : UDINT;   (* ms *)
  NOW : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Q  : BOOL;
  ET : UDINT;
END_VAR
VAR
  (* The block keeps its OWN last reading, so it measures from when IT last
     ran rather than from what a caller believed. That is what makes it
     rate-independent: the same block works on any task, and a scan it missed
     costs it nothing. Unsigned subtraction carries the wrap. *)
  last   : UDINT;
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  elapsed : UDINT;
END_VAR
  IF seeded THEN elapsed := NOW - last;
  ELSE elapsed := 0; seeded := TRUE;
  END_IF;
  last := NOW;

  IF IN THEN
    ET := 0;
    Q  := TRUE;
  ELSIF Q THEN
    (* Saturating, so a scan that arrives after PT cannot wrap the counter. *)
    IF ET > UDINT#4000000000 - elapsed THEN ET := UDINT#4000000000;
    ELSE ET := ET + elapsed;
    END_IF;
    IF ET >= PT THEN Q := FALSE; END_IF;
  END_IF;
END_FUNCTION_BLOCK

FUNCTION_BLOCK TON_MS
(* On-delay: true once IN has held true for PT, false the moment it drops.
   A dwell - "has been wet for 30 s", "has been silent long enough to alarm". *)
VAR_INPUT
  IN  : BOOL;
  PT  : UDINT;   (* ms *)
  NOW : UDINT;   (* sys_now_ms: the scan clock, not an elapsed time *)
END_VAR
VAR_OUTPUT
  Q  : BOOL;
  ET : UDINT;
END_VAR
VAR
  last   : UDINT;
  seeded : BOOL := FALSE;
END_VAR
VAR_TEMP
  elapsed : UDINT;
END_VAR
  IF seeded THEN elapsed := NOW - last;
  ELSE elapsed := 0; seeded := TRUE;
  END_IF;
  last := NOW;

  IF NOT IN THEN
    ET := 0;
    Q  := FALSE;
  ELSE
    (* Saturating, so a scan that arrives after PT cannot wrap the counter. *)
    IF ET > UDINT#4000000000 - elapsed THEN ET := UDINT#4000000000;
    ELSE ET := ET + elapsed;
    END_IF;
    Q := ET >= PT;
  END_IF;
END_FUNCTION_BLOCK
