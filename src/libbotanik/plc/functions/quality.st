(* What a reading is worth, and the two ways a law asks
   ------------------------------------------------------------------------ *)

(* The codes are Vars::Quality's, in its order: matiec numbers an enumeration
   0,1,2... in declaration order, and the two sides have to agree because the
   value IS the companion row's. matiec rejects explicit values here, so the
   order is the contract. *)

TYPE
  QUALITY : (Q_NO_DATA, Q_VALID, Q_STALE, Q_IMPLAUSIBLE, Q_REFUSED,
             Q_SUBSTITUTED);
END_TYPE

FUNCTION GOOD : BOOL
(* Whether Q is believable. The common read, so it does not spell out the
   comparison at forty call sites. *)
VAR_INPUT
  Q : QUALITY;
END_VAR
  GOOD := Q = Q_VALID;
END_FUNCTION

FUNCTION WORST_OF : QUALITY
(* How a derived row inherits its inputs': the first that is not believable,
   so the reason reaches the reader rather than a bare "no". A is asked first,
   which makes the result deterministic when both are bad. *)
VAR_INPUT
  A : QUALITY;
  B : QUALITY;
END_VAR
  IF A <> Q_VALID THEN WORST_OF := A; ELSE WORST_OF := B; END_IF;
END_FUNCTION

FUNCTION QUAL : QUALITY
(* The quality of a row a law publishes with no input verdict to inherit:
   measured this beat, HOLDING something it measured once, or nothing it has
   ever seen.

   SEEDED is asked separately and always, because the middle case is the one
   that gets skipped: a block whose output holds its last reading and reports
   only OK publishes Q_NO_DATA for a row that has real evidence in it, and
   Ref::get() then hands every reader the declared value instead. Pass FALSE
   where the law genuinely holds nothing - a verdict recomputed each scan -
   and let that be a claim somebody made rather than a default. *)
VAR_INPUT
  OK     : BOOL;
  SEEDED : BOOL;
END_VAR
  IF OK THEN QUAL := Q_VALID;
  ELSIF SEEDED THEN QUAL := Q_SUBSTITUTED;
  ELSE QUAL := Q_NO_DATA; END_IF;
END_FUNCTION
