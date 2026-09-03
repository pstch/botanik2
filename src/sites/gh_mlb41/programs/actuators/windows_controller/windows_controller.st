(* windows_controller program
   ------------------------------------------------------------------------ *)

PROGRAM windows_controller
VAR_EXTERNAL CONSTANT
  cli_windowDem        : REAL;
  cli_windowLockout    : BOOL;
  cli_windowCap        : REAL;
  cli_windowTravel_sec : UINT;
  cli_windowTmo_sec    : UINT;
  cli_windowMinRun_ms  : UINT;
  cli_windowReverse_ms : UINT;
  cli_windowDeadband   : REAL;
  cli_windowRedatum    : REAL;
  prog_windows_park    : BOOL;
  sys_now_ms           : UDINT;
END_VAR
VAR_EXTERNAL
  prog_windows_parked : BOOL;
  cli_window1_opn : BOOL;
  cli_window2_opn : BOOL;
  cli_window3_opn : BOOL;
  cli_window4_opn : BOOL;
  cli_window1_clo : BOOL;
  cli_window2_clo : BOOL;
  cli_window3_clo : BOOL;
  cli_window4_clo : BOOL;
  cli_window1_tgt : REAL;
  cli_window2_tgt : REAL;
  cli_window3_tgt : REAL;
  cli_window4_tgt : REAL;
  cli_window1_pos : REAL;
  cli_window2_pos : REAL;
  cli_window3_pos : REAL;
  cli_window4_pos : REAL;
  cli_window1_drift : REAL;
  cli_window2_drift : REAL;
  cli_window3_drift : REAL;
  cli_window4_drift : REAL;
END_VAR
VAR
  ram1 : ACTUATOR;
  ram2 : ACTUATOR;
  ram3 : ACTUATOR;
  ram4 : ACTUATOR;
END_VAR
VAR_TEMP
  ceiling : REAL;
  aim     : REAL;
  travel  : UDINT;
  tmo     : UDINT;
  seat    : UDINT;
  owner   : USINT;
  tgt1    : REAL;
  tgt2    : REAL;
  tgt3    : REAL;
  tgt4    : REAL;
