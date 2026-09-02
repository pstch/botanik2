(* The two dwell timers: off-delay and on-delay, in milliseconds
   ------------------------------------------------------------------------ *)

(* Off-delay: true while IN is, and for PT after it drops. An overrun - "keep
   the roof shut for a quarter of an hour after the last drop". *)
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
  (* Measured from when this timer last ran, so a scan it missed costs it
     nothing. *)
  last : UDINT;
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
  last : UDINT;
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
