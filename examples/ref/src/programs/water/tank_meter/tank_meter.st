(* tank_meter program
   ------------------------------------------------------------------------ *)

PROGRAM tank_meter
VAR_EXTERNAL CONSTANT
  tank_flow_Lpm : REAL;
  bus_dc_V1     : REAL;
  bus_dc_V2     : REAL;
  bus_dc_min    : REAL;
  vent_pos      : REAL;
END_VAR
VAR_EXTERNAL
  tank_inTotal_L  : REAL;
  tank_outTotal_L : REAL;
  bus_dcLow      : BOOL;
END_VAR
  (* One control scan is SITE_CONTROL_MS, so litres per minute divided by the
     number of scans in a minute is litres this scan. A meter that reads zero
     costs nothing, which is why there is no gate around this. *)
  tank_inTotal_L := tank_inTotal_L + tank_flow_Lpm / 600.0;

  (* What the vent's cooling has evaporated off, which is the only way
     out of this tank. Proportional to how hard the vent is working. *)
  tank_outTotal_L := tank_outTotal_L + vent_pos / 6000.0;

  (* Either rail under the floor: the pair is one supply, and half of it
     failing is the whole of it worth annunciating. *)
  bus_dcLow := (bus_dc_V1 < bus_dc_min) OR (bus_dc_V2 < bus_dc_min);
END_PROGRAM
