(* intractor_controller, as ST. Mirrors
   src/sites/gh_mlb41/programs/actuators/intractor_controller/. *)

PROGRAM intractor_controller
VAR_EXTERNAL CONSTANT
  cli_coolDem        : REAL;
  cli_rotVentDem     : REAL;
  cli_humidDem       : REAL;
  cli_groundSpeed    : REAL;
  cli_intakeStartDem : REAL;
  cli_intakeFullDem  : REAL;
  cli_intakeCoolOk   : BOOL;
  cli_groundHeatOk   : BOOL;
  cli_intakeDryOk    : BOOL;
  cli_intrSafeSpeed  : REAL;
END_VAR
VAR_EXTERNAL
  cli_intr1 : REAL;
  cli_intr2 : REAL;
END_VAR
VAR_TEMP
  cooling : REAL;
  drying  : REAL;
  wetting : REAL;
  demand  : REAL;
  speed   : REAL;
END_VAR
  (* Three demands, each gated by the law that owns the reading behind it.
       - cooling, while the intake really is cooler
       - the anti-rot ventilation, ungated: rot is the collection, and the dew
         point it fights is the one inside
       - the DEHUMIDIFY arm alone, while the air outside is drier. Positive is
         asking for the skirt, and more air would work against it *)
  IF cli_intakeCoolOk THEN cooling := cli_coolDem; ELSE cooling := 0.0; END_IF;
  IF cli_intakeDryOk  THEN drying  := -cli_humidDem; ELSE drying := 0.0; END_IF;

  (* ...and the OTHER arm too. Too little humidity is answered by the skirt and
     these fans TOGETHER: the air they draw comes in through the wetted
     textile, which is the only humidifier the plant has. Ungated - the point
     is to bring moisture in, so "is it drier outside" is the wrong question
     for this arm. *)
  wetting := MAX(0.0, cli_humidDem);

  demand := MAX(MAX(cooling, cli_rotVentDem), MAX(drying, wetting));

  (* The FLOOR, and it is a SPEED rather than a demand: some air is always
     wanted, and the row is where this actuator's safe state is declared. A
     demand below the start threshold simply does not reach it. *)
  speed := MAX(cli_intrSafeSpeed,
               LIMIT(0.0,
                     (demand - cli_intakeStartDem)
                       / (cli_intakeFullDem - cli_intakeStartDem),
                     1.0));

  (* Ground heat is ONE RUNG, not a ramp: the spec's negative half asks for low
     speed at -0.1 and says nothing about wanting more of it the colder it
     gets. Pulling harder on a cold house moves more air through the same
     ground, cooling the ground rather than warming the house. *)
  IF cli_groundHeatOk THEN speed := MAX(speed, cli_groundSpeed); END_IF;

  cli_intr1 := speed;
  cli_intr2 := speed;
END_PROGRAM
