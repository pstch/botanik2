(* day_phase_controller program
   ------------------------------------------------------------------------ *)

PROGRAM day_phase_controller
VAR_EXTERNAL CONSTANT
  cli_temp_in          : REAL;
  cli_temp_in_qlt      : QUALITY;
  sol_ready            : BOOL;
  sol_fromRise_min     : INT;
  sol_fromSet_min      : INT;
  cli_coldBankTrg_degC : REAL;
  cli_heatBankTrg_degC : REAL;
  sol_coldBankRise_min : INT;
  sol_breezeRise_min   : INT;
  sol_dayRise_min      : INT;
  sol_heatBankSet_min  : INT;
  sol_duskSet_min      : INT;
  sol_nightSet_min     : INT;
END_VAR
VAR_EXTERNAL
  cli_phase             : USINT;
  cli_phase_qlt         : QUALITY;
  cli_coldBankArm       : BOOL;
  cli_coldBankArm_qlt   : QUALITY;
  cli_heatBankArm       : BOOL;
  cli_heatBankArm_qlt   : QUALITY;
  cli_prevDayMax_degC   : REAL;
  cli_prevDayMax_degC_qlt   : QUALITY;
  cli_prevNightMin_degC : REAL;
  cli_prevNightMin_degC_qlt : QUALITY;
END_VAR
VAR
  phase    : USINT := 0;   (* PHASE_NIGHT *)
  seeded   : BOOL  := FALSE;
  day_max   : REAL;
  night_min : REAL;
END_VAR
VAR_TEMP
  prev_phase : USINT;
END_VAR

  (* The extremes start at the house as it is: from zero, the first beat of
     every boot would arm a cold bank. *)
  IF NOT seeded THEN
    day_max   := cli_temp_in;
    night_min := cli_temp_in;
    seeded    := TRUE;
  END_IF;
  IF cli_temp_in > day_max   THEN day_max   := cli_temp_in; END_IF;
  IF cli_temp_in < night_min THEN night_min := cli_temp_in; END_IF;

  prev_phase := phase;

  (* Where the day is, as sun_watch published it. Unknown until the tracker has
     a location and a wall clock - every boot, for as long as NTP takes - and
     the phase HOLDS through that rather than announcing a night the plant is
     not in.

     Down the day IN ORDER, so a boundary an operator has trimmed past the one
     after it shortens its own phase rather than swallowing the next.
     0 NIGHT, 1 COLD_BANK, 2 DAWN_BREEZE, 3 DAY, 4 HEAT_BANK, 5 DUSK_BREEZE. *)
  IF sol_ready THEN
    IF sol_fromRise_min < sol_coldBankRise_min THEN
      phase := 0;
    ELSIF sol_fromRise_min < sol_breezeRise_min THEN
      IF cli_coldBankArm THEN phase := 1; ELSE phase := 0; END_IF;
    ELSIF sol_fromRise_min < sol_dayRise_min THEN
      phase := 2;
    ELSIF sol_fromSet_min < sol_heatBankSet_min THEN
      phase := 3;
    ELSIF sol_fromSet_min < sol_duskSet_min THEN
      IF cli_heatBankArm THEN phase := 4; ELSE phase := 3; END_IF;
    ELSIF sol_fromSet_min < sol_nightSet_min THEN
      phase := 5;
    ELSE
      phase := 0;
    END_IF;
  END_IF;

  (* The extremes are gathered PER PHASE, not per calendar day: what a cold
     bank needs is how hot it got before it, and the phase boundary is where
     that window closes. *)
  IF phase = 3 AND prev_phase <> 3 THEN day_max   := cli_temp_in; END_IF;
  IF phase = 0 AND prev_phase <> 0 THEN night_min := cli_temp_in; END_IF;

  cli_phase     := phase;
  (* WHERE the phase came from, not whether it is a legal one - it always is,
     because an unplaced phase HOLDS rather than reading as nothing. A law gates
     on this where NIGHT would make it act wrongly, and not where NIGHT simply
     makes it do nothing. *)
  cli_phase_qlt := QUAL(sol_ready);

  (* An accumulation, and a probe that has gone stops adding to it: what is
     published is the window as it stood when the house was last visible. *)
  cli_prevDayMax_degC   := day_max;
  cli_prevNightMin_degC := night_min;
  (* A window accumulated out of a probe nobody can read is a window over
     nothing, so both carry the reading's own verdict. *)
  cli_prevDayMax_degC_qlt   := cli_temp_in_qlt;
  cli_prevNightMin_degC_qlt := cli_temp_in_qlt;

  (* Whether the bank phases have anything to do when their turn comes. Both
     can be armed at once - a clear late-spring day is hot and its night cold,
     and the two are decided independently.

     Off the ROWS just written, not the locals behind them: forcing one is how
     a cold-bank morning gets rehearsed in July, and a law arming on its own
     copy would leave the page saying something it was not doing. *)
  cli_coldBankArm     := cli_prevDayMax_degC   > cli_coldBankTrg_degC;
  cli_coldBankArm_qlt := cli_prevDayMax_degC_qlt;
  cli_heatBankArm     := cli_prevNightMin_degC < cli_heatBankTrg_degC;
  cli_heatBankArm_qlt := cli_prevNightMin_degC_qlt;
END_PROGRAM
