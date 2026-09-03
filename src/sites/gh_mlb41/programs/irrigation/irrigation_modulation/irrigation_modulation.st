(* irrigation_modulation program
   ------------------------------------------------------------------------ *)

PROGRAM irrigation_modulation
VAR_EXTERNAL CONSTANT
  cli_vpd_kPa           : REAL;
  (* The VPD and the margin are one verdict: temp_rh_zone measures both or
     neither. *)
  cli_dpMargin_degC_qlt : QUALITY;
  irr_dryEwmaDays       : UINT;
  irr_dryLo_kPa         : REAL;
  irr_dryHi_kPa         : REAL;
  irr_modMin            : REAL;
  irr_modGamma          : REAL;
  sys_now_ms            : UDINT;
END_VAR
VAR_EXTERNAL
  irr_vpdAvg_kPa : REAL;
  irr_vpdAvg_kPa_qlt : QUALITY;
  irr_mod        : REAL;
  irr_mod_qlt    : QUALITY;
END_VAR
VAR
  avg       : REAL;
  folded_at : UDINT;
  started   : BOOL := FALSE;
  seeded    : BOOL := FALSE;
END_VAR
VAR_TEMP
  measured : BOOL;
  due      : BOOL;
  alpha    : REAL;
  demand   : REAL;
  min_mod  : REAL;
  gamma    : REAL;
END_VAR
  measured := GOOD(cli_dpMargin_degC_qlt);
  IF NOT started THEN
    folded_at := sys_now_ms;
    started   := TRUE;
  END_IF;

  (* Once an HOUR, and the interval is the whole design: folded every scan the
     increment underflows a float and the average freezes; folded once a day
     the answer depends on which hour it was taken.

     The clock is READ either way and only re-stamped on a fold, so an hour
     that came due blind folds the moment the probes answer instead of waiting
     out another one. *)
  due := (sys_now_ms - folded_at) >= UDINT#3600000 OR NOT seeded;

  (* Only a MEASURED sample is folded. The row KEEPS its last VPD while the
     probes are out, so folding it is the same number an hour apart - and a
     multi-day average pulled toward whatever the house was doing when it was
     last visible is worse than one that stopped moving, because it looks like
     weather. *)
  IF due AND measured THEN
    IF irr_dryEwmaDays = 0 THEN
      alpha := 1.0;
    ELSE
      alpha := 1.0 / (24.0 * UINT_TO_REAL(irr_dryEwmaDays));
    END_IF;
    IF seeded THEN
      avg := avg + alpha * (cli_vpd_kPa - avg);
    ELSE
      avg    := cli_vpd_kPa;
      seeded := TRUE;
    END_IF;
    folded_at := sys_now_ms;
  END_IF;

  (* Nothing folded yet, so there is no average to publish: an average of zero
     is the VPD of saturated air and would read as a wet week. *)
  IF seeded THEN
    irr_vpdAvg_kPa := avg;

    demand  := LADDER(V := avg, LO := irr_dryLo_kPa, HI := irr_dryHi_kPa);
    min_mod := LIMIT(0.0, irr_modMin, 1.0);
    (* Gamma is held at 1 or more so the curve is concave-up: watering reaches
       its summer figure only once dryness is really established, and every
       error in between is toward LESS water. *)
    gamma   := MAX(1.0, irr_modGamma);
    irr_mod := LIMIT(min_mod,
                     min_mod + (1.0 - min_mod) * POW(IN1 := demand, IN2 := gamma),
                     1.0);
  END_IF;

  (* A modulation nobody can read is no dose, and irr_schedule reads it that
     way: before the first fold there is nothing to modulate BY, and the row
     stays at the declared figure that covers the ordinary case. *)
  irr_mod_qlt        := QUAL(seeded AND measured);
  irr_vpdAvg_kPa_qlt := QUAL(seeded AND measured);
END_PROGRAM
