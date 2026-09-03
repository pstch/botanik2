(* weather_watch program
   ------------------------------------------------------------------------ *)

PROGRAM weather_watch
VAR_EXTERNAL CONSTANT
  cli_wind_mps        : REAL;
  cli_rain_mmph       : REAL;
  cli_wind_mps_qlt    : QUALITY;
  cli_rain_mmph_qlt   : QUALITY;
  cli_windShutEnd_mps : REAL;
  sys_now_ms          : UDINT;
END_VAR
VAR_EXTERNAL
  alarm_weather_storm : BOOL;
  cli_sinceRain_h     : REAL;
END_VAR
VAR
  seen   : BOOL := FALSE;   (* the station has answered at least once *)
  last   : UDINT;
  primed : BOOL := FALSE;
  (* SECONDS, with the sub-second part kept aside. In milliseconds this wraps
     after 49 days, and a 50-day drought would read as a fresh shower; in
     seconds it is 136 years. *)
  dry_s   : UDINT;
  rem_ms  : UDINT;
END_VAR
VAR_TEMP
  hours : REAL;
END_VAR
  IF GOOD(cli_wind_mps_qlt) AND GOOD(cli_rain_mmph_qlt) THEN seen := TRUE; END_IF;

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
    IF GOOD(cli_rain_mmph_qlt) THEN
      IF cli_rain_mmph > 0.0 THEN
        dry_s  := 0;
        rem_ms := 0;
      ELSE
        rem_ms := rem_ms + (sys_now_ms - last);
        IF rem_ms >= 1000 THEN
          dry_s  := dry_s + rem_ms / 1000;
          rem_ms := rem_ms MOD 1000;
        END_IF;
      END_IF;
    END_IF;
  ELSE
    primed := TRUE;
  END_IF;
  last := sys_now_ms;

  (* HOURS, which reads at both ends: 0.25 is a quarter of an hour ago and
     1200 is fifty days. *)
  hours := UDINT_TO_REAL(dry_s) / 3600.0;
  cli_sinceRain_h := hours;
END_PROGRAM
