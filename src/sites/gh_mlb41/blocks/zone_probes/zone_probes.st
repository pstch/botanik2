(* ZONE_PROBES, as ST. Mirrors src/sites/gh_mlb41/blocks/zone_probes/.

   The quality of each probe arrives as a plain BOOL, mirrored from the row by
   Vars::QualityMirror. That is the whole reason this block can be ST at all:
   a C++ block asks the bank whether a reading is believable, and ST has no
   way to ask anything. *)

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
  IF n = 3 THEN
    (* THE MEDIAN, per the spec: keep the middle reading and discard both
       extremes, so a probe stuck high or stuck low can never be the one
       retained. *)
    IF   (v1 <= v2 AND v2 <= v3) OR (v3 <= v2 AND v2 <= v1) THEN Y := v2;
    ELSIF (v2 <= v1 AND v1 <= v3) OR (v3 <= v1 AND v1 <= v2) THEN Y := v1;
    ELSE Y := v3;
    END_IF;
  ELSIF n = 2 THEN
    (* No middle to keep, and their mean is the only answer there is. *)
    Y := 0.5 * (v1 + v2);
  ELSIF n = 1 THEN
    (* Believed: a reading from one probe still beats none. *)
    Y := v1;
  ELSE
    Y := 0.0;
  END_IF;
END_FUNCTION_BLOCK


FUNCTION_BLOCK ZONE_PUBLISH
(* Filter, clamp and say whether anything measured it. The filter KEEPS its
   state while the zone is out, so a probe coming back rejoins the average it
   left rather than climbing out of a hole. *)
VAR_INPUT
  RAW   : REAL;
  USED  : USINT;
  TAU_S : REAL;
  NOW   : UDINT;
  LO    : REAL;
  HI    : REAL;
END_VAR
VAR_OUTPUT
  Y  : REAL;
  OK : BOOL;    (* this beat MEASURED it *)
END_VAR
VAR
  filt   : EWMA;
  seeded : BOOL := FALSE;
END_VAR
  IF USED = 0 THEN
    (* Nothing believable left. The row keeps the last average this zone could
       make and says it is not current: a reader gets the temperature the
       greenhouse last had rather than 0 degC, which reads like a measurement
       of a freezing house. *)
    OK := FALSE;
    IF NOT seeded THEN Y := LO; END_IF;
  ELSE
    seeded := TRUE;
    filt(X := RAW, TAU_S := TAU_S, NOW := NOW);
    Y  := LIMIT(LO, filt.Y, HI);
    OK := TRUE;
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
  t_pub  : ZONE_PUBLISH;
  h_pub  : ZONE_PUBLISH;
  quorum : TOF_MS;
END_VAR
VAR_TEMP
  worst : USINT;
  h_raw : REAL;
END_VAR
  t_vote(A := T1, A_OK := T1_OK, B := T2, B_OK := T2_OK, C := T3, C_OK := T3_OK);
  h_vote(A := H1, A_OK := H1_OK, B := H2, B_OK := H2_OK, C := H3, C_OK := H3_OK);

  (* The instruments report humidity in PERCENT and everything above this line
     works in fractions - the spec's convention for a percentage is a real
     number between 0 and 1. Converted HERE, once, at the boundary between the
     instrument and the model: published as a percent it clamped at 1.00 and
     every dew-point margin in the plant read zero. *)
  h_raw := h_vote.Y * 0.01;

  t_pub(RAW := t_vote.Y, USED := t_vote.USED, TAU_S := TAU_S, NOW := NOW,
        LO := -100.0, HI := 200.0);
  h_pub(RAW := h_raw,    USED := h_vote.USED, TAU_S := TAU_S, NOW := NOW,
        LO := 0.0,    HI := 1.0);

  TEMP    := t_pub.Y;
  RH      := h_pub.Y;
  TEMP_OK := t_pub.OK;
  RH_OK   := h_pub.OK;

  (* The alarm follows whichever quantity has lost the most: two probes still
     select correctly, so the plant keeps running and the operator is told to
     replace one before the second goes. Its own dwell, because one missed
     scan of RS-485 is the bus being busy rather than a probe that has gone. *)
  IF t_vote.USED < h_vote.USED THEN worst := t_vote.USED;
  ELSE worst := h_vote.USED; END_IF;
  quorum(IN := worst < 3, PT := 5000, NOW := NOW);
  FAULT := quorum.Q;
END_FUNCTION_BLOCK
