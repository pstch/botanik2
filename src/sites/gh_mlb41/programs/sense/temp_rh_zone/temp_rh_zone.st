(* temp_rh_zone program
   ------------------------------------------------------------------------ *)

PROGRAM temp_rh_zone
VAR_EXTERNAL CONSTANT
  cli_temp_hi        : REAL;
  cli_rh_hi          : REAL;
  cli_temp_lo        : REAL;
  cli_rh_lo          : REAL;
  cli_temp_hi_qlt    : QUALITY;
  cli_temp_lo_qlt    : QUALITY;
  cli_rotMargin_degC : REAL;
END_VAR
VAR_EXTERNAL
  cli_temp_in           : REAL;
  cli_temp_in_qlt       : QUALITY;
  cli_dewPoint_degC     : REAL;
  cli_dewPoint_degC_qlt : QUALITY;
  cli_dpMargin_degC     : REAL;
  cli_dpMargin_degC_qlt : QUALITY;
  cli_satPress_kPa      : REAL;
  cli_satPress_kPa_qlt  : QUALITY;
  cli_vpd_kPa           : REAL;
  cli_vpd_kPa_qlt       : QUALITY;
  alarm_rot             : BOOL;
END_VAR
VAR_TEMP
  dp_hi      : REAL;
  dp_lo      : REAL;
  margin_hi  : REAL;
  margin_lo  : REAL;
  vpd_hi     : REAL;
  vpd_lo     : REAL;
  hi_worst   : BOOL;
  worst_temp : REAL;
  measured   : BOOL;
END_VAR
  measured := GOOD(cli_temp_hi_qlt) OR GOOD(cli_temp_lo_qlt);

  (* ONE interior temperature, for everything that wants the house rather than
     a level of it - the cooling ladder included, which is why it is the HOTTER
     of the two and not their mean. The spec keeps the worst case between
     levels, and for heat that is the top of the house.

     Both levels gone: the rows are holding their last readings, so the hotter
     of them is still the best answer there is - held, like they are, and the
     companion is what says so. *)
  IF GOOD(cli_temp_hi_qlt) AND NOT GOOD(cli_temp_lo_qlt) THEN
    cli_temp_in := cli_temp_hi;
  ELSIF GOOD(cli_temp_lo_qlt) AND NOT GOOD(cli_temp_hi_qlt) THEN
    cli_temp_in := cli_temp_lo;
  ELSE
    cli_temp_in := MAX(cli_temp_hi, cli_temp_lo);
  END_IF;
  cli_temp_in_qlt := QUAL(measured);

  (* One dew point per level: the margin and the VPD both come off it. *)
  dp_hi := DEW_POINT(T_DEGC := cli_temp_hi, RH := cli_rh_hi);
  dp_lo := DEW_POINT(T_DEGC := cli_temp_lo, RH := cli_rh_lo);
  margin_hi  := cli_temp_hi - dp_hi;
  margin_lo  := cli_temp_lo - dp_lo;

  (* Between the two levels the WORST case is kept, per the spec: the smallest
     margin decides the condensation risk, and an average would hide a level
     already at the dew point. Among the levels that are MEASURED, so one
     holding a reading from before its probes died cannot speak for the house. *)
  IF GOOD(cli_temp_hi_qlt) = GOOD(cli_temp_lo_qlt) THEN
    hi_worst := margin_hi <= margin_lo;
  ELSE
    hi_worst := GOOD(cli_temp_hi_qlt);
  END_IF;
  IF hi_worst THEN
    cli_dewPoint_degC := dp_hi;
    cli_dpMargin_degC := margin_hi;
    worst_temp           := cli_temp_hi;
  ELSE
    cli_dewPoint_degC := dp_lo;
    cli_dpMargin_degC := margin_lo;
    worst_temp           := cli_temp_lo;
  END_IF;
  cli_dpMargin_degC_qlt := QUAL(measured);
  cli_dewPoint_degC_qlt := QUAL(measured);
  cli_satPress_kPa      := SAT_VAPOUR_PRESSURE(T_DEGC := worst_temp);
  cli_satPress_kPa_qlt  := QUAL(measured);

  (* The worst case is per QUANTITY, not one level chosen for all of them: the
     spec keeps the smallest margin for rot and the LOWEST VPD for irrigation,
     and they are not always the same level. A margin and a VPD tell the same
     story in different units, and the units are not linear in each other - the
     same margin is a much larger VPD when it is warm. *)
  vpd_hi := SAT_VAPOUR_PRESSURE(T_DEGC := cli_temp_hi)
          - SAT_VAPOUR_PRESSURE(T_DEGC := dp_hi);
  vpd_lo := SAT_VAPOUR_PRESSURE(T_DEGC := cli_temp_lo)
          - SAT_VAPOUR_PRESSURE(T_DEGC := dp_lo);
  IF GOOD(cli_temp_hi_qlt) = GOOD(cli_temp_lo_qlt) THEN
    cli_vpd_kPa := MIN(vpd_hi, vpd_lo);
  ELSIF GOOD(cli_temp_hi_qlt) THEN
    cli_vpd_kPa := vpd_hi;
  ELSE
    cli_vpd_kPa := vpd_lo;
  END_IF;
  cli_vpd_kPa_qlt := QUAL(measured);

  alarm_rot := cli_dpMargin_degC < cli_rotMargin_degC;
END_PROGRAM
