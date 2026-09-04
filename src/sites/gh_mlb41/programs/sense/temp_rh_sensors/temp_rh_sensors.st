(* temp_rh_sensors program
   ------------------------------------------------------------------------ *)

(* Four zones, each with its own quorum. *)

PROGRAM temp_rh_sensors
VAR_EXTERNAL CONSTANT
  cli_sensorTau_ms  : UINT;
  sys_now_ms        : UDINT;
  cli_temp_hi1      : REAL;
  cli_temp_hi1_qlt  : QUALITY;
  cli_temp_hi2      : REAL;
  cli_temp_hi2_qlt  : QUALITY;
  cli_temp_hi3      : REAL;
  cli_temp_hi3_qlt  : QUALITY;
  cli_rh_hi1        : REAL;
  cli_rh_hi1_qlt    : QUALITY;
  cli_rh_hi2        : REAL;
  cli_rh_hi2_qlt    : QUALITY;
  cli_rh_hi3        : REAL;
  cli_rh_hi3_qlt    : QUALITY;
  cli_temp_lo1      : REAL;
  cli_temp_lo1_qlt  : QUALITY;
  cli_temp_lo2      : REAL;
  cli_temp_lo2_qlt  : QUALITY;
  cli_temp_lo3      : REAL;
  cli_temp_lo3_qlt  : QUALITY;
  cli_rh_lo1        : REAL;
  cli_rh_lo1_qlt    : QUALITY;
  cli_rh_lo2        : REAL;
  cli_rh_lo2_qlt    : QUALITY;
  cli_rh_lo3        : REAL;
  cli_rh_lo3_qlt    : QUALITY;
  cli_temp_cs1      : REAL;
  cli_temp_cs1_qlt  : QUALITY;
  cli_temp_cs2      : REAL;
  cli_temp_cs2_qlt  : QUALITY;
  cli_temp_cs3      : REAL;
  cli_temp_cs3_qlt  : QUALITY;
  cli_rh_cs1        : REAL;
  cli_rh_cs1_qlt    : QUALITY;
  cli_rh_cs2        : REAL;
  cli_rh_cs2_qlt    : QUALITY;
  cli_rh_cs3        : REAL;
  cli_rh_cs3_qlt    : QUALITY;
  cli_temp_ext1     : REAL;
  cli_temp_ext1_qlt : QUALITY;
  cli_temp_ext2     : REAL;
  cli_temp_ext2_qlt : QUALITY;
  cli_temp_ext3     : REAL;
  cli_temp_ext3_qlt : QUALITY;
  cli_rh_ext1       : REAL;
  cli_rh_ext1_qlt   : QUALITY;
  cli_rh_ext2       : REAL;
  cli_rh_ext2_qlt   : QUALITY;
  cli_rh_ext3       : REAL;
  cli_rh_ext3_qlt   : QUALITY;
