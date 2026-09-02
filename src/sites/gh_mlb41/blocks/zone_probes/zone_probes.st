(* One climate zone: three probes voted, filtered and published
   ------------------------------------------------------------------------ *)

FUNCTION_BLOCK ZONE_VOTE
(* The median of up to three readings, EXCLUDING the ones nothing measured.
   An unbelievable probe reads its declared default, which is outside the band
   and would drag an average even where the outlier rule dropped it. *)
VAR_INPUT
  A : REAL;   A_OK : BOOL;
  B : REAL;   B_OK : BOOL;
  C : REAL;   C_OK : BOOL;
END_VAR
VAR_OUTPUT
  Y    : REAL;    (* the vote *)
  USED : USINT;   (* how many probes it stands on *)
END_VAR
VAR_TEMP
  v1 : REAL;
  v2 : REAL;
  v3 : REAL;
  n  : USINT;
END_VAR
  n := 0;
  IF A_OK THEN n := n + 1; v1 := A; END_IF;
  IF B_OK THEN
    IF n = 0 THEN v1 := B; ELSE v2 := B; END_IF;
    n := n + 1;
  END_IF;
  IF C_OK THEN
    IF   n = 0 THEN v1 := C;
    ELSIF n = 1 THEN v2 := C;
    ELSE v3 := C; END_IF;
    n := n + 1;
  END_IF;

  USED := n;
  IF   n = 3 THEN Y := MEDIAN3(A := v1, B := v2, C := v3);
  ELSIF n = 2 THEN Y := AVG2(A := v1, B := v2);
  (* Believed: a reading from one probe still beats none. *)
  ELSIF n = 1 THEN Y := v1;
  ELSE Y := 0.0;
  END_IF;
END_FUNCTION_BLOCK


FUNCTION_BLOCK ZONE_PROBES
(* One zone: three temperature probes, three humidity probes, each quantity
   carrying its OWN quorum. Coupling them would let one dead humidity probe
   invalidate a temperature three good probes agree on. *)
VAR_INPUT
  T1 : REAL;  T1_OK : BOOL;
  T2 : REAL;  T2_OK : BOOL;
  T3 : REAL;  T3_OK : BOOL;
  H1 : REAL;  H1_OK : BOOL;
  H2 : REAL;  H2_OK : BOOL;
  H3 : REAL;  H3_OK : BOOL;
  TAU_S : REAL;
  NOW   : UDINT;
END_VAR
VAR_OUTPUT
  TEMP    : REAL;
  RH      : REAL;
  TEMP_OK : BOOL;   (* the zone MEASURED its temperature this beat *)
  RH_OK   : BOOL;   (* ...and its humidity, which is a SEPARATE quorum *)
  FAULT   : BOOL;   (* fewer than three probes, dwelt *)
END_VAR
VAR
  t_vote : ZONE_VOTE;
  h_vote : ZONE_VOTE;
  t_pub  : EWMA;
  h_pub  : EWMA;
  quorum : TON_MS;
END_VAR
VAR_TEMP
  worst : USINT;
  h_raw : REAL;
END_VAR
  t_vote(A := T1, A_OK := T1_OK, B := T2, B_OK := T2_OK, C := T3, C_OK := T3_OK);
  h_vote(A := H1, A_OK := H1_OK, B := H2, B_OK := H2_OK, C := H3, C_OK := H3_OK);

  (* The instruments report humidity in PERCENT and every law works in
     fractions. Converted once, here, where the instrument meets the model:
     a percent published as a fraction clamps at 1.00, and every dew-point
     margin in the plant then reads zero. *)
  h_raw := h_vote.Y * 0.01;

  t_pub.X     := t_vote.Y;
  t_pub.OK_IN := t_vote.USED > 0;
  t_pub.TAU_S := TAU_S;
  t_pub.NOW   := NOW;
  t_pub.LO    := -100.0;
  t_pub.HI    := 200.0;
  t_pub();
  h_pub.X     := h_raw;
  h_pub.OK_IN := h_vote.USED > 0;
  h_pub.TAU_S := TAU_S;
  h_pub.NOW   := NOW;
  h_pub.LO    := 0.0;
  h_pub.HI    := 1.0;
  h_pub();

  TEMP    := t_pub.Y;
  RH      := h_pub.Y;
  TEMP_OK := t_pub.OK;
  RH_OK   := h_pub.OK;

  (* The alarm follows whichever quantity has lost the most: two probes still
     select correctly, so the plant keeps running and the operator is told to
     replace one before the second goes.

     A DWELL, so the shortfall has to persist: one missed scan of the field bus
     is the bus being busy rather than a probe that has gone, and at bring-up
     no probe has answered yet. *)
  IF t_vote.USED < h_vote.USED THEN worst := t_vote.USED;
  ELSE worst := h_vote.USED; END_IF;
  quorum(IN := worst < 3, PT := 5000, NOW := NOW);
  FAULT := quorum.Q;
END_FUNCTION_BLOCK
