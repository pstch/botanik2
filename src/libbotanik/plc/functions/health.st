(* What a program says about its own answer
   ------------------------------------------------------------------------ *)

(* The codes are Plc::Health's, in its order: matiec numbers an enumeration
   0,1,2... in declaration order, and the two sides have to agree because the
   value IS the program's health row. matiec rejects explicit values here, so
   the order is the contract.

   Only a program that OPTS IN (`health` in programs.csv) has a row to write,
   and one that opts in owes a write to it on every path - which the generator
   checks off matiec's own write list, the same way it checks a quality.

   Every output is written on every scan, so this is never about whether a row
   moved. It is what the number MEANS. *)

TYPE
  HEALTH : (HEALTHY, DEGRADED, FAULTED);
END_TYPE

FUNCTION WORST_HEALTH : HEALTH
(* How a program that leans on another's answer inherits it: the weaker of the
   two. Spelled as comparisons rather than as a maximum, because the ordering
   of an enumerated type is not something to lean on. *)
VAR_INPUT
  A : HEALTH;
  B : HEALTH;
END_VAR
  IF A = FAULTED OR B = FAULTED THEN WORST_HEALTH := FAULTED;
  ELSIF A = DEGRADED OR B = DEGRADED THEN WORST_HEALTH := DEGRADED;
  ELSE WORST_HEALTH := HEALTHY; END_IF;
END_FUNCTION

FUNCTION VERDICT : HEALTH
(* The common shape, said once: whether the law could answer at all, and
   whether it trusted what it answered on.

   ANSWERED is asked first and separately because the two are not degrees of
   one thing: a law with no evidence at all writes its safe state, which is a
   different fact from a law that chose a fallback and stands behind it. *)
VAR_INPUT
  ANSWERED : BOOL;
  TRUSTED  : BOOL;
END_VAR
  IF NOT ANSWERED THEN VERDICT := FAULTED;
  ELSIF NOT TRUSTED THEN VERDICT := DEGRADED;
  ELSE VERDICT := HEALTHY; END_IF;
END_FUNCTION
