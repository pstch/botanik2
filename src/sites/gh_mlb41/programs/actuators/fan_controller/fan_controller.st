(* fan_controller program
   ------------------------------------------------------------------------ *)

PROGRAM fan_controller
VAR_EXTERNAL CONSTANT
  cli_stratDem   : REAL;
  cli_coolDem    : REAL;
  cli_fanIrrDem  : REAL;
  irr_state      : IrrState;
  cli_phase      : DayPhase;
  cli_breezeDem  : REAL;
  cli_fanLowDem  : REAL;
  cli_fanMedDem  : REAL;
  cli_fanHighDem : REAL;
  cli_fanDemHyst : REAL;
END_VAR
VAR_EXTERNAL
  cli_destrat1_speed1 : BOOL;
  cli_destrat1_speed2 : BOOL;
  cli_destrat1_speed3 : BOOL;
  cli_destrat2_speed1 : BOOL;
  cli_destrat2_speed2 : BOOL;
  cli_destrat2_speed3 : BOOL;
  cli_fanSpeed        : USINT;
END_VAR
VAR
  steps : STAIRCASE3;
END_VAR
VAR_TEMP
  watering : REAL;
  breeze   : REAL;
  demand   : REAL;
END_VAR

  (* Both demands declare the low band, so an unbelievable one already reads as
     the floor: still air is a rot risk and the fans keep turning. *)

  (* A cycle in progress wants air moving over the trays it has just wetted. *)
  IF irr_state <> IRR_IDLE THEN watering := cli_fanIrrDem;
  ELSE watering := 0.0; END_IF;

  (* The day's two breezes. At dusk it dries the leaves before the stomata
     open - these plants breathe at night, and a wet leaf cannot - and at dawn
     it takes the night's condensation off them. A FLOOR like the others. *)
  IF cli_phase = PHASE_DAWN_BREEZE OR cli_phase = PHASE_DUSK_BREEZE THEN
    breeze := cli_breezeDem;
  ELSE breeze := 0.0; END_IF;

  demand := MAX(MAX(cli_stratDem, cli_coolDem), MAX(watering, breeze));

  (* Three rungs, each engaging at its own threshold and released a little
     under it, which keeps a demand sitting on an edge from stepping the
     relays. The same block the destratification ladder uses. *)
  steps.IN   := demand;
  steps.LO   := cli_fanLowDem;
  steps.MED  := cli_fanMedDem;
  steps.HI   := cli_fanHighDem;
  steps.HYST := cli_fanDemHyst;
  steps();

  cli_destrat1_speed1 := steps.Q = 1;
  cli_destrat2_speed1 := steps.Q = 1;
  cli_destrat1_speed2 := steps.Q = 2;
  cli_destrat2_speed2 := steps.Q = 2;
  cli_destrat1_speed3 := steps.Q = 3;
  cli_destrat2_speed3 := steps.Q = 3;
  cli_fanSpeed         := steps.Q;
END_PROGRAM