END_VAR
  (* Stage 1, decided upstream: the ROWS, never a verdict of this law's own.
     Rain and wind shut the roof whatever else is true, including the safe
     state below. *)
  ceiling := LIMIT(0.0, cli_windowCap, 1.0);
  IF prog_windows_park THEN
    (* PARKING, and it outranks everything: the roof SHUTS on the way out. A
       ram left half open is a roof nobody is watching with the weather still
       to come, and the declared position of every window row is 0 - so the
       park and the safe state agree and the rams are what close the gap. *)
    aim := 0.0;
  ELSIF cli_windowLockout THEN
    aim := 0.0;
  ELSE
    (* The cascade STOPS here. cooling_controller publishes the roof's own park
       position when it cannot see the house, so this is a real opening either
       way and the law runs its normal path on it rather than branching. *)
    aim := LIMIT(0.0, cli_windowDem, 1.0);
  END_IF;
  (* ...and the wind's ceiling over all of it: a stiff wind is a reason to open
     less, whatever the ladder asked for. *)
  IF aim > ceiling THEN aim := ceiling; END_IF;

  travel := SEC_MS(cli_windowTravel_sec);
  tmo    := SEC_MS(cli_windowTmo_sec);
  (* The safe time is the travel plus the allowance for reaching the stop, so
     the difference IS the seating run rather than a second figure to keep in
     step with the first. *)
  IF tmo > travel THEN seat := tmo - travel; ELSE seat := 2000; END_IF;

  (* ONE ram at a time. A stroke is committed: whoever is moving keeps the bus
     until it stops, or the next selection would leave a coil energised with
     nothing stepping it down. Then, PER RAM in order, whichever first has
     something to do - not knowing where it is, or being away from the target.
     The two tests belong to the same pass: asking all four the first question
     before any of them the second lets an unknown ram 3 outrank a ram 1 that
     is wide open with the roof being shut. The rest are HELD: they keep their
     clocks and decide nothing. *)
  IF   ram1.MOVING THEN owner := 1;
  ELSIF ram2.MOVING THEN owner := 2;
  ELSIF ram3.MOVING THEN owner := 3;
  ELSIF ram4.MOVING THEN owner := 4;
  ELSIF NOT ram1.KNOWN OR ABS(aim - ram1.POS) > 0.02 THEN owner := 1;
  ELSIF NOT ram2.KNOWN OR ABS(aim - ram2.POS) > 0.02 THEN owner := 2;
  ELSIF NOT ram3.KNOWN OR ABS(aim - ram3.POS) > 0.02 THEN owner := 3;
  ELSIF NOT ram4.KNOWN OR ABS(aim - ram4.POS) > 0.02 THEN owner := 4;
  ELSE owner := 0;
  END_IF;

  (* A held ram is handed its own position, so nothing it is given can ask it
     to move even if the HOLD were ever dropped by mistake. *)
  tgt1 := ram1.POS;
  tgt2 := ram2.POS;
  tgt3 := ram3.POS;
  tgt4 := ram4.POS;
  IF owner = 1 THEN tgt1 := aim; END_IF;
  IF owner = 2 THEN tgt2 := aim; END_IF;
  IF owner = 3 THEN tgt3 := aim; END_IF;
  IF owner = 4 THEN tgt4 := aim; END_IF;

  ram1.TARGET       := tgt1;
  ram1.OPEN_MS      := travel;
  ram1.CLOSE_MS     := travel;
  ram1.SEAT_MS      := seat;
  ram1.MIN_RUN_MS   := UINT_TO_UDINT(cli_windowMinRun_ms);
  ram1.REVERSE_MS   := UINT_TO_UDINT(cli_windowReverse_ms);
  ram1.DB           := cli_windowDeadband;
  ram1.REDATUM      := cli_windowRedatum;
  ram1.HOLD         := owner <> 1;
  ram1.PRESET_KNOWN := FALSE;
  ram1.PRESET_POS   := 0.0;
  ram1.NOW          := sys_now_ms;
  ram1();

  ram2.TARGET       := tgt2;
  ram2.OPEN_MS      := travel;
  ram2.CLOSE_MS     := travel;
  ram2.SEAT_MS      := seat;
  ram2.MIN_RUN_MS   := UINT_TO_UDINT(cli_windowMinRun_ms);
  ram2.REVERSE_MS   := UINT_TO_UDINT(cli_windowReverse_ms);
  ram2.DB           := cli_windowDeadband;
  ram2.REDATUM      := cli_windowRedatum;
  ram2.HOLD         := owner <> 2;
  ram2.PRESET_KNOWN := FALSE;
  ram2.PRESET_POS   := 0.0;
  ram2.NOW          := sys_now_ms;
  ram2();

  ram3.TARGET       := tgt3;
  ram3.OPEN_MS      := travel;
  ram3.CLOSE_MS     := travel;
  ram3.SEAT_MS      := seat;
  ram3.MIN_RUN_MS   := UINT_TO_UDINT(cli_windowMinRun_ms);
  ram3.REVERSE_MS   := UINT_TO_UDINT(cli_windowReverse_ms);
  ram3.DB           := cli_windowDeadband;
  ram3.REDATUM      := cli_windowRedatum;
  ram3.HOLD         := owner <> 3;
  ram3.PRESET_KNOWN := FALSE;
  ram3.PRESET_POS   := 0.0;
  ram3.NOW          := sys_now_ms;
  ram3();

  ram4.TARGET       := tgt4;
  ram4.OPEN_MS      := travel;
  ram4.CLOSE_MS     := travel;
  ram4.SEAT_MS      := seat;
  ram4.MIN_RUN_MS   := UINT_TO_UDINT(cli_windowMinRun_ms);
  ram4.REVERSE_MS   := UINT_TO_UDINT(cli_windowReverse_ms);
  ram4.DB           := cli_windowDeadband;
  ram4.REDATUM      := cli_windowRedatum;
  ram4.HOLD         := owner <> 4;
  ram4.PRESET_KNOWN := FALSE;
  ram4.PRESET_POS   := 0.0;
  ram4.NOW          := sys_now_ms;
  ram4();

  cli_window1_opn := ram1.OPEN_CMD;
  cli_window1_clo := ram1.CLOSE_CMD;
  cli_window2_opn := ram2.OPEN_CMD;
  cli_window2_clo := ram2.CLOSE_CMD;
  cli_window3_opn := ram3.OPEN_CMD;
  cli_window3_clo := ram3.CLOSE_CMD;
  cli_window4_opn := ram4.OPEN_CMD;
  cli_window4_clo := ram4.CLOSE_CMD;

  (* One target for all four - they are one roof - against each ram's own
     position estimate, which is what says how far behind the sequence is. A
     ram that does not know where it is publishes nothing rather than a zero. *)
  cli_window1_tgt := aim;
  cli_window2_tgt := aim;
  cli_window3_tgt := aim;
  cli_window4_tgt := aim;
  (* Travel since each ram last reached a stop. No ram reports a fault - they
     have limit switches and no contact back - so a drift that climbs while its
     three siblings reset is the only sign of a linkage going. *)
  cli_window1_drift := ram1.DRIFT;
  cli_window2_drift := ram2.DRIFT;
  cli_window3_drift := ram3.DRIFT;
  cli_window4_drift := ram4.DRIFT;
  IF ram1.KNOWN THEN cli_window1_pos := ram1.POS; END_IF;
  IF ram2.KNOWN THEN cli_window2_pos := ram2.POS; END_IF;
  IF ram3.KNOWN THEN cli_window3_pos := ram3.POS; END_IF;
  IF ram4.KNOWN THEN cli_window4_pos := ram4.POS; END_IF;

  (* PARKED is `owner = 0`, and that is not a shortcut: the selection above
     hands the bus to any ram that is moving, does not know where it is, or is
     away from the target - so nobody owning it means all four know where they
     are and are all at `aim`, which while parking is shut. *)
  prog_windows_parked := prog_windows_park AND owner = 0;
END_PROGRAM
