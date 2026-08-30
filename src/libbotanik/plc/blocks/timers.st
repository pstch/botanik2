(* The ST source for Tof, beside the C++ it replaces.
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