END_VAR
VAR_EXTERNAL
  cli_temp_hi          : REAL;
  cli_rh_hi            : REAL;
  alarm_sns_ceiling    : BOOL;
  cli_temp_lo          : REAL;
  cli_rh_lo            : REAL;
  alarm_sns_floor      : BOOL;
  cli_temp_cs          : REAL;
  cli_rh_cs            : REAL;
  alarm_sns_crawlspace : BOOL;
  cli_temp_ext         : REAL;
  cli_rh_ext           : REAL;
  alarm_sns_exterior   : BOOL;
  cli_temp_hi_qlt      : QUALITY;
  cli_temp_lo_qlt      : QUALITY;
  cli_temp_cs_qlt      : QUALITY;
  cli_temp_ext_qlt     : QUALITY;
  cli_rh_hi_qlt        : QUALITY;
  cli_rh_lo_qlt        : QUALITY;
  cli_rh_cs_qlt        : QUALITY;
  cli_rh_ext_qlt       : QUALITY;
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

  ceiling.T1    := cli_temp_hi1;
  ceiling.T1_Q  := cli_temp_hi1_qlt;
  ceiling.T2    := cli_temp_hi2;
  ceiling.T2_Q  := cli_temp_hi2_qlt;
  ceiling.T3    := cli_temp_hi3;
  ceiling.T3_Q  := cli_temp_hi3_qlt;
  ceiling.H1    := cli_rh_hi1;
  ceiling.H1_Q  := cli_rh_hi1_qlt;
  ceiling.H2    := cli_rh_hi2;
  ceiling.H2_Q  := cli_rh_hi2_qlt;
  ceiling.H3    := cli_rh_hi3;
  ceiling.H3_Q  := cli_rh_hi3_qlt;
  ceiling.TAU_S := tau_s;
  ceiling.NOW   := sys_now_ms;
  ceiling();
  cli_temp_hi      := ceiling.TEMP;
  cli_rh_hi        := ceiling.RH;
  alarm_sns_ceiling := ceiling.FAULT;

  floor.T1    := cli_temp_lo1;
  floor.T1_Q  := cli_temp_lo1_qlt;
  floor.T2    := cli_temp_lo2;
  floor.T2_Q  := cli_temp_lo2_qlt;
  floor.T3    := cli_temp_lo3;
  floor.T3_Q  := cli_temp_lo3_qlt;
  floor.H1    := cli_rh_lo1;
  floor.H1_Q  := cli_rh_lo1_qlt;
  floor.H2    := cli_rh_lo2;
  floor.H2_Q  := cli_rh_lo2_qlt;
  floor.H3    := cli_rh_lo3;
  floor.H3_Q  := cli_rh_lo3_qlt;
  floor.TAU_S := tau_s;
  floor.NOW   := sys_now_ms;
  floor();
  cli_temp_lo      := floor.TEMP;
  cli_rh_lo        := floor.RH;
  alarm_sns_floor := floor.FAULT;

  crawlspace.T1    := cli_temp_cs1;
  crawlspace.T1_Q  := cli_temp_cs1_qlt;
  crawlspace.T2    := cli_temp_cs2;
  crawlspace.T2_Q  := cli_temp_cs2_qlt;
  crawlspace.T3    := cli_temp_cs3;
  crawlspace.T3_Q  := cli_temp_cs3_qlt;
  crawlspace.H1    := cli_rh_cs1;
  crawlspace.H1_Q  := cli_rh_cs1_qlt;
  crawlspace.H2    := cli_rh_cs2;
  crawlspace.H2_Q  := cli_rh_cs2_qlt;
  crawlspace.H3    := cli_rh_cs3;
  crawlspace.H3_Q  := cli_rh_cs3_qlt;
  crawlspace.TAU_S := tau_s;
  crawlspace.NOW   := sys_now_ms;
  crawlspace();
  cli_temp_cs      := crawlspace.TEMP;
  cli_rh_cs        := crawlspace.RH;
  alarm_sns_crawlspace := crawlspace.FAULT;

  exterior.T1    := cli_temp_ext1;
  exterior.T1_Q  := cli_temp_ext1_qlt;
  exterior.T2    := cli_temp_ext2;
  exterior.T2_Q  := cli_temp_ext2_qlt;
  exterior.T3    := cli_temp_ext3;
  exterior.T3_Q  := cli_temp_ext3_qlt;
  exterior.H1    := cli_rh_ext1;
  exterior.H1_Q  := cli_rh_ext1_qlt;
  exterior.H2    := cli_rh_ext2;
  exterior.H2_Q  := cli_rh_ext2_qlt;
  exterior.H3    := cli_rh_ext3;
  exterior.H3_Q  := cli_rh_ext3_qlt;
  exterior.TAU_S := tau_s;
  exterior.NOW   := sys_now_ms;
  exterior();
  cli_temp_ext      := exterior.TEMP;
  cli_rh_ext        := exterior.RH;
  alarm_sns_exterior := exterior.FAULT;

  (* Every zone says whether it MEASURED, per quantity, and that is where a
     law downstream reads the doubt. The two quorums are separate: one dead
     humidity probe must not invalidate a temperature three good probes agree
     on. *)
  cli_temp_hi_qlt  := ceiling.TEMP_Q;
  cli_temp_lo_qlt  := floor.TEMP_Q;
  cli_temp_cs_qlt  := crawlspace.TEMP_Q;
  cli_temp_ext_qlt := exterior.TEMP_Q;
  cli_rh_hi_qlt    := ceiling.RH_Q;
  cli_rh_lo_qlt    := floor.RH_Q;
  cli_rh_cs_qlt    := crawlspace.RH_Q;
  cli_rh_ext_qlt   := exterior.RH_Q;
END_PROGRAM
