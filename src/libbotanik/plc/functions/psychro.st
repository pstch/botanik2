(* Psychrometrics: dew points, and the margin to them
   ------------------------------------------------------------------------ *)

(* DEW POINTS, not humidities: air at 90% and 5 degC is drier than air at 60%
   and 25, and swapping one for the other is what ventilating does. *)

FUNCTION DEW_POINT : REAL
(* The temperature at which this air saturates and its vapour begins
   condensing onto whatever it touches. Magnus, valid -40..+50 degC.
   RH is a FRACTION, 0..1, never a percentage. *)
VAR_INPUT
  T_DEGC : REAL;
  RH     : REAL;
END_VAR
VAR
  gamma : REAL;
END_VAR
  (* The floor keeps the logarithm defined: bone-dry air is not a reason to
     return minus infinity to a control law. *)
  gamma := LN(LIMIT(0.05, RH, 1.0)) + (17.62 * T_DEGC) / (243.12 + T_DEGC);
  DEW_POINT := (243.12 * gamma) / (17.62 - gamma);
END_FUNCTION

FUNCTION DEW_POINT_MARGIN : REAL
(* How far the air is from condensing, in degC. LARGER is drier. What the rot
   and condensation laws are set in, because it is the margin that decides
   whether water lands on a plant at the temperature it is actually at. *)
VAR_INPUT
  T_DEGC : REAL;
  RH     : REAL;
END_VAR
  DEW_POINT_MARGIN := T_DEGC - DEW_POINT(T_DEGC := T_DEGC, RH := RH);
END_FUNCTION

FUNCTION SAT_VAPOUR_PRESSURE : REAL
(* The most water this air could hold, in kPa. Tetens, whose coefficients are
   NOT Magnus's above: mixing the two sets shifts the answer. *)
VAR_INPUT
  T_DEGC : REAL;
END_VAR
  SAT_VAPOUR_PRESSURE := 0.6108 * EXP((17.27 * T_DEGC) / (T_DEGC + 237.3));
END_FUNCTION
