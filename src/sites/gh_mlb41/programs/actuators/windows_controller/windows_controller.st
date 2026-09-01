(* windows_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/actuators/windows_controller/. *)

PROGRAM windows_controller
VAR_EXTERNAL CONSTANT
  cli_windowDem        : REAL;
  cli_windowLockout    : BOOL;
  cli_windowCap        : REAL;
  cli_windowTravel_sec : UINT;
  cli_windowTmo_sec    : UINT;
  sys_now_ms           : UDINT;
END_VAR
VAR_EXTERNAL
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
END_VAR
VAR
  a1 : ACTUATOR;
  a2 : ACTUATOR;
  a3 : ACTUATOR;
  a4 : ACTUATOR;
END_VAR
VAR_TEMP
  ceiling : REAL;
  aim     : REAL;
  travel  : UDINT;
  tmo     : UDINT;
  seat    : UDINT;
  owner   : USINT;
  t1      : REAL;
  t2      : REAL;
  t3      : REAL;
  t4      : REAL;
END_VAR
  (* Stage 1, decided upstream: the ROWS, never a verdict of this law's own.
     Rain and wind shut the roof whatever else is true, including the safe
     state below. *)
  ceiling := LIMIT(0.0, cli_windowCap, 1.0);
  IF cli_windowLockout THEN
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
     is wide open with the roof being shut. The rest are HELD - they keep their
     clocks and decide nothing, which is what "not stepped" meant in C++. *)
  IF   a1.MOVING THEN owner := 1;
  ELSIF a2.MOVING THEN owner := 2;
  ELSIF a3.MOVING THEN owner := 3;
  ELSIF a4.MOVING THEN owner := 4;
  ELSIF NOT a1.KNOWN OR ABS(aim - a1.POS) > 0.02 THEN owner := 1;
  ELSIF NOT a2.KNOWN OR ABS(aim - a2.POS) > 0.02 THEN owner := 2;
  ELSIF NOT a3.KNOWN OR ABS(aim - a3.POS) > 0.02 THEN owner := 3;
  ELSIF NOT a4.KNOWN OR ABS(aim - a4.POS) > 0.02 THEN owner := 4;
  ELSE owner := 0;
  END_IF;

  (* A held ram is handed its own position, so nothing it is given can ask it
     to move even if the HOLD were ever dropped by mistake. *)
  t1 := a1.POS;
  t2 := a2.POS;
  t3 := a3.POS;
  t4 := a4.POS;
  IF owner = 1 THEN t1 := aim; END_IF;
  IF owner = 2 THEN t2 := aim; END_IF;
  IF owner = 3 THEN t3 := aim; END_IF;
  IF owner = 4 THEN t4 := aim; END_IF;

  a1(TARGET       := t1,
     OPEN_MS      := travel,
     CLOSE_MS     := travel,
     SEAT_MS      := seat,
     MIN_RUN_MS   := 250,
     REVERSE_MS   := 500,
     DB           := 0.02,
     REDATUM      := 3.0,
     HOLD         := owner <> 1,
     PRESET_KNOWN := FALSE,
     PRESET_POS   := 0.0,
     NOW          := sys_now_ms);

  a2(TARGET       := t2,
     OPEN_MS      := travel,
     CLOSE_MS     := travel,
     SEAT_MS      := seat,
     MIN_RUN_MS   := 250,
     REVERSE_MS   := 500,
     DB           := 0.02,
     REDATUM      := 3.0,
     HOLD         := owner <> 2,
     PRESET_KNOWN := FALSE,
     PRESET_POS   := 0.0,
     NOW          := sys_now_ms);

  a3(TARGET       := t3,
     OPEN_MS      := travel,
     CLOSE_MS     := travel,
     SEAT_MS      := seat,
     MIN_RUN_MS   := 250,
     REVERSE_MS   := 500,
     DB           := 0.02,
     REDATUM      := 3.0,
     HOLD         := owner <> 3,
     PRESET_KNOWN := FALSE,
     PRESET_POS   := 0.0,
     NOW          := sys_now_ms);

  a4(TARGET       := t4,
     OPEN_MS      := travel,
     CLOSE_MS     := travel,
     SEAT_MS      := seat,
     MIN_RUN_MS   := 250,
     REVERSE_MS   := 500,
     DB           := 0.02,
     REDATUM      := 3.0,
     HOLD         := owner <> 4,
     PRESET_KNOWN := FALSE,
     PRESET_POS   := 0.0,
     NOW          := sys_now_ms);

  cli_window1_opn := a1.OPEN_CMD;
  cli_window1_clo := a1.CLOSE_CMD;
  cli_window2_opn := a2.OPEN_CMD;
  cli_window2_clo := a2.CLOSE_CMD;
  cli_window3_opn := a3.OPEN_CMD;
  cli_window3_clo := a3.CLOSE_CMD;
  cli_window4_opn := a4.OPEN_CMD;
  cli_window4_clo := a4.CLOSE_CMD;

  (* One target for all four - they are one roof - against each ram's own
     position estimate, which is what says how far behind the sequence is. A
     ram that does not know where it is publishes nothing rather than a zero. *)
  cli_window1_tgt := aim;
  cli_window2_tgt := aim;
  cli_window3_tgt := aim;
  cli_window4_tgt := aim;
  IF a1.KNOWN THEN cli_window1_pos := a1.POS; END_IF;
  IF a2.KNOWN THEN cli_window2_pos := a2.POS; END_IF;
  IF a3.KNOWN THEN cli_window3_pos := a3.POS; END_IF;
  IF a4.KNOWN THEN cli_window4_pos := a4.POS; END_IF;
END_PROGRAM
