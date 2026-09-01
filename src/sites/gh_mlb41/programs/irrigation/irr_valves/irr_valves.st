(* irr_valves program
   ------------------------------------------------------------------------ *)

PROGRAM irr_valves
VAR_EXTERNAL CONSTANT
  irr_state      : USINT;
  irr_activeTray : USINT;
END_VAR
VAR_EXTERNAL
  irr_input_ro1  : BOOL;
  irr_input_ro2  : BOOL;
  irr_output_ro1 : BOOL;
  irr_output_ro2 : BOOL;
  irr_tray1      : BOOL;
  irr_tray2      : BOOL;
  irr_tray3      : BOOL;
  irr_tray4      : BOOL;
  irr_tray5      : BOOL;
  irr_tray6      : BOOL;
  irr_tray7      : BOOL;
  irr_tray8      : BOOL;
END_VAR
VAR_TEMP
  filling  : BOOL;
  draining : BOOL;
  flushing : BOOL;
  soaking  : BOOL;
  onto     : BOOL;   (* a tray is connected to the manifold *)
END_VAR
  (* 0 IDLE, 1 FILLING, 2 DRAINING, 3 FLUSHING, 4 SOAKING. Both inputs default
     to IDLE and no tray, which is a safe pattern rather than a dangerous one -
     but it is a pattern nobody asked for, so every coil below is written from
     it rather than left where it was. *)
  filling  := irr_state = 1;
  draining := irr_state = 2;
  flushing := irr_state = 3;
  soaking  := irr_state = 4;

  (* Fill: inlet open, this tray open, outlet shut. Drain: inlet shut, tray
     still open, outlet open. FLUSHING opens BOTH ends with no tray between
     them, so fresh water runs the length of the shared manifold and out
     through the drain: what sat in the line after a tray emptied is not what
     the next tray gets first. *)
  irr_input_ro1  := filling OR flushing;
  irr_input_ro2  := filling OR flushing;
  irr_output_ro1 := draining OR flushing;
  irr_output_ro2 := draining OR flushing;

  (* SOAKING keeps the tray connected with both ends shut, so the dose sits
     where it was put. Every OTHER tray is shut in all three, so one cycle can
     never water two. *)
  onto := filling OR soaking OR draining;
  irr_tray1 := onto AND irr_activeTray = 1;
  irr_tray2 := onto AND irr_activeTray = 2;
  irr_tray3 := onto AND irr_activeTray = 3;
  irr_tray4 := onto AND irr_activeTray = 4;
  irr_tray5 := onto AND irr_activeTray = 5;
  irr_tray6 := onto AND irr_activeTray = 6;
  irr_tray7 := onto AND irr_activeTray = 7;
  irr_tray8 := onto AND irr_activeTray = 8;
END_PROGRAM
