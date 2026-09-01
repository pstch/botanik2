(* intake_gates program
   ------------------------------------------------------------------------ *)

PROGRAM intake_gates
VAR_EXTERNAL CONSTANT
  cli_temp_in          : REAL;
  cli_temp_cs          : REAL;
  cli_coolDem          : REAL;
  cli_temp_in_qlt      : BOOL;
  cli_temp_cs_qlt      : BOOL;
  cli_coolDem_qlt      : BOOL;
  cli_ventTempMgn_degC : REAL;
  cli_ventHyst_degC    : REAL;
  cli_groundHeatDem    : REAL;
  cli_groundDemHyst    : REAL;
END_VAR
VAR_EXTERNAL
  cli_intakeCoolOk : BOOL;
  cli_groundHeatOk : BOOL;
END_VAR
VAR
  cool_gate   : BAND_LATCH;
  ground_band : BAND_LATCH;
  ground_gate : BAND_LATCH;
END_VAR
VAR_TEMP
  asking : BOOL;
END_VAR
  (* The cooling ladder is the other half of this: a gate is a decision about
     which side of a difference is cooler AND whether anything is being asked
     for. cli_coolDem_qlt is where the ladder says it stopped having an
     opinion, and shutting both gates is not a value any demand expresses.

     Drawing air only moves the interior toward whatever the intake is, so both
     gates are shut while the crawlspace probe is out too - that costs a little
     ventilation and cannot cost a plant: the windows and the fans are on their
     own paths.

     The bands are left where they are: a gate is a command, and shutting is
     the safe answer rather than a stale guess. Stepping them on a reading
     nobody has would advance a hysteresis that is supposed to be a claim about
     a real difference. *)
  IF NOT cli_temp_in_qlt OR NOT cli_coolDem_qlt OR NOT cli_temp_cs_qlt THEN
    cli_intakeCoolOk := FALSE;
    cli_groundHeatOk := FALSE;
  ELSE
    (* Each gate opens AT its margin and closes a band under it, so a
       difference sitting on the margin - which is what two converging
       temperatures do - holds the gate where it is instead of flipping it
       every beat. *)
    cool_gate.IN   := cli_temp_in - cli_temp_cs;
    cool_gate.RISE := cli_ventTempMgn_degC;
    cool_gate.FALL := cli_ventTempMgn_degC - cli_ventHyst_degC;
    cool_gate();
    cli_intakeCoolOk := cool_gate.Q;

    (* The mirror, and the only place the negative half of the ladder does
       anything: cold enough to be asking for heat, and ground warm enough to
       be worth pulling it out of. Both halves are banded - the demand is a PID
       output and it crosses a rung as slowly as it likes. *)
    ground_band.IN   := -cli_coolDem;
    ground_band.RISE := -cli_groundHeatDem;
    ground_band.FALL := -cli_groundHeatDem + cli_groundDemHyst;
    ground_band();
    asking := ground_band.Q;
    ground_gate.IN   := cli_temp_cs - cli_temp_in;
    ground_gate.RISE := cli_ventTempMgn_degC;
    ground_gate.FALL := cli_ventTempMgn_degC - cli_ventHyst_degC;
    ground_gate();
    cli_groundHeatOk := asking AND ground_gate.Q;
  END_IF;
END_PROGRAM
