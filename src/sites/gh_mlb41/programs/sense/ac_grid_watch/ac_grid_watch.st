(* ac_grid_watch, as ST. Mirrors
   src/sites/gh_mlb41/programs/sense/ac_grid_watch/. *)

PROGRAM ac_grid_watch
VAR_EXTERNAL CONSTANT
  grid_LN_V        : REAL;
  grid_LN_V_qlt    : BOOL;
  grid_total_A     : REAL;
  grid_total_A_qlt : BOOL;
  grid_total_W     : REAL;
  grid_total_W_qlt : BOOL;
  grid_Hz          : REAL;
  grid_Hz_qlt      : BOOL;
  pwr_gridLo_V     : REAL;
  pwr_gridHi_V     : REAL;
  pwr_gridLo_Hz    : REAL;
  pwr_gridHi_Hz    : REAL;
END_VAR
VAR_EXTERNAL
  sys_grid_V      : REAL;
  sys_grid_V_qlt  : BOOL;
  sys_grid_A      : REAL;
  sys_grid_A_qlt  : BOOL;
  sys_grid_W      : REAL;
  sys_grid_W_qlt  : BOOL;
  sys_grid_Hz     : REAL;
  sys_grid_Hz_qlt : BOOL;
  sys_grid_ok     : BOOL;
  alarm_ac_grid   : BOOL;
END_VAR
VAR
  supply : AC_SUPPLY;
END_VAR
  (* The block mirrors the four readings, judges them against the in-spec band,
     and answers both the verdict and the alarm. Whether the meter ANSWERED is
     its own output, and nothing here asks. *)
  supply.V     := grid_LN_V;
  supply.V_OK  := grid_LN_V_qlt;
  supply.A     := grid_total_A;
  supply.A_OK  := grid_total_A_qlt;
  supply.W     := grid_total_W;
  supply.W_OK  := grid_total_W_qlt;
  supply.HZ    := grid_Hz;
  supply.HZ_OK := grid_Hz_qlt;
  supply.LO_V  := pwr_gridLo_V;
  supply.HI_V  := pwr_gridHi_V;
  supply.LO_HZ := pwr_gridLo_Hz;
  supply.HI_HZ := pwr_gridHi_Hz;
  supply();

  sys_grid_V      := supply.OUT_V;
  sys_grid_V_qlt  := supply.OUT_V_OK;
  sys_grid_A      := supply.OUT_A;
  sys_grid_A_qlt  := supply.OUT_A_OK;
  sys_grid_W      := supply.OUT_W;
  sys_grid_W_qlt  := supply.OUT_W_OK;
  sys_grid_Hz     := supply.OUT_HZ;
  sys_grid_Hz_qlt := supply.OUT_HZ_OK;
  sys_grid_ok     := supply.OK;
  alarm_ac_grid   := supply.FAULT;
END_PROGRAM
