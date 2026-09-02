(* screen_controller program
   ------------------------------------------------------------------------ *)

PROGRAM screen_controller
VAR_EXTERNAL CONSTANT
  cli_coolDem          : REAL;
  cli_coolDem_qlt      : BOOL;
  cli_thermalScreenDem : REAL;
  cli_phase            : USINT;
  cli_screenTravel_sec : UINT;
  cli_screenTmo_sec    : UINT;
  sys_now_ms           : UDINT;
END_VAR
VAR_EXTERNAL
  cli_screen       : BOOL;
  cli_screenMoving : BOOL;
END_VAR
VAR
  fabric : ACTUATOR;
END_VAR
VAR_TEMP
  cold   : BOOL;
  allows : BOOL;
  travel : UDINT;
  tmo    : UDINT;
  stroke : UDINT;
  want   : REAL;
END_VAR
  (* An unknown cooling demand DEPLOYS it: that is this one's safe state, and a
     greenhouse nobody can see loses heat through the roof all night. Free,
     too - the fabric deploys on the de-energised coil.

     No dead band on this one, and none is needed: the permission table below
     makes every phase ONE-DIRECTIONAL, so a demand sitting on the rung can
     move the fabric at most once per phase whatever it does afterwards. *)
  cold := NOT cli_coolDem_qlt OR cli_coolDem <= cli_thermalScreenDem;

  (* Deploying belongs to the half of the day that is closing down, retracting
     to the half that is opening up. An hour of cold at midday therefore never
     shuts the fabric against the sun, and a mild night never opens it and
     gives back the heat it was holding.
     0 NIGHT, 1 COLD_BANK, 2 DAWN_BREEZE, 3 DAY, 4 HEAT_BANK, 5 DUSK_BREEZE. *)
  IF cold THEN
    allows := cli_phase = 0 OR cli_phase = 5 OR cli_phase = 4;
  ELSE
    allows := cli_phase = 3 OR cli_phase = 2 OR cli_phase = 1;
  END_IF;

  (* The phase gives PERMISSION, the demand asks for the move - and nothing
     else does. Denied, the command STANDS, read back off the row an operator
     can force rather than off a copy of it here. *)
  IF allows THEN cli_screen := cold; END_IF;

  travel := SEC_MS(cli_screenTravel_sec);
  tmo    := SEC_MS(cli_screenTmo_sec);
  (* Capped at the actuator's own SAFE time, which is the datasheet's number
     where the travel is the operator's: this traverse is what the fans are cut
     for and what a parking supervisor waits on, so a mis-set travel must not
     hold both for as long as it says. *)
  IF travel < tmo THEN stroke := travel; ELSE stroke := tmo; END_IF;

  IF cli_screen THEN want := 1.0; ELSE want := 0.0; END_IF;

  (* Stepped from the ROW, which is what this law asked for and what an
     operator may have forced - so a screen parked on the way to its safe
     position still reports itself as moving and the fans stay cut.

     No seating run and no reversal dead time: the coil is HELD rather than
     driven, so the fabric starts back the moment the command does and the
     stroke is the whole of the traverse. DEPLOYED at bring-up, and known to
     be: this coil deploys on the de-energised one, so a controller that was
     not driving anything left the fabric out. Presetting says that, where
     UNKNOWN would send it on a full datum stroke to learn what the wiring
     already guarantees. *)
  fabric.TARGET       := want;
  fabric.OPEN_MS      := stroke;
  fabric.CLOSE_MS     := stroke;
  fabric.SEAT_MS      := 0;
  fabric.MIN_RUN_MS   := 250;
  fabric.REVERSE_MS   := 0;
  fabric.DB           := 0.02;
  fabric.REDATUM      := 3.0;
  fabric.HOLD         := FALSE;
  fabric.PRESET_KNOWN := TRUE;
  fabric.PRESET_POS   := 1.0;
  fabric.NOW          := sys_now_ms;
  fabric();
  cli_screenMoving := fabric.MOVING;
END_PROGRAM
