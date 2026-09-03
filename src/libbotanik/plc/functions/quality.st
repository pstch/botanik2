(* What a reading is worth, and the two ways a law asks
   ------------------------------------------------------------------------ *)

(* The codes are Vars::Quality's, in its order: matiec numbers an enumeration
   0,1,2... in declaration order, and the two sides have to agree because the
   value IS the companion row's. matiec rejects explicit values here, so the
   order is the contract. *)

TYPE
  QUALITY : (Q_NO_DATA, Q_VALID, Q_STALE, Q_IMPLAUSIBLE, Q_REFUSED);
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
(* The quality of a law that has no input to inherit from: it either produced
   the value or it did not. *)
VAR_INPUT
  OK : BOOL;
END_VAR
  IF OK THEN QUAL := Q_VALID; ELSE QUAL := Q_NO_DATA; END_IF;
END_FUNCTION
