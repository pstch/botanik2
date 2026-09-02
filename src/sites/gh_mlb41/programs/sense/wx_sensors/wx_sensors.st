(* wx_sensors program
   ------------------------------------------------------------------------ *)

PROGRAM wx_sensors
VAR_EXTERNAL CONSTANT
  cli_wind1_mps         : REAL;
  cli_wind1_mps_qlt     : BOOL;
  cli_wind2_mps         : REAL;
  cli_wind2_mps_qlt     : BOOL;
  cli_windGust1_mps     : REAL;
  cli_windGust1_mps_qlt : BOOL;
  cli_windGust2_mps     : REAL;
  cli_windGust2_mps_qlt : BOOL;
  cli_rain1_mmph        : REAL;
  cli_rain1_mmph_qlt    : BOOL;
  cli_rain2_mmph        : REAL;
  cli_rain2_mmph_qlt    : BOOL;
  cli_irrad1_Wpm2       : REAL;
  cli_irrad1_Wpm2_qlt   : BOOL;
  cli_irrad2_Wpm2       : REAL;
  cli_irrad2_Wpm2_qlt   : BOOL;
  cli_wxTau_ms          : UINT;
  sys_now_ms            : UDINT;
END_VAR
VAR_EXTERNAL
  cli_wind_mps      : REAL;
  cli_windGust_mps  : REAL;
  cli_rain_mmph     : REAL;
  cli_irrad_Wpm2    : REAL;
  cli_wind_mps_qlt  : BOOL;
  cli_rain_mmph_qlt : BOOL;
  alarm_wx_station  : BOOL;
END_VAR
VAR
  wind_vote  : WX_WORSE;
  gust_vote  : WX_WORSE;
  rain_vote  : WX_WORSE;
  sun_vote   : WX_MEAN;
  wind_pub   : EWMA;
  gust_pub   : EWMA;
  rain_pub   : EWMA;
  sun_pub    : EWMA;
  quorum_for : TON_MS;
END_VAR
VAR_TEMP
  tau   : REAL;
  worst : USINT;
END_VAR
  (* One tau for all four, shorter than the probes': a roof interlock answers
     weather that moves in seconds. *)
  tau := UINT_TO_REAL(cli_wxTau_ms) / 1000.0;

  (* Three of the four take the worse reading, because three of the four are
     read by an interlock. Irradiance is not. *)
  wind_vote.A    := cli_wind1_mps;
  wind_vote.A_OK := cli_wind1_mps_qlt;
  wind_vote.B    := cli_wind2_mps;
  wind_vote.B_OK := cli_wind2_mps_qlt;
  wind_vote();
  gust_vote.A    := cli_windGust1_mps;
  gust_vote.A_OK := cli_windGust1_mps_qlt;
  gust_vote.B    := cli_windGust2_mps;
  gust_vote.B_OK := cli_windGust2_mps_qlt;
  gust_vote();
  rain_vote.A    := cli_rain1_mmph;
  rain_vote.A_OK := cli_rain1_mmph_qlt;
  rain_vote.B    := cli_rain2_mmph;
  rain_vote.B_OK := cli_rain2_mmph_qlt;
  rain_vote();
  sun_vote.A    := cli_irrad1_Wpm2;
  sun_vote.A_OK := cli_irrad1_Wpm2_qlt;
  sun_vote.B    := cli_irrad2_Wpm2;
  sun_vote.B_OK := cli_irrad2_Wpm2_qlt;
  sun_vote();

  (* The rails are the rows' own declared bands. *)
  wind_pub.X     := wind_vote.Y;
  wind_pub.OK_IN := wind_vote.USED > 0;
  wind_pub.TAU_S := tau;
  wind_pub.NOW   := sys_now_ms;
  wind_pub.LO    := 0.0;
  wind_pub.HI    := 80.0;
  wind_pub();
  gust_pub.X     := gust_vote.Y;
  gust_pub.OK_IN := gust_vote.USED > 0;
  gust_pub.TAU_S := tau;
  gust_pub.NOW   := sys_now_ms;
  gust_pub.LO    := 0.0;
  gust_pub.HI    := 80.0;
  gust_pub();
  rain_pub.X     := rain_vote.Y;
  rain_pub.OK_IN := rain_vote.USED > 0;
  rain_pub.TAU_S := tau;
  rain_pub.NOW   := sys_now_ms;
  rain_pub.LO    := 0.0;
  rain_pub.HI    := 200.0;
  rain_pub();
  sun_pub.X     := sun_vote.Y;
  sun_pub.OK_IN := sun_vote.USED > 0;
  sun_pub.TAU_S := tau;
  sun_pub.NOW   := sys_now_ms;
  sun_pub.LO    := 0.0;
  sun_pub.HI    := 1500.0;
  sun_pub();

  cli_wind_mps     := wind_pub.Y;
  cli_windGust_mps := gust_pub.Y;
  cli_rain_mmph    := rain_pub.Y;
  cli_irrad_Wpm2   := sun_pub.Y;

  (* Calm and dry is what a station reads when it has stopped answering, so
     the roof interlock is told which of the two it is looking at. *)
  cli_wind_mps_qlt  := wind_pub.OK;
  cli_rain_mmph_qlt := rain_pub.OK;

  (* The WORST-served quantity decides, so a station answering three of its
     four registers is still a station short. One is enough to keep the plant
     running and is worth sending somebody out for. The dwell is because one
     missed scan of the field bus is the bus being busy, not a station that
     has gone. *)
  worst := wind_vote.USED;
  IF gust_vote.USED < worst THEN worst := gust_vote.USED; END_IF;
  IF rain_vote.USED < worst THEN worst := rain_vote.USED; END_IF;
  IF sun_vote.USED  < worst THEN worst := sun_vote.USED;  END_IF;

  quorum_for(IN := worst < 2, PT := UDINT#5000, NOW := sys_now_ms);
  alarm_wx_station := quorum_for.Q;
END_PROGRAM
