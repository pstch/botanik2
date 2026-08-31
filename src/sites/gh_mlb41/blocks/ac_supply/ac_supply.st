(* The ST source for AcSupply, beside the C++ it replaces.

   The mirror exists because a field-read row carries no Modbus register of its
   own - only parameters, status values and alarms are mapped - so an external
   master cannot see the meter without one. *)

FUNCTION_BLOCK AC_MIRROR
(* One quantity, deadbanded onto its status row. The band is about the METER's
   last digit and not about the plant, so it is stated here rather than as a
   parameter: a supply an operator has to re-tune to keep quiet is a defect.

   With nothing measured the row HOLDS its last reading and OK says so. It used
   to fall back to the row's declared 0, which is what a meter reads when the
   supply is DEAD rather than when nobody asked it. *)
VAR_INPUT
  V    : REAL;
  V_OK : BOOL;
  BAND : REAL;
END_VAR
VAR_OUTPUT
  Y  : REAL;
  OK : BOOL;
END_VAR
VAR
  (* NOT `band`: IEC folds case, so the instance would be the VAR_INPUT. *)
  db : DEADBAND;
END_VAR
  OK := V_OK;
  IF V_OK THEN db(V := V, BAND := BAND); END_IF;
  Y := db.Y;
END_FUNCTION_BLOCK


FUNCTION_BLOCK AC_SUPPLY
(* One AC supply, watched: four readings mirrored onto status rows and judged
   against their bands. *)
VAR_INPUT
  V     : REAL;
  V_OK  : BOOL;
  A     : REAL;
  A_OK  : BOOL;
  W     : REAL;
  W_OK  : BOOL;
  HZ    : REAL;
  HZ_OK : BOOL;
  LO_V  : REAL;
  HI_V  : REAL;
  LO_HZ : REAL;
  HI_HZ : REAL;
END_VAR
VAR_OUTPUT
  OUT_V     : REAL;
  OUT_V_OK  : BOOL;
  OUT_A     : REAL;
  OUT_A_OK  : BOOL;
  OUT_W     : REAL;
  OUT_W_OK  : BOOL;
  OUT_HZ    : REAL;
  OUT_HZ_OK : BOOL;
  OK        : BOOL;
  FAULT     : BOOL;
  ANSWERED  : BOOL;   (* every quantity in, not just the two judged on *)
END_VAR
VAR
  mv   : AC_MIRROR;
  ma   : AC_MIRROR;
  mw   : AC_MIRROR;
  mhz  : AC_MIRROR;
  seen : BOOL := FALSE;
END_VAR
VAR_TEMP
  measured : BOOL;
  in_spec  : BOOL;
END_VAR
  (* The volts and the frequency are what the VERDICT is made of, so those two
     decide whether there is a verdict at all. Current and power are reported
     either way: a meter half answering is still worth what it does answer. *)
  measured := V_OK AND HZ_OK;
  IF measured THEN seen := TRUE; END_IF;

  mv (V := V,  V_OK := V_OK,  BAND := 0.5);
  ma (V := A,  V_OK := A_OK,  BAND := 0.1);
  mw (V := W,  V_OK := W_OK,  BAND := 5.0);
  mhz(V := HZ, V_OK := HZ_OK, BAND := 0.05);

  OUT_V  := mv.Y;   OUT_V_OK  := mv.OK;
  OUT_A  := ma.Y;   OUT_A_OK  := ma.OK;
  OUT_W  := mw.Y;   OUT_W_OK  := mw.OK;
  OUT_HZ := mhz.Y;  OUT_HZ_OK := mhz.OK;

  in_spec := V >= LO_V AND V <= HI_V AND HZ >= LO_HZ AND HZ <= HI_HZ;
  OK      := measured AND in_spec;

  (* A meter that has never answered is BRING-UP, not a supply fault: the bus
     has not got round to it yet, and alarming on that annunciates every boot.
     Once it has answered, silence is exactly the fault worth raising. *)
  FAULT := seen AND (NOT measured OR NOT in_spec);

  (* Each mirror is its own network of one input, and one that is out is a
     network this block is not producing. So the verdict and the block being
     WHOLE are two questions, and this is the second. *)
  ANSWERED := measured AND A_OK AND W_OK;
END_FUNCTION_BLOCK
