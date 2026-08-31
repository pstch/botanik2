(* weather_watch, as ST. Mirrors
   src/sites/gh_mlb41/programs/sense/weather_watch/. *)

PROGRAM weather_watch
VAR_EXTERNAL CONSTANT
  cli_wind_mps        : REAL;
  cli_rain_mmph       : REAL;
  cli_wind_mps_qlt    : BOOL;
  cli_rain_mmph_qlt   : BOOL;
  cli_windShutEnd_mps : REAL;
  sys_now_ms          : UDINT;
END_VAR
VAR_EXTERNAL
  alarm_weather_storm : BOOL;
  cli_sinceRain_min   : UINT;
END_VAR
VAR
  seen   : BOOL := FALSE;   (* the station has answered at least once *)
  dry_ms : UDINT;
  last   : UDINT;
  primed : BOOL := FALSE;
END_VAR
VAR_TEMP
  mins : UDINT;
END_VAR
  IF cli_wind_mps_qlt AND cli_rain_mmph_qlt THEN seen := TRUE; END_IF;

  (* A STORM is sustained wind: cli_windShutEnd_mps, which is 75 kmph and
     Beaufort 9. The same number that shuts the roof, because it is the same
     fact. The MEAN, not the gust - a gust is a moment and a storm is a
     condition - and only once the station has answered, or every boot
     announces one. *)
  alarm_weather_storm := seen AND cli_wind_mps >= cli_windShutEnd_mps;

  (* How long the house has been dry. A gauge that is not answering says
     nothing about the weather, so the clock STANDS rather than running on.
     It runs from BOOT, not from the first shower this unit happened to
     witness: before then the answer is "at least this long", which is both
     true and what the roof cares about. *)
  IF primed THEN
    IF cli_rain_mmph_qlt THEN
      IF cli_rain_mmph > 0.0 THEN dry_ms := 0;
      ELSE dry_ms := dry_ms + (sys_now_ms - last);
      END_IF;
    END_IF;
  ELSE
    primed := TRUE;
  END_IF;
  last := sys_now_ms;

  mins := dry_ms / 60000;
  IF mins > 65535 THEN mins := 65535; END_IF;
  cli_sinceRain_min := UDINT_TO_UINT(mins);
END_PROGRAM
