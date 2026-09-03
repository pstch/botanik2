(* ac_local_watch program
   ------------------------------------------------------------------------ *)

PROGRAM ac_local_watch
VAR_EXTERNAL CONSTANT
  ats_V            : REAL;
  ats_V_qlt        : QUALITY;
  ats_A            : REAL;
  ats_A_qlt        : QUALITY;
  ats_W            : REAL;
  ats_W_qlt        : QUALITY;
  ats_Hz           : REAL;
  ats_Hz_qlt       : QUALITY;
  pwr_localLo_V    : REAL;
  pwr_localHi_V    : REAL;
  pwr_localLo_Hz   : REAL;
  pwr_localHi_Hz   : REAL;
  sys_grid_ok      : BOOL;
  pwr_genStart_sec : UINT;
  sys_now_ms       : UDINT;
END_VAR
VAR_EXTERNAL
  sys_local_V      : REAL;
  sys_local_V_qlt  : QUALITY;
  sys_local_A      : REAL;
  sys_local_A_qlt  : QUALITY;
  sys_local_W      : REAL;
  sys_local_W_qlt  : QUALITY;
  sys_local_Hz     : REAL;
  sys_local_Hz_qlt : QUALITY;
  sys_local_ok     : BOOL;
  sys_local_ok_qlt : QUALITY;
  alarm_ac_local   : BOOL;
  alarm_gen        : BOOL;
END_VAR
VAR
  supply   : AC_SUPPLY;
  cranking : TON_MS;
END_VAR
  supply.V     := ats_V;
  supply.V_OK  := GOOD(ats_V_qlt);
  supply.A     := ats_A;
  supply.A_OK  := GOOD(ats_A_qlt);
  supply.W     := ats_W;
  supply.W_OK  := GOOD(ats_W_qlt);
  supply.HZ    := ats_Hz;
  supply.HZ_OK := GOOD(ats_Hz_qlt);
  supply.LO_V  := pwr_localLo_V;
  supply.HI_V  := pwr_localHi_V;
  supply.LO_HZ := pwr_localLo_Hz;
  supply.HI_HZ := pwr_localHi_Hz;
  supply();

  sys_local_V      := supply.OUT_V;
  sys_local_V_qlt  := QUAL(supply.OUT_V_OK);
  sys_local_A      := supply.OUT_A;
  sys_local_A_qlt  := QUAL(supply.OUT_A_OK);
  sys_local_W      := supply.OUT_W;
  sys_local_W_qlt  := QUAL(supply.OUT_W_OK);
  sys_local_Hz     := supply.OUT_HZ;
  sys_local_Hz_qlt := QUAL(supply.OUT_HZ_OK);
  sys_local_ok     := supply.OK;
  sys_local_ok_qlt := QUAL(supply.ANSWERED);
  alarm_ac_local   := supply.FAULT;

  (* The generator only owes an answer once the grid has gone. With the grid
     up, an unhealthy ATS output is a transfer-switch fault - alarm_ac_local
     above - and the set was never asked to start. *)
  cranking.IN  := NOT sys_grid_ok AND NOT supply.ANSWERED;
  cranking.PT  := SEC_MS(pwr_genStart_sec);
  cranking.NOW := sys_now_ms;
  cranking();
  alarm_gen := cranking.Q;
END_PROGRAM
