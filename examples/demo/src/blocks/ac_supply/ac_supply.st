(* AC_SUPPLY function block
   ------------------------------------------------------------------------ *)

FUNCTION_BLOCK AC_MIRROR
(* One quantity, deadbanded onto the status row a supervisor reads. The band is
   about the METER's last digit and not about the plant, so it is stated here
   rather than as a parameter: a supply an operator has to re-tune to keep
   quiet is a defect.

   With nothing measured the row HOLDS its last reading and Q says so. A zero
   is what a meter reads when the supply is DEAD, not when nobody asked it. *)
VAR_INPUT
  V    : REAL;
  V_Q  : QUALITY;
  BAND : REAL;
END_VAR
VAR_OUTPUT
  Y : REAL;
  (* Q_HELD once a reading has been held; before the first one Y is the
     block's own zero, which is what a DEAD supply reads. *)
  Q : QUALITY;
END_VAR
VAR
  db   : DEADBAND;
  seen : BOOL := FALSE;
END_VAR
  IF GOOD(V_Q) THEN db(V := V, BAND := BAND); seen := TRUE; END_IF;
  Y := db.Y;
  Q := QUAL(GOOD(V_Q), seen);
END_FUNCTION_BLOCK


FUNCTION_BLOCK AC_SUPPLY
(* One AC supply, watched: four readings mirrored onto status rows and judged
   against their bands. *)
VAR_INPUT
  V     : REAL;
  V_Q   : QUALITY;
  A     : REAL;
  A_Q   : QUALITY;
  W     : REAL;
  W_Q   : QUALITY;
  HZ    : REAL;
  HZ_Q  : QUALITY;
  LO_V  : REAL;
  HI_V  : REAL;
  LO_HZ : REAL;
  HI_HZ : REAL;
END_VAR
VAR_OUTPUT
  OUT_V    : REAL;
  OUT_V_Q  : QUALITY;
  OUT_A    : REAL;
  OUT_A_Q  : QUALITY;
  OUT_W    : REAL;
  OUT_W_Q  : QUALITY;
  OUT_HZ   : REAL;
  OUT_HZ_Q : QUALITY;
  OK       : BOOL;
  FAULT    : BOOL;
  ANSWERED : BOOL;   (* every quantity in, not just the two judged on *)
END_VAR
VAR
  mv  : AC_MIRROR;
  ma  : AC_MIRROR;
  mw  : AC_MIRROR;
  mhz : AC_MIRROR;
  seen : BOOL := FALSE;
END_VAR
VAR_TEMP
  measured : BOOL;
  in_spec  : BOOL;
END_VAR
  (* The volts and the frequency are what the VERDICT is made of, so those two
     decide whether there is a verdict at all. Current and power are reported
     either way: a meter half answering is still worth what it does answer. *)
  measured := GOOD(V_Q) AND GOOD(HZ_Q);
  IF measured THEN seen := TRUE; END_IF;

  mv (V := V,  V_Q := V_Q,  BAND := 0.5);
  ma (V := A,  V_Q := A_Q,  BAND := 0.1);
  mw (V := W,  V_Q := W_Q,  BAND := 5.0);
  mhz(V := HZ, V_Q := HZ_Q, BAND := 0.05);

  OUT_V  := mv.Y;   OUT_V_Q  := mv.Q;
  OUT_A  := ma.Y;   OUT_A_Q  := ma.Q;
  OUT_W  := mw.Y;   OUT_W_Q  := mw.Q;
  OUT_HZ := mhz.Y;  OUT_HZ_Q := mhz.Q;

  in_spec := V >= LO_V AND V <= HI_V AND HZ >= LO_HZ AND HZ <= HI_HZ;
  OK      := measured AND in_spec;

  (* A meter that has never answered is BRING-UP, not a supply fault: the bus
     has not got round to it yet, and alarming on that annunciates every boot.
     Once it has answered, silence is exactly the fault worth raising. *)
  FAULT := seen AND (NOT measured OR NOT in_spec);

  ANSWERED := measured AND GOOD(A_Q) AND GOOD(W_Q);
END_FUNCTION_BLOCK
