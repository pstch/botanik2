(* irrigation_cycle program
   ------------------------------------------------------------------------ *)

PROGRAM irrigation_cycle
VAR_EXTERNAL CONSTANT
  irr_today             : UDINT;
  irr_dueTray         : USINT;
  irr_mod             : REAL;
  irr_mod_qlt         : QUALITY;
  irr_inTotal_L       : REAL;
  irr_outTotal_L      : REAL;
  irr_outFlow_Lpm_qlt : QUALITY;
  irr_volFreqSplit    : REAL;
  irr_maxFill_sec     : UINT;
  irr_soak_sec        : UINT;
  irr_drainWindow_sec : UINT;
  irr_flush_sec       : UINT;
  irr_expDrainFrac    : REAL;
  irr_drainMargin     : REAL;
  sys_now_ms          : UDINT;
  irr_liters_tray1    : UINT;
  irr_liters_tray2    : UINT;
  irr_liters_tray3    : UINT;
  irr_liters_tray4    : UINT;
  irr_liters_tray5    : UINT;
  irr_liters_tray6    : UINT;
  irr_liters_tray7    : UINT;
  irr_liters_tray8    : UINT;
END_VAR
VAR_EXTERNAL
  irr_volume_L          : REAL;
  irr_state             : IrrState;
  irr_activeTray        : USINT;
  irr_delivered_L       : REAL;
  irr_drained_L         : REAL;
  irr_servedToday       : USINT;
  irr_forceNow          : BOOL;
  alarm_irr_fill        : BOOL;
  alarm_irr_drain       : BOOL;
  irr_last_tray1        : UDINT;
  irr_last_tray2        : UDINT;
  irr_last_tray3        : UDINT;
  irr_last_tray4        : UDINT;
  irr_last_tray5        : UDINT;
  irr_last_tray6        : UDINT;
  irr_last_tray7        : UDINT;
  irr_last_tray8        : UDINT;
  irr_forceOn_tray1     : BOOL;
  irr_forceOn_tray2     : BOOL;
  irr_forceOn_tray3     : BOOL;
  irr_forceOn_tray4     : BOOL;
  irr_forceOn_tray5     : BOOL;
  irr_forceOn_tray6     : BOOL;
  irr_forceOn_tray7     : BOOL;
  irr_forceOn_tray8     : BOOL;
  alarm_irr_fill_tray1  : BOOL;
  alarm_irr_fill_tray2  : BOOL;
  alarm_irr_fill_tray3  : BOOL;
  alarm_irr_fill_tray4  : BOOL;
  alarm_irr_fill_tray5  : BOOL;
  alarm_irr_fill_tray6  : BOOL;
  alarm_irr_fill_tray7  : BOOL;
  alarm_irr_fill_tray8  : BOOL;
  alarm_irr_drain_tray1 : BOOL;
  alarm_irr_drain_tray2 : BOOL;
  alarm_irr_drain_tray3 : BOOL;
  alarm_irr_drain_tray4 : BOOL;
  alarm_irr_drain_tray5 : BOOL;
  alarm_irr_drain_tray6 : BOOL;
  alarm_irr_drain_tray7 : BOOL;
  alarm_irr_drain_tray8 : BOOL;
END_VAR
VAR
  dwell    : TON_MS;
  tray     : USINT;
  mode     : IrrState := IRR_IDLE;
  forced   : BOOL;
  fill_at  : REAL;    (* the inlet counter when this fill started *)
  drain_at : REAL;    (* ...and the outlet counter when the drain did *)
  dose_L   : REAL;    (* what actually went in: the drain is judged on it *)
END_VAR
VAR_TEMP
  split_lim    : REAL;
  dose         : REAL;
  litres       : REAL;
  delivered_L  : REAL;
  dwell_ms     : UDINT;
  expired      : BOOL;
  moved        : BOOL;
  served       : BOOL;   (* the fill was judged: spend the forces and stamp *)
  blame        : BOOL;
  frac         : REAL;
  min_frac     : REAL;
  clog         : BOOL;
  judged       : BOOL;   (* the drain was judged *)
  served_today : USINT;
