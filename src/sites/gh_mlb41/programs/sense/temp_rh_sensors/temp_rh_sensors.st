(* temp_rh_sensors, as ST. Four zones, each with its own quorum. *)

PROGRAM temp_rh_sensors
VAR_EXTERNAL CONSTANT
  cli_sensorTau_ms : UINT;
  sys_now_ms       : UDINT;
  cli_temp_hi1     : REAL;
  cli_temp_hi1_qlt : BOOL;
  cli_temp_hi2     : REAL;
  cli_temp_hi2_qlt : BOOL;
  cli_temp_hi3     : REAL;
  cli_temp_hi3_qlt : BOOL;
  cli_rh_hi1       : REAL;
  cli_rh_hi1_qlt   : BOOL;
  cli_rh_hi2       : REAL;
  cli_rh_hi2_qlt   : BOOL;
  cli_rh_hi3       : REAL;
  cli_rh_hi3_qlt   : BOOL;
  cli_temp_lo1     : REAL;
  cli_temp_lo1_qlt : BOOL;
  cli_temp_lo2     : REAL;
  cli_temp_lo2_qlt : BOOL;
  cli_temp_lo3     : REAL;
  cli_temp_lo3_qlt : BOOL;
  cli_rh_lo1       : REAL;
  cli_rh_lo1_qlt   : BOOL;
  cli_rh_lo2       : REAL;
  cli_rh_lo2_qlt   : BOOL;
  cli_rh_lo3       : REAL;
  cli_rh_lo3_qlt   : BOOL;
  cli_temp_cs1     : REAL;
  cli_temp_cs1_qlt : BOOL;
  cli_temp_cs2     : REAL;
  cli_temp_cs2_qlt : BOOL;
  cli_temp_cs3     : REAL;
  cli_temp_cs3_qlt : BOOL;
  cli_rh_cs1       : REAL;
  cli_rh_cs1_qlt   : BOOL;
  cli_rh_cs2       : REAL;
  cli_rh_cs2_qlt   : BOOL;
  cli_rh_cs3       : REAL;
  cli_rh_cs3_qlt   : BOOL;
  cli_temp_ext1     : REAL;
  cli_temp_ext1_qlt : BOOL;
  cli_temp_ext2     : REAL;
  cli_temp_ext2_qlt : BOOL;
  cli_temp_ext3     : REAL;
  cli_temp_ext3_qlt : BOOL;
  cli_rh_ext1       : REAL;
  cli_rh_ext1_qlt   : BOOL;
  cli_rh_ext2       : REAL;
  cli_rh_ext2_qlt   : BOOL;
  cli_rh_ext3       : REAL;
  cli_rh_ext3_qlt   : BOOL;
END_VAR
VAR_EXTERNAL
  cli_temp_hi        : REAL;
  cli_rh_hi          : REAL;
  alarm_sns_ceiling   : BOOL;
  cli_temp_lo        : REAL;
  cli_rh_lo          : REAL;
  alarm_sns_floor     : BOOL;
  cli_temp_cs        : REAL;
  cli_rh_cs          : REAL;
  alarm_sns_crawlspace : BOOL;
  cli_temp_ext        : REAL;
  cli_rh_ext          : REAL;
  alarm_sns_exterior  : BOOL;
  cli_temp_hi_qlt    : BOOL;
  cli_temp_lo_qlt    : BOOL;
  cli_temp_cs_qlt    : BOOL;
  cli_temp_ext_qlt   : BOOL;
  cli_rh_hi_qlt      : BOOL;
  cli_rh_lo_qlt      : BOOL;
  cli_rh_cs_qlt      : BOOL;
  cli_rh_ext_qlt     : BOOL;
END_VAR
VAR
  ceiling    : ZONE_PROBES;
  floor      : ZONE_PROBES;
  crawlspace : ZONE_PROBES;
  exterior   : ZONE_PROBES;
END_VAR
VAR_TEMP
  tau_s : REAL;
