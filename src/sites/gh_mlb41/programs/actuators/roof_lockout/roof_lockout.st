(* roof_lockout as ST. The one law that cannot be written without quality:
   cli_rain_mmph and cli_wind_mps declare 0, which reads as fine weather, so a
   station that has stopped answering would OPEN the roof. The two _qlt
   companions are what shut it. *)

PROGRAM roof_lockout
(* VAR_EXTERNAL, not VAR_INPUT: a program binds rows by NAME, which is what
   VAR_EXTERNAL means, and matiec then emits no copy in or out - the body
   reads and writes the bank directly and __INIT_EXTERNAL is the whole of
   bind(). The formal-parameter connection list does the opposite and does
   not compile against matiec's own runtime. See docs/dev/matiec-configuration.md *)
VAR_EXTERNAL
  cli_rain_mmph         : REAL;
  cli_rain_mmph_qlt     : BOOL;
  cli_wind_mps          : REAL;
  cli_wind_mps_qlt      : BOOL;
  cli_windGust_mps      : REAL;
  cli_windShutBegin_mps : REAL;
  cli_windShutEnd_mps   : REAL;
  cli_windShutMax       : REAL;
  cli_rainLockout_sec   : UINT;
  sys_supply_lockout    : BOOL;
  sys_now_ms : UDINT;
  cli_rainLockout       : BOOL;
  cli_windLockout       : BOOL;
  cli_windowLockout     : BOOL;
  cli_windowCap         : REAL;
END_VAR
VAR
  rain_for : TOF_MS;
END_VAR
VAR_TEMP
  blind   : BOOL;
  w       : REAL;
  ceiling : REAL;
END_VAR
  blind := NOT cli_rain_mmph_qlt OR NOT cli_wind_mps_qlt;

  (* An OVERRUN on the rain: the roof stays shut for the declared time after
     the last drop. Being BLIND is not a shower - it is a live interlock that
     lets go the moment the station answers, where feeding it the overrun cost
     a quarter of an hour of ventilation after every restart. *)
  rain_for(IN := cli_rain_mmph > 0.0, PT := UINT_TO_UDINT(cli_rainLockout_sec) * 1000,
           NOW := sys_now_ms);
  cli_rainLockout := rain_for.Q OR blind;

  (* The GUST, not the average: what tears a window off its ram is the peak. *)
  IF cli_windGust_mps > cli_wind_mps THEN w := cli_windGust_mps; ELSE w := cli_wind_mps; END_IF;
  cli_windLockout := blind OR (w >= cli_windShutEnd_mps);

  cli_windowLockout := cli_rainLockout OR cli_windLockout OR sys_supply_lockout;

  (* How far the roof may open, which is not a yes or a no: from cli_windShutBegin_mps the
     ceiling comes down a straight line to nothing at cli_windShutEnd_mps. *)
  ceiling := 1.0;
  IF w >= cli_windShutBegin_mps AND cli_windShutEnd_mps > cli_windShutBegin_mps THEN
    ceiling := cli_windShutMax * (1.0 - (w - cli_windShutBegin_mps) / (cli_windShutEnd_mps - cli_windShutBegin_mps));
  END_IF;
  IF cli_windowLockout THEN cli_windowCap := 0.0;
  ELSE cli_windowCap := LIMIT(0.0, ceiling, 1.0);
  END_IF;
END_PROGRAM
