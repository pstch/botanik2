(* irrigation_schedule, as ST. Mirrors
   src/sites/gh_mlb41/programs/irrigation/irrigation_schedule/. *)

PROGRAM irrigation_schedule
VAR_EXTERNAL CONSTANT
  irr_mod            : REAL;
  irr_volFreqSplit   : REAL;
  irr_traysPerDay    : UINT;
  cli_temp_ext       : REAL;
  cli_temp_ext_qlt   : BOOL;
  irr_dawnGate_degC  : REAL;
  cli_phase          : USINT;
  cli_phase_qlt      : BOOL;
  sol_ready          : BOOL;
  sol_fromRise_min   : INT;
  sol_dayRise_min    : INT;
  irr_morningWin_min : UINT;
  sys_today          : UDINT;
  irr_days_tray1     : UINT;
  irr_days_tray2     : UINT;
  irr_days_tray3     : UINT;
  irr_days_tray4     : UINT;
  irr_days_tray5     : UINT;
  irr_days_tray6     : UINT;
  irr_days_tray7     : UINT;
  irr_days_tray8     : UINT;
END_VAR
VAR_EXTERNAL
  irr_dueTray        : USINT;
  irr_slotOpen       : BOOL;
  irr_period_days    : REAL;
  irr_today          : UDINT;
  irr_servedToday    : USINT;
  irr_forceNow       : BOOL;
  irr_last_tray1     : UDINT;
  irr_last_tray2     : UDINT;
  irr_last_tray3     : UDINT;
  irr_last_tray4     : UDINT;
  irr_last_tray5     : UDINT;
  irr_last_tray6     : UDINT;
  irr_last_tray7     : UDINT;
  irr_last_tray8     : UDINT;
  irr_forceOn_tray1  : BOOL;
  irr_forceOn_tray2  : BOOL;
  irr_forceOn_tray3  : BOOL;
  irr_forceOn_tray4  : BOOL;
  irr_forceOn_tray5  : BOOL;
  irr_forceOn_tray6  : BOOL;
  irr_forceOn_tray7  : BOOL;
  irr_forceOn_tray8  : BOOL;
  irr_forceOff_tray1 : BOOL;
  irr_forceOff_tray2 : BOOL;
  irr_forceOff_tray3 : BOOL;
  irr_forceOff_tray4 : BOOL;
  irr_forceOff_tray5 : BOOL;
  irr_forceOff_tray6 : BOOL;
  irr_forceOff_tray7 : BOOL;
  irr_forceOff_tray8 : BOOL;
END_VAR
VAR
  t1 : TRAY_DUE;
  t2 : TRAY_DUE;
  t3 : TRAY_DUE;
  t4 : TRAY_DUE;
  t5 : TRAY_DUE;
  t6 : TRAY_DUE;
  t7 : TRAY_DUE;
  t8 : TRAY_DUE;
  served_day : UDINT;
END_VAR
VAR_TEMP
  forced    : BOOL;
  open_slot : BOOL;
  split_lim : REAL;
  stretch   : REAL;
  shortest  : UINT;
  best      : USINT;
  worst     : UDINT;
  picked    : BOOL;
