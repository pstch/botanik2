(* irr_mtr_output, as ST. Mirrors
   src/sites/gh_mlb41/programs/irrigation/irr_mtr_output/. *)

PROGRAM irr_mtr_output
VAR_EXTERNAL CONSTANT
  irr_out_Lpm        : REAL;
  irr_out_Lpm_qlt    : BOOL;
  irr_out_avg_ms     : UINT;
  irr_flowBand_Lpm   : REAL;
  irr_flowCutoff_Lpm : REAL;
  sys_now_ms         : UDINT;
END_VAR
VAR_EXTERNAL
  irr_outFlow_Lpm     : REAL;
  irr_outFlow_Lpm_qlt : BOOL;
  irr_outTotal_L      : REAL;
  irr_outTotal_L_qlt  : BOOL;
END_VAR
VAR
  flow_avg  : EWMA;
  flow_band : DEADBAND;
  meter     : INTEGRAL;
  seeded    : BOOL := FALSE;
  base_L    : REAL;
END_VAR
VAR_TEMP
  measured : BOOL;
  lpm      : REAL;
END_VAR
  measured := irr_out_Lpm_qlt;

  (* The retained row carries what was metered before this run, read ONCE:
     after that the block owns the total and the row is its output. *)
  IF NOT seeded THEN
    base_L := irr_outTotal_L;
    seeded := TRUE;
  END_IF;

  (* Only a MEASURED sample is folded in: the row keeps its last reading while
     the loop is out, and averaging that is the same number over and over. The
     filter KEEPS its state and its CLOCK, so a loop that comes back rejoins
     where it left rather than snapping. @see EWMA.OK_IN *)
  flow_avg(X := irr_out_Lpm, OK_IN := measured, NOW := sys_now_ms,
           TAU_S := UINT_TO_REAL(irr_out_avg_ms) / 1000.0, LO := 0.0, HI := 0.0);
  lpm := flow_avg.Y;

  IF measured THEN
    IF lpm < irr_flowBand_Lpm THEN
      (* WITHIN THE DEADBAND OF ZERO, so it reads zero: a reading the band
         cannot tell from nothing is nothing. Left to the block the row parks
         on the last tenth it reported - a drain meter saying 0.09 Lpm with
         every valve shut, which is what a leak looks like. *)
      irr_outFlow_Lpm := 0.0;
    ELSE
      flow_band(V := lpm, BAND := irr_flowBand_Lpm);
      irr_outFlow_Lpm := flow_band.Y;
    END_IF;
  END_IF;
  (* A flow derived from a loop that has stopped answering is not a flow, so
     the row is LEFT where it was and the companion is what says so. Zero would
     read as "no water is moving", which is what a leak detector must not be
     told. *)
  irr_outFlow_Lpm_qlt := measured;

  (* A totalizer is a record of what went past, so nothing is counted from a
     loop that is not answering: the last flow it reported, integrated, is
     invented water - and it is invented into the figure the drain verdict and
     the leak alarm are made against. The total STANDS, and says it is not
     current. *)
  meter(RATE_PER_S := lpm / 60.0,
        CUTOFF     := irr_flowCutoff_Lpm / 60.0,
        HOLD       := NOT measured,
        SEED       := base_L,
        NOW        := sys_now_ms);
  irr_outTotal_L := meter.TOTAL;
  (* The cycle reads both: a totaliser DIFFERENCE cannot take a substituted
     constant at either end, and the drain verdict declines to judge on a
     silent meter rather than reading the declared 0 Lpm as a clog. *)
  irr_outTotal_L_qlt := measured;
END_PROGRAM
