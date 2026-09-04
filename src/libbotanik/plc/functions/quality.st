(* What a reading is worth, and the two ways a law asks
   ------------------------------------------------------------------------ *)

(* The codes are Vars::Quality's, in its order: matiec numbers an enumeration
   0,1,2... in declaration order, and the two sides have to agree because the
   value IS the companion row's. matiec rejects explicit values here, so the
   order is the contract.

   Three, and each is a DECISION a reader makes differently: nothing behind the
   row, something measured, or a number the law stands behind though nothing
   measured it. The reasons a reading went bad - stale, out of band, refused -
   are diagnoses rather than decisions, and each has a home of its own: the
   stamp's age, the band beside the value, and one log line from the master. *)

TYPE
  QUALITY : (Q_NONE, Q_GOOD, Q_HELD);
END_TYPE

FUNCTION GOOD : BOOL
(* Whether something MEASURED it. What a law gating on seeing the plant asks. *)
VAR_INPUT
  Q : QUALITY;
END_VAR
  GOOD := Q = Q_GOOD;
END_FUNCTION

FUNCTION USABLE : BOOL
(* Whether there is a NUMBER behind it worth computing on. Wider than GOOD by
   exactly Q_HELD, which is a law's own fallback. *)
VAR_INPUT
  Q : QUALITY;
END_VAR
  USABLE := Q <> Q_NONE;
END_FUNCTION

FUNCTION WORST_OF : QUALITY
(* How a derived row inherits its inputs': the weaker of the two. Spelled as
   comparisons rather than as a minimum, because the ordering of an enumerated
   type is not something to lean on. *)
VAR_INPUT
  A : QUALITY;
  B : QUALITY;
END_VAR
  IF A = Q_NONE OR B = Q_NONE THEN WORST_OF := Q_NONE;
  ELSIF A = Q_HELD OR B = Q_HELD THEN WORST_OF := Q_HELD;
  ELSE WORST_OF := Q_GOOD; END_IF;
END_FUNCTION

FUNCTION QUAL : QUALITY
(* The quality of a row a law publishes with no input verdict to inherit:
   measured this beat, HOLDING something it measured once, or nothing it has
   ever seen.

   SEEDED is asked separately and always, because the middle case is the one
   that gets skipped: a block whose output holds its last reading and reports
   only OK publishes Q_NONE for a row that has real evidence in it, and
   Ref::get() then hands every reader the declared value instead. Pass FALSE
   where the law genuinely holds nothing - a verdict recomputed each scan - and
   let that be a claim somebody made rather than a default. *)
VAR_INPUT
  OK     : BOOL;
  SEEDED : BOOL;
END_VAR
  IF OK THEN QUAL := Q_GOOD;
  ELSIF SEEDED THEN QUAL := Q_HELD;
  ELSE QUAL := Q_NONE; END_IF;
END_FUNCTION