END_VAR
  tau_s := UINT_TO_REAL(cli_sensorTau_ms) / 1000.0;

  ceiling(T1 := cli_temp_hi1, T1_OK := cli_temp_hi1_qlt,
     T2 := cli_temp_hi2, T2_OK := cli_temp_hi2_qlt,
     T3 := cli_temp_hi3, T3_OK := cli_temp_hi3_qlt,
     H1 := cli_rh_hi1,   H1_OK := cli_rh_hi1_qlt,
     H2 := cli_rh_hi2,   H2_OK := cli_rh_hi2_qlt,
     H3 := cli_rh_hi3,   H3_OK := cli_rh_hi3_qlt,
     TAU_S := tau_s, NOW := sys_now_ms);
  cli_temp_hi      := ceiling.TEMP;
  cli_rh_hi        := ceiling.RH;
  alarm_sns_ceiling := ceiling.FAULT;

  floor(T1 := cli_temp_lo1, T1_OK := cli_temp_lo1_qlt,
     T2 := cli_temp_lo2, T2_OK := cli_temp_lo2_qlt,
     T3 := cli_temp_lo3, T3_OK := cli_temp_lo3_qlt,
     H1 := cli_rh_lo1,   H1_OK := cli_rh_lo1_qlt,
     H2 := cli_rh_lo2,   H2_OK := cli_rh_lo2_qlt,
     H3 := cli_rh_lo3,   H3_OK := cli_rh_lo3_qlt,
     TAU_S := tau_s, NOW := sys_now_ms);
  cli_temp_lo      := floor.TEMP;
  cli_rh_lo        := floor.RH;
  alarm_sns_floor := floor.FAULT;

  crawlspace(T1 := cli_temp_cs1, T1_OK := cli_temp_cs1_qlt,
     T2 := cli_temp_cs2, T2_OK := cli_temp_cs2_qlt,
     T3 := cli_temp_cs3, T3_OK := cli_temp_cs3_qlt,
     H1 := cli_rh_cs1,   H1_OK := cli_rh_cs1_qlt,
     H2 := cli_rh_cs2,   H2_OK := cli_rh_cs2_qlt,
     H3 := cli_rh_cs3,   H3_OK := cli_rh_cs3_qlt,
     TAU_S := tau_s, NOW := sys_now_ms);
  cli_temp_cs      := crawlspace.TEMP;
  cli_rh_cs        := crawlspace.RH;
  alarm_sns_crawlspace := crawlspace.FAULT;

  exterior(T1 := cli_temp_ext1, T1_OK := cli_temp_ext1_qlt,
     T2 := cli_temp_ext2, T2_OK := cli_temp_ext2_qlt,
     T3 := cli_temp_ext3, T3_OK := cli_temp_ext3_qlt,
     H1 := cli_rh_ext1,   H1_OK := cli_rh_ext1_qlt,
     H2 := cli_rh_ext2,   H2_OK := cli_rh_ext2_qlt,
     H3 := cli_rh_ext3,   H3_OK := cli_rh_ext3_qlt,
     TAU_S := tau_s, NOW := sys_now_ms);
  cli_temp_ext      := exterior.TEMP;
  cli_rh_ext        := exterior.RH;
  alarm_sns_exterior := exterior.FAULT;

  (* Every zone says whether it MEASURED, per quantity. A derived row cannot
     carry its own quality any more - ST has no way to write one - so the
     companion is where the doubt lives, and a law reads it instead of asking
     the bank. The two quorums are separate: one dead humidity probe must not
     invalidate a temperature three good probes agree on. *)
  cli_temp_hi_qlt  := ceiling.TEMP_OK;
  cli_temp_lo_qlt  := floor.TEMP_OK;
  cli_temp_cs_qlt  := crawlspace.TEMP_OK;
  cli_temp_ext_qlt := exterior.TEMP_OK;
  cli_rh_hi_qlt    := ceiling.RH_OK;
  cli_rh_lo_qlt    := floor.RH_OK;
  cli_rh_cs_qlt    := crawlspace.RH_OK;
  cli_rh_ext_qlt   := exterior.RH_OK;
END_PROGRAM
