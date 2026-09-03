(* dc_watch program
   ------------------------------------------------------------------------ *)

PROGRAM dc_watch
VAR_EXTERNAL CONSTANT
  pwr_dc1_V         : REAL;
  pwr_dc1_V_qlt     : QUALITY;
  pwr_dc2_V         : REAL;
  pwr_dc2_V_qlt     : QUALITY;
  pwr_upsBat_V      : REAL;
  pwr_upsBat_V_qlt  : QUALITY;
  pwr_genBat_V      : REAL;
  pwr_genBat_V_qlt  : QUALITY;
  pwr_dc1Lo_V       : REAL;
  pwr_dc2Lo_V       : REAL;
  pwr_upsBatLo_V    : REAL;
  pwr_genBatLo_V    : REAL;
  pwr_dc1_avg_ms    : UINT;
  pwr_dc2_avg_ms    : UINT;
  pwr_upsBat_avg_ms : UINT;
  pwr_genBat_avg_ms : UINT;
  pwr_upsLowBat     : BOOL;
  pwr_upsLowBat_qlt : QUALITY;
  sys_now_ms        : UDINT;
END_VAR
VAR_EXTERNAL
  sys_dc1_V               : REAL;
  sys_dc1_V_qlt           : QUALITY;
  sys_dc2_V               : REAL;
  sys_dc2_V_qlt           : QUALITY;
  sys_upsBat_V            : REAL;
  sys_upsBat_V_qlt        : QUALITY;
  sys_genBat_V            : REAL;
  sys_genBat_V_qlt        : QUALITY;
  sys_dc_ok               : BOOL;
  sys_dc_ok_qlt           : QUALITY;
  sys_supply_lockout      : BOOL;
  sys_supply_lockout_qlt  : QUALITY;
  alarm_dc_ps1            : BOOL;
  alarm_dc_ps2            : BOOL;
  alarm_dc_ups            : BOOL;
  alarm_generator_battery : BOOL;
END_VAR
VAR
  ps1     : DC_RAIL;
  ps2     : DC_RAIL;
  ups     : DC_RAIL;
  gen     : DC_RAIL;
  low_for : TON_MS;
END_VAR
VAR_TEMP
  low : BOOL;
END_VAR
  (* 0.5 V is the same publish deadband the AC supplies use: it is about the
     METER's last digit and not about the plant. *)
  ps1.V      := pwr_dc1_V;
  ps1.V_OK   := GOOD(pwr_dc1_V_qlt);
  ps1.LO     := pwr_dc1Lo_V;
  ps1.AVG_MS := pwr_dc1_avg_ms;
  ps1.BAND   := 0.5;
  ps1.NOW    := sys_now_ms;
  ps1();
  ps2.V      := pwr_dc2_V;
  ps2.V_OK   := GOOD(pwr_dc2_V_qlt);
  ps2.LO     := pwr_dc2Lo_V;
  ps2.AVG_MS := pwr_dc2_avg_ms;
  ps2.BAND   := 0.5;
  ps2.NOW    := sys_now_ms;
  ps2();
  ups.V      := pwr_upsBat_V;
  ups.V_OK   := GOOD(pwr_upsBat_V_qlt);
  ups.LO     := pwr_upsBatLo_V;
  ups.AVG_MS := pwr_upsBat_avg_ms;
  ups.BAND   := 0.5;
  ups.NOW    := sys_now_ms;
  ups();
  gen.V      := pwr_genBat_V;
  gen.V_OK   := GOOD(pwr_genBat_V_qlt);
  gen.LO     := pwr_genBatLo_V;
  gen.AVG_MS := pwr_genBat_avg_ms;
  gen.BAND   := 0.5;
  gen.NOW    := sys_now_ms;
  gen();

  sys_dc1_V        := ps1.Y;
  sys_dc1_V_qlt    := QUAL(ps1.OK);
  sys_dc2_V        := ps2.Y;
  sys_dc2_V_qlt    := QUAL(ps2.OK);
  sys_upsBat_V     := ups.Y;
  sys_upsBat_V_qlt := QUAL(ups.OK);
  sys_genBat_V     := gen.Y;
  sys_genBat_V_qlt := QUAL(gen.OK);

  alarm_dc_ps1            := NOT ps1.HEALTHY;
  alarm_dc_ps2            := NOT ps2.HEALTHY;
  alarm_generator_battery := NOT gen.HEALTHY;

  (* THE CONTACT, not the divider: the UPS knows what its own battery has left,
     and a voltage on the rail is a guess at it. Debounced, because a dry
     contact bounces and this one holds the whole plant. *)
  low_for(IN := pwr_upsLowBat, PT := UDINT#2000, NOW := sys_now_ms);
  low                := low_for.Q;
  alarm_dc_ups           := low;
  sys_supply_lockout     := low;
  (* The interlock is the CONTACT's, so it is only as believable as the
     terminal that reads it. *)
  sys_supply_lockout_qlt := pwr_upsLowBat_qlt;

  (* The battery voltage is a WARNING and the contact is the interlock, so the
     summary carries both: an operator reading one row wants the whole answer. *)
  sys_dc_ok := ps1.HEALTHY AND ps2.HEALTHY AND ups.HEALTHY AND gen.HEALTHY
               AND NOT low;
  (* A summary of four rails is worth what the four are: one unread rail makes
     the whole verdict a guess. *)
  sys_dc_ok_qlt := QUAL(ps1.OK AND ps2.OK AND ups.OK AND gen.OK
                        AND GOOD(pwr_upsLowBat_qlt));
END_PROGRAM
