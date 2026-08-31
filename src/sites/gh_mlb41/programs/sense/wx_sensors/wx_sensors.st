(* wx_sensors, as ST. Mirrors
   src/sites/gh_mlb41/programs/sense/wx_sensors/. *)

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
  cli_wind_mps          : REAL;
  cli_windGust_mps      : REAL;
  cli_rain_mmph         : REAL;
  cli_irrad_Wpm2        : REAL;
  cli_wind_mps_qlt      : BOOL;
  cli_rain_mmph_qlt     : BOOL;
  alarm_wx_station      : BOOL;
END_VAR
VAR
  wind_vote  : WX_WORSE;
  gust_vote  : WX_WORSE;
  rain_vote  : WX_WORSE;
  sun_vote   : WX_MEAN;
  wind_pub   : WX_PUBLISH;
  gust_pub   : WX_PUBLISH;
  rain_pub   : WX_PUBLISH;
  sun_pub    : WX_PUBLISH;
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
  wind_vote(A := cli_wind1_mps,     A_OK := cli_wind1_mps_qlt,
            B := cli_wind2_mps,     B_OK := cli_wind2_mps_qlt);
  gust_vote(A := cli_windGust1_mps, A_OK := cli_windGust1_mps_qlt,
            B := cli_windGust2_mps, B_OK := cli_windGust2_mps_qlt);
  rain_vote(A := cli_rain1_mmph,    A_OK := cli_rain1_mmph_qlt,
            B := cli_rain2_mmph,    B_OK := cli_rain2_mmph_qlt);
  sun_vote(A := cli_irrad1_Wpm2,   A_OK := cli_irrad1_Wpm2_qlt,
            B := cli_irrad2_Wpm2,   B_OK := cli_irrad2_Wpm2_qlt);

  (* The rails are the rows' own declared bands. *)
  wind_pub(V := wind_vote.Y, USED := wind_vote.USED, TAU_S := tau,
           NOW := sys_now_ms, LO := 0.0, HI := 80.0);
  gust_pub(V := gust_vote.Y, USED := gust_vote.USED, TAU_S := tau,
           NOW := sys_now_ms, LO := 0.0, HI := 80.0);
  rain_pub(V := rain_vote.Y, USED := rain_vote.USED, TAU_S := tau,
           NOW := sys_now_ms, LO := 0.0, HI := 200.0);
  sun_pub(V := sun_vote.Y,  USED := sun_vote.USED,  TAU_S := tau,
           NOW := sys_now_ms, LO := 0.0, HI := 1500.0);

  cli_wind_mps     := wind_pub.Y;
  cli_windGust_mps := gust_pub.Y;
  cli_rain_mmph    := rain_pub.Y;
  cli_irrad_Wpm2   := sun_pub.Y;

  (* The roof interlock reads both of these, and both rows declare 0, which
     reads as fine weather. So the companions are what let it tell a calm dry
     sky from one nobody is watching. *)
  cli_wind_mps_qlt  := wind_pub.OK;
  cli_rain_mmph_qlt := rain_pub.OK;

  (* The WORST-served quantity decides, so a station answering three of its
     four registers is still a station short. One is enough to keep the plant
     running and is worth sending somebody out for. The dwell is because one
     missed scan of RS-485 is the bus being busy, not a station that has gone. *)
  worst := wind_vote.USED;
  IF gust_vote.USED < worst THEN worst := gust_vote.USED; END_IF;
  IF rain_vote.USED < worst THEN worst := rain_vote.USED; END_IF;
  IF sun_vote.USED  < worst THEN worst := sun_vote.USED;  END_IF;

  quorum_for(IN := worst < 2, PT := UDINT#5000, NOW := sys_now_ms);
  alarm_wx_station := quorum_for.Q;
END_PROGRAM
