(* irr_mtr_input program
   ------------------------------------------------------------------------ *)

PROGRAM irr_mtr_input
VAR_EXTERNAL CONSTANT
  irr_inCount         : UDINT;
  irr_input_qtyPerImp : REAL;
  irr_rateWin_ms      : UINT;
  irr_leak_Lpm        : REAL;
  irr_minFill_Lpm     : REAL;
  irr_state           : USINT;
  sys_now_ms          : UDINT;
END_VAR
VAR_EXTERNAL
  irr_inTotal_L        : REAL;
  irr_inFlow_Lpm       : REAL;
  irr_inPulse          : BOOL;
  alarm_water_leak     : BOOL;
  alarm_water_pressure : BOOL;
END_VAR
VAR
  rate     : DERIVATIVE;
  idle_for : TON_MS;
  fill_for : TON_MS;
  seeded   : BOOL := FALSE;
  base_L   : REAL;
  base_cnt : UDINT;
  last_cnt : UDINT;
END_VAR
VAR_TEMP
  total : REAL;
  flow  : REAL;
  win   : UDINT;
  idle  : BOOL;
  fill  : BOOL;
END_VAR
  (* Adopt the counter where it stands rather than starting from zero: the
     RETAINED row carries what was metered before this run, and the pulse
     counter carries what has been metered since the last boot. *)
  IF NOT seeded THEN
    base_L   := irr_inTotal_L;
    base_cnt := irr_inCount;
    last_cnt := irr_inCount;
    seeded   := TRUE;
  END_IF;

  (* Unsigned difference, so the 32-bit counter wrapping costs nothing. *)
  total := base_L
         + UDINT_TO_REAL(irr_inCount - base_cnt) * irr_input_qtyPerImp;

  (* Whether the line is ticking, which is the only thing a pulse terminal can
     say about itself: its LEVEL is a moment of a train. Written every beat, so
     the row reads as a live terminal rather than one nothing has published. *)
  irr_inPulse := irr_inCount <> last_cnt;
  last_cnt    := irr_inCount;

  irr_inTotal_L := total;
  rate.QUANTITY  := total;
  rate.WINDOW_MS := UINT_TO_UDINT(irr_rateWin_ms);
  rate.PRESET    := FALSE;
  rate.NOW       := sys_now_ms;
  rate();
  flow           := 60.0 * rate.RATE;
  irr_inFlow_Lpm := flow;

  (* The two alarms below are gated on the cycle's state: without it they would
     be judged against IDLE, which is what a leak is measured from.

     TWO windows, not one. The rate is the last COMPLETED window and stands
     until the next one closes, and the window boundaries have nothing to do
     with when the state changed: a window straddling the change closes with
     the old state's water still in it, and that figure then stands for a
     window more. Until two have passed, the rate can still be reporting the
     state before - which is how the flush's 60 L/min read as a leak the moment
     the cycle went idle, and idle's zero read as a pressure fault the moment a
     fill opened. Both fired on every cycle the plant ran.
     0 IDLE, 1 FILLING. *)
  win := 2 * UINT_TO_UDINT(irr_rateWin_ms);
  idle_for(IN := irr_state = 0, PT := win, NOW := sys_now_ms);
  fill_for(IN := irr_state = 1, PT := win, NOW := sys_now_ms);
  idle := idle_for.Q;
  fill := fill_for.Q;

  (* Idle means every valve in the circuit is shut, so anything the inlet meter
     counts is going somewhere nobody opened. *)
  alarm_water_leak := idle AND flow > irr_leak_Lpm;
  (* The mirror image: the inlet is open and the water is not arriving. *)
  alarm_water_pressure := fill AND flow < irr_minFill_Lpm;
END_PROGRAM
