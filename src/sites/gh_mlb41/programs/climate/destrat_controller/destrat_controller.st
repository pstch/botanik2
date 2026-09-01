(* destrat_controller program
   ------------------------------------------------------------------------ *)

(* STAIRCASE3 comes from src/libbotanik/plc/blocks/analog.st - the library
   block, not a copy. *)

PROGRAM destrat_controller
(* VAR_EXTERNAL, so __INIT_EXTERNAL is the generated bind() and the body
   reads and writes the bank with no copy in or out. *)
VAR_EXTERNAL CONSTANT
  cli_dTStrat_degC     : REAL;
  cli_dTStratLow_degC  : REAL;
  cli_dTStratMed_degC  : REAL;
  cli_dTStratHigh_degC : REAL;
  cli_dTStratHyst_degC : REAL;
  cli_fanLowDem        : REAL;
  cli_fanMedDem        : REAL;
  cli_fanHighDem       : REAL;
END_VAR
VAR_EXTERNAL
  cli_stratDem         : REAL;
END_VAR
VAR
  (* A plain member, like Plc::Staircase3 is today. Its rung survives between
     scans because the instance does, and it costs no row. *)
  bands : STAIRCASE3;
END_VAR
  bands.IN   := cli_dTStrat_degC;
  bands.LO   := cli_dTStratLow_degC;
  bands.MED  := cli_dTStratMed_degC;
  bands.HI   := cli_dTStratHigh_degC;
  bands.HYST := cli_dTStratHyst_degC;
  bands();

  (* The demand is the FANS' OWN rung for that speed. Any other number would
     be translated twice, and the two ladders would drift apart the first time
     one of them was retuned. *)
  CASE bands.Q OF
    1: cli_stratDem := cli_fanLowDem;
    2: cli_stratDem := cli_fanMedDem;
    3: cli_stratDem := cli_fanHighDem;
  ELSE
    cli_stratDem := 0.0;
  END_CASE;
END_PROGRAM