END_VAR
  (* The daily tally is against a DATE, not an uptime: it rolls when the day
     does, so a cycle running through midnight is not counted twice and a
     reboot at noon does not hand the plant a second allowance. 0 is "the RTC
     has never been set", which the row carries as itself rather than as a
     date. *)
  irr_today := sys_today;
  IF sys_today <> 0 AND sys_today <> served_day THEN
    served_day      := sys_today;
    irr_servedToday := 0;
  END_IF;

  forced := irr_forceNow;

  (* WHICH slot. END OF NIGHT, and only when the plants are not cold: watering
     cold roots is what the gate exists to prevent, and the morning slot is
     where that watering goes instead. Never the afternoon - a tray filled then
     is still wet at dusk.
     1 COLD_BANK, 2 DAWN_BREEZE, 3 DAY. *)
  IF cli_phase = 1 OR cli_phase = 2 THEN
    open_slot := cli_temp_ext >= irr_dawnGate_degC;
  ELSE
    (* START OF THE DAY, for a window an operator sets: the tray has to drain
       and dry before the stomata open at dusk. *)
    open_slot := cli_phase = 3
             AND sol_fromRise_min <= sol_dayRise_min + UINT_TO_INT(irr_morningWin_min);
  END_IF;
  irr_slotOpen := open_slot;

  (* A STARTING value, so the row does not read as never-written for the whole
     of bring-up: the shortest summer interval is what the plant runs on at
     full modulation, and it is overwritten the moment a tray is picked. *)
  shortest := 0;
  IF irr_days_tray1 > 0 AND (shortest = 0 OR irr_days_tray1 < shortest)
  THEN shortest := irr_days_tray1; END_IF;
  IF irr_days_tray2 > 0 AND (shortest = 0 OR irr_days_tray2 < shortest)
  THEN shortest := irr_days_tray2; END_IF;
  IF irr_days_tray3 > 0 AND (shortest = 0 OR irr_days_tray3 < shortest)
  THEN shortest := irr_days_tray3; END_IF;
  IF irr_days_tray4 > 0 AND (shortest = 0 OR irr_days_tray4 < shortest)
  THEN shortest := irr_days_tray4; END_IF;
  IF irr_days_tray5 > 0 AND (shortest = 0 OR irr_days_tray5 < shortest)
  THEN shortest := irr_days_tray5; END_IF;
  IF irr_days_tray6 > 0 AND (shortest = 0 OR irr_days_tray6 < shortest)
  THEN shortest := irr_days_tray6; END_IF;
  IF irr_days_tray7 > 0 AND (shortest = 0 OR irr_days_tray7 < shortest)
  THEN shortest := irr_days_tray7; END_IF;
  IF irr_days_tray8 > 0 AND (shortest = 0 OR irr_days_tray8 < shortest)
  THEN shortest := irr_days_tray8; END_IF;
  IF shortest > 0 THEN irr_period_days := UINT_TO_REAL(shortest); END_IF;

  (* period = summer / mod^(1 - split) and volume = summer * mod^split, so
     their RATIO - the water actually delivered per day - is proportional to
     the modulation whatever the split is set to. *)
  split_lim := LIMIT(0.0, irr_volFreqSplit, 1.0);
  stretch   := POW(IN1 := irr_mod, IN2 := 1.0 - split_lim);

  t1(DAYS := irr_days_tray1, LAST := irr_last_tray1,
     FORCE_OFF := irr_forceOff_tray1, DAY := sys_today,
     STRETCH := stretch);
  t2(DAYS := irr_days_tray2, LAST := irr_last_tray2,
     FORCE_OFF := irr_forceOff_tray2, DAY := sys_today,
     STRETCH := stretch);
  t3(DAYS := irr_days_tray3, LAST := irr_last_tray3,
     FORCE_OFF := irr_forceOff_tray3, DAY := sys_today,
     STRETCH := stretch);
  t4(DAYS := irr_days_tray4, LAST := irr_last_tray4,
     FORCE_OFF := irr_forceOff_tray4, DAY := sys_today,
     STRETCH := stretch);
  t5(DAYS := irr_days_tray5, LAST := irr_last_tray5,
     FORCE_OFF := irr_forceOff_tray5, DAY := sys_today,
     STRETCH := stretch);
  t6(DAYS := irr_days_tray6, LAST := irr_last_tray6,
     FORCE_OFF := irr_forceOff_tray6, DAY := sys_today,
     STRETCH := stretch);
  t7(DAYS := irr_days_tray7, LAST := irr_last_tray7,
     FORCE_OFF := irr_forceOff_tray7, DAY := sys_today,
     STRETCH := stretch);
  t8(DAYS := irr_days_tray8, LAST := irr_last_tray8,
     FORCE_OFF := irr_forceOff_tray8, DAY := sys_today,
     STRETCH := stretch);

  (* A skip is SPENT and the tray stamped as if it had been watered, so it goes
     to the back of the queue. Both are EDITS: the rows are retained parameters
     and a plain drive would leave the store owing them. *)
  IF t1.SKIP THEN irr_forceOff_tray1 := FALSE;
                  irr_last_tray1 := sys_today; END_IF;
  IF t2.SKIP THEN irr_forceOff_tray2 := FALSE;
                  irr_last_tray2 := sys_today; END_IF;
  IF t3.SKIP THEN irr_forceOff_tray3 := FALSE;
                  irr_last_tray3 := sys_today; END_IF;
  IF t4.SKIP THEN irr_forceOff_tray4 := FALSE;
                  irr_last_tray4 := sys_today; END_IF;
  IF t5.SKIP THEN irr_forceOff_tray5 := FALSE;
                  irr_last_tray5 := sys_today; END_IF;
  IF t6.SKIP THEN irr_forceOff_tray6 := FALSE;
                  irr_last_tray6 := sys_today; END_IF;
  IF t7.SKIP THEN irr_forceOff_tray7 := FALSE;
                  irr_last_tray7 := sys_today; END_IF;
  IF t8.SKIP THEN irr_forceOff_tray8 := FALSE;
                  irr_last_tray8 := sys_today; END_IF;

  (* WHEN, before which tray: the climate decides the slot and the schedule
     decides the tray, and an operator forcing a cycle overrides both.

     A modulation nobody can read does NOT hold the plant: it reads as the
     declared one, the tray is picked against that, and irr_volume_L's quality
     is what says the interval was a guess. A tray left dry because the weather
     stopped being legible is the worse outcome. *)
  IF (USINT_TO_UINT(irr_servedToday) >= irr_traysPerDay AND NOT forced)
     OR (NOT forced AND NOT open_slot) THEN
    (* Nothing owed is a definite answer, whatever the weather is doing. *)
    irr_dueTray := 0;
  ELSE
    best   := 0;
    worst  := 0;
    picked := FALSE;
    IF irr_forceOn_tray1 THEN best := 1; picked := TRUE;
    ELSIF irr_forceOn_tray2 THEN best := 2; picked := TRUE;
    ELSIF irr_forceOn_tray3 THEN best := 3; picked := TRUE;
    ELSIF irr_forceOn_tray4 THEN best := 4; picked := TRUE;
    ELSIF irr_forceOn_tray5 THEN best := 5; picked := TRUE;
    ELSIF irr_forceOn_tray6 THEN best := 6; picked := TRUE;
    ELSIF irr_forceOn_tray7 THEN best := 7; picked := TRUE;
    ELSIF irr_forceOn_tray8 THEN best := 8; picked := TRUE;
    END_IF;

    (* The most overdue first, so a backlog drains in the order it built up
       rather than by tray number. The interval that CHOSE it is published:
       the summer figure is on the PARAMS page and this is what the weather
       made of it. *)
    IF NOT picked THEN
      IF t1.DUE AND (best = 0 OR t1.OVERDUE > worst) THEN
        best := 1; worst := t1.OVERDUE; irr_period_days := t1.INTERVAL;
      END_IF;
      IF t2.DUE AND (best = 0 OR t2.OVERDUE > worst) THEN
        best := 2; worst := t2.OVERDUE; irr_period_days := t2.INTERVAL;
      END_IF;
      IF t3.DUE AND (best = 0 OR t3.OVERDUE > worst) THEN
        best := 3; worst := t3.OVERDUE; irr_period_days := t3.INTERVAL;
      END_IF;
      IF t4.DUE AND (best = 0 OR t4.OVERDUE > worst) THEN
        best := 4; worst := t4.OVERDUE; irr_period_days := t4.INTERVAL;
      END_IF;
      IF t5.DUE AND (best = 0 OR t5.OVERDUE > worst) THEN
        best := 5; worst := t5.OVERDUE; irr_period_days := t5.INTERVAL;
      END_IF;
      IF t6.DUE AND (best = 0 OR t6.OVERDUE > worst) THEN
        best := 6; worst := t6.OVERDUE; irr_period_days := t6.INTERVAL;
      END_IF;
      IF t7.DUE AND (best = 0 OR t7.OVERDUE > worst) THEN
        best := 7; worst := t7.OVERDUE; irr_period_days := t7.INTERVAL;
      END_IF;
      IF t8.DUE AND (best = 0 OR t8.OVERDUE > worst) THEN
        best := 8; worst := t8.OVERDUE; irr_period_days := t8.INTERVAL;
      END_IF;
    END_IF;
    (* GOOD, even on a guessed modulation. This row's declared value is 0 - "no
       tray owed" - so marking it doubtful would hand the cycle a refusal
       rather than a doubt, and the tray it just picked would go unwatered. *)
    irr_dueTray := best;
  END_IF;
END_PROGRAM