END_VAR
  moved  := FALSE;
  served := FALSE;
  judged := FALSE;
  blame  := FALSE;
  clog   := FALSE;

  (* The dwell for whichever state is in force, TICKED before the machine reads
     it and zeroed again below if the state moves. *)
  IF    mode = IRR_FILLING  THEN dwell_ms := SEC_MS(irr_maxFill_sec);
  ELSIF mode = IRR_SOAKING  THEN dwell_ms := SEC_MS(irr_soak_sec);
  ELSIF mode = IRR_DRAINING THEN dwell_ms := SEC_MS(irr_drainWindow_sec);
  ELSIF mode = IRR_FLUSHING THEN dwell_ms := SEC_MS(irr_flush_sec);
  ELSE dwell_ms := UDINT#0;
  END_IF;
  dwell(IN := mode <> IRR_IDLE, PT := dwell_ms, NOW := sys_now_ms);
  expired := dwell.Q;

  (* The dose is the tray's summer figure scaled by the modulation. One nobody
     has reads as the DECLARED one, so there is always a dose - the cycle runs
     on it, and irr_volume_L's companion is what says it was a guess. *)
  split_lim := LIMIT(0.0, irr_volFreqSplit, 1.0);
  litres    := 0.0;

  IF    tray = 1 THEN litres := UINT_TO_REAL(irr_liters_tray1);
  ELSIF tray = 2 THEN litres := UINT_TO_REAL(irr_liters_tray2);
  ELSIF tray = 3 THEN litres := UINT_TO_REAL(irr_liters_tray3);
  ELSIF tray = 4 THEN litres := UINT_TO_REAL(irr_liters_tray4);
  ELSIF tray = 5 THEN litres := UINT_TO_REAL(irr_liters_tray5);
  ELSIF tray = 6 THEN litres := UINT_TO_REAL(irr_liters_tray6);
  ELSIF tray = 7 THEN litres := UINT_TO_REAL(irr_liters_tray7);
  ELSIF tray = 8 THEN litres := UINT_TO_REAL(irr_liters_tray8);
  END_IF;

  IF tray > 0 THEN 
    dose := litres * POW(IN1 := irr_mod, IN2 := split_lim);
  ELSE 
    dose := 0.0;
  END_IF;

  irr_volume_L := dose;

  IF mode = IRR_IDLE THEN
    irr_delivered_L := 0.0;
    irr_drained_L   := 0.0;
    (* WHICH tray, and whether now: irr_schedule has decided both. A
       modulation nobody could measure does NOT hold the cycle - a tray that
       goes unwatered because the weather stopped being legible is a worse
       outcome than one watered to a fair guess. *)
    IF irr_dueTray <> 0 THEN
      tray   := irr_dueTray;
      forced := irr_forceNow;
      mode   := IRR_FILLING;
      moved  := TRUE;
    END_IF;

  ELSIF mode = IRR_FILLING THEN
    irr_delivered_L := irr_inTotal_L - fill_at;
    (* ...and read BACK, so the fill ends on the row an operator can watch and
       force rather than on a local difference. *)
    delivered_L := irr_delivered_L;
    IF delivered_L >= dose AND dose > 0.0 THEN
      alarm_irr_fill := FALSE;
      blame  := FALSE;
      served := TRUE;
      dose_L := delivered_L;
      (* A soak of zero is SKIPPED, not passed through for one activation: a
         state the field bus sees flicker is a state somebody has to explain. *)
      IF irr_soak_sec > 0 THEN mode := IRR_SOAKING;
      ELSE mode := IRR_DRAINING; END_IF;
      moved := TRUE;
    ELSIF expired THEN
      (* The dose never arrived: water off, a stuck inlet, or a meter that has
         stopped counting. Move to the drain regardless - leaving the inlet
         open on a fault is how a tray overflows - and soak or no soak, since
         whatever is in this tray got there by accident.
         SPENT exactly as a fill that arrived: an attempt that failed is still
         an attempt, and leaving the force standing and the tray unstamped puts
         the schedule straight back on it, refilling every slot until the
         window closes. alarm_irr_fill is how that gets read instead. *)
      alarm_irr_fill := TRUE;
      blame  := TRUE;
      served := TRUE;
      dose_L := delivered_L;
      mode   := IRR_DRAINING;
      moved  := TRUE;
    END_IF;

  ELSIF mode = IRR_SOAKING THEN
    (* The tray sits full. Nothing is measured here and nothing can go wrong:
       the fill has been judged and the drain window has not started. *)
    IF expired THEN mode := IRR_DRAINING; moved := TRUE; END_IF;

  ELSIF mode = IRR_DRAINING THEN
    irr_drained_L := irr_outTotal_L - drain_at;
    IF expired THEN
      judged := TRUE;
      (* A fill that never delivered, or a meter that is not answering. Both
         are already alarms of their own; a clog raised on top would send
         somebody to the wrong end of the plant. *)
      IF dose_L > 0.0 AND GOOD(irr_outFlow_Lpm_qlt) THEN
        (* Against the DOSE, not a fixed volume: a modulated half-dose drains
           half as much and is not a clog. The published row, not the
           difference behind it - a verdict made on a number nobody can see is
           a verdict nobody can rehearse. *)
        frac     := irr_drained_L / dose_L;
        min_frac := irr_expDrainFrac - irr_drainMargin;
        clog     := frac < min_frac;
      END_IF;
      alarm_irr_drain := clog;
    END_IF;

  ELSE
    IF expired THEN mode := IRR_IDLE; moved := TRUE; END_IF;
  END_IF;

  (* The per-tray alarms, while `tray` still says which one. The any-form
     beside them is what an operator sees; this is what still names the tray
     once the cycle has moved on to the next. *)
  IF served AND tray = 1 THEN alarm_irr_fill_tray1 := blame; END_IF;
  IF served AND tray = 2 THEN alarm_irr_fill_tray2 := blame; END_IF;
  IF served AND tray = 3 THEN alarm_irr_fill_tray3 := blame; END_IF;
  IF served AND tray = 4 THEN alarm_irr_fill_tray4 := blame; END_IF;
  IF served AND tray = 5 THEN alarm_irr_fill_tray5 := blame; END_IF;
  IF served AND tray = 6 THEN alarm_irr_fill_tray6 := blame; END_IF;
  IF served AND tray = 7 THEN alarm_irr_fill_tray7 := blame; END_IF;
  IF served AND tray = 8 THEN alarm_irr_fill_tray8 := blame; END_IF;

  IF judged AND tray = 1 THEN alarm_irr_drain_tray1 := clog; END_IF;
  IF judged AND tray = 2 THEN alarm_irr_drain_tray2 := clog; END_IF;
  IF judged AND tray = 3 THEN alarm_irr_drain_tray3 := clog; END_IF;
  IF judged AND tray = 4 THEN alarm_irr_drain_tray4 := clog; END_IF;
  IF judged AND tray = 5 THEN alarm_irr_drain_tray5 := clog; END_IF;
  IF judged AND tray = 6 THEN alarm_irr_drain_tray6 := clog; END_IF;
  IF judged AND tray = 7 THEN alarm_irr_drain_tray7 := clog; END_IF;
  IF judged AND tray = 8 THEN alarm_irr_drain_tray8 := clog; END_IF;

  (* Both forces are spent on the watering they asked for, and spent WHATEVER
     the clock says: left standing, one asks for the same tray on every cycle
     and the other for a cycle at any hour, for ever, and no other tray is ever
     reached. Before NTP there is no date to stamp, and the day's tally is then
     the only thing bounding the day, so neither may wait on one.

     The stamp is AFTER THE FILL, not after the drain: the tray has had its
     water by then, and a fault or a restart during the long drain must not
     water it twice. A forced cycle stamps too - it IS a watering. *)
  IF served THEN
    IF tray = 1 THEN
      irr_forceOn_tray1 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray1 := irr_today; END_IF;
    END_IF;
    IF tray = 2 THEN
      irr_forceOn_tray2 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray2 := irr_today; END_IF;
    END_IF;
    IF tray = 3 THEN
      irr_forceOn_tray3 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray3 := irr_today; END_IF;
    END_IF;
    IF tray = 4 THEN
      irr_forceOn_tray4 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray4 := irr_today; END_IF;
    END_IF;
    IF tray = 5 THEN
      irr_forceOn_tray5 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray5 := irr_today; END_IF;
    END_IF;
    IF tray = 6 THEN
      irr_forceOn_tray6 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray6 := irr_today; END_IF;
    END_IF;
    IF tray = 7 THEN
      irr_forceOn_tray7 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray7 := irr_today; END_IF;
    END_IF;
    IF tray = 8 THEN
      irr_forceOn_tray8 := FALSE;
      IF irr_today <> 0 THEN irr_last_tray8 := irr_today; END_IF;
    END_IF;
    IF forced THEN
      irr_forceNow := FALSE;
      forced       := FALSE;
    END_IF;
    (* The day's allowance, counted by the schedule and spent here. *)
    served_today := irr_servedToday;
    IF served_today < 255 THEN irr_servedToday := served_today + 1; END_IF;
  END_IF;

  (* The drain ends the cycle, and only after its verdict has named the tray. *)
  IF judged THEN
    tray  := 0;
    mode  := IRR_FLUSHING;
    moved := TRUE;
  END_IF;

  (* Entering a state stamps the counter the next one is judged on: a meter is
     a running total, so a window is a DIFFERENCE between two readings of it. *)
  IF moved THEN
    IF mode = IRR_FILLING THEN fill_at := irr_inTotal_L; END_IF;
    IF mode = IRR_DRAINING THEN
      drain_at      := irr_outTotal_L;
      irr_drained_L := 0.0;
    END_IF;
    (* ...and the dwell starts again with the state it belongs to. *)
    dwell(IN := FALSE, PT := dwell_ms, NOW := sys_now_ms);
  END_IF;

  irr_state      := mode;
  irr_activeTray := tray;
END_PROGRAM
