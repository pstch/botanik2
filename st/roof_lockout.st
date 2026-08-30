(* roof_lockout as ST. The one law that cannot be written without quality:
   cli_rain_mmph and cli_wind_mps declare 0, which reads as fine weather, so a
   station that has stopped answering would OPEN the roof. The two _qlt
   companions are what shut it. *)

PROGRAM roof_lockout
VAR_INPUT
  rain_mmph     : REAL;
  rain_mmph_qlt : BOOL;
  wind_mps      : REAL;
  wind_mps_qlt  : BOOL;
  gust_mps      : REAL;
  shut_begin    : REAL;
  shut_end      : REAL;
  shut_max      : REAL;
  rain_hold_s   : UINT;
  supply_lock   : BOOL;
  dt_ms         : UDINT;
END_VAR
VAR_OUTPUT
  rain_lock   : BOOL;
  wind_lock   : BOOL;
  window_lock : BOOL;
  cap         : REAL;
END_VAR
VAR
  rain_for : TOF_MS;
END_VAR
VAR_TEMP
  blind   : BOOL;
  w       : REAL;
  ceiling : REAL;
END_VAR
  blind := NOT rain_mmph_qlt OR NOT wind_mps_qlt;

  (* An OVERRUN on the rain: the roof stays shut for the declared time after
     the last drop. Being BLIND is not a shower - it is a live interlock that
     lets go the moment the station answers, where feeding it the overrun cost
     a quarter of an hour of ventilation after every restart. *)
  rain_for(IN := rain_mmph > 0.0, PT := UINT_TO_UDINT(rain_hold_s) * 1000,
           DT_MS := dt_ms);
  rain_lock := rain_for.Q OR blind;

  (* The GUST, not the average: what tears a window off its ram is the peak. *)
  IF gust_mps > wind_mps THEN w := gust_mps; ELSE w := wind_mps; END_IF;
  wind_lock := blind OR (w >= shut_end);

  window_lock := rain_lock OR wind_lock OR supply_lock;

  (* How far the roof may open, which is not a yes or a no: from shut_begin the
     ceiling comes down a straight line to nothing at shut_end. *)
  ceiling := 1.0;
  IF w >= shut_begin AND shut_end > shut_begin THEN
    ceiling := shut_max * (1.0 - (w - shut_begin) / (shut_end - shut_begin));
  END_IF;
  IF window_lock THEN cap := 0.0;
  ELSE cap := LIMIT(0.0, ceiling, 1.0);
  END_IF;
END_PROGRAM
