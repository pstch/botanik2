(* One tray's place in the irrigation schedule
   ------------------------------------------------------------------------ *)

(* A FUNCTION_BLOCK because ST has no array over VAR_EXTERNAL rows - a row is
   named, not indexed - so the interval arithmetic is written once and
   instanced eight times rather than unrolled eight ways. *)

FUNCTION_BLOCK TRAY_DUE
VAR_INPUT
  DAYS      : UINT;    (* the summer interval; 0 is "not scheduled" *)
  LAST      : UDINT;   (* the day it was last watered; 0 is never *)
  FORCE_OFF : BOOL;
  DAY       : UDINT;   (* today; 0 is "the clock has never been set" *)
  STRETCH   : REAL;
END_VAR
VAR_OUTPUT
  DUE      : BOOL;
  OVERDUE  : UDINT;   (* days past its interval, so a backlog drains in order *)
  INTERVAL : REAL;    (* what the weather made of the summer figure *)
  SKIP     : BOOL;    (* it was due, and held back by hand *)
END_VAR
VAR_TEMP
  summer    : REAL;
  due_after : UDINT;
  elapsed   : UDINT;
END_VAR
  DUE      := FALSE;
  SKIP     := FALSE;
  OVERDUE  := 0;
  INTERVAL := 0.0;

  (* No date, no schedule: a tray is due against a DAY, and there is not one
     until the clock has been set. A force is the way past that, and it is the
     program's to honour. *)
  IF DAY > 0 AND DAYS > 0 THEN
    summer := UINT_TO_REAL(DAYS);
    (* Damper air stretches the interval; drier air shortens it back toward the
       summer figure. *)
    IF STRETCH > 0.0 THEN INTERVAL := summer / STRETCH; ELSE INTERVAL := summer; END_IF;
    (* One day is the floor: a cycle is a day's work. *)
    IF INTERVAL < 1.0 THEN due_after := 1;
    ELSE due_after := REAL_TO_UDINT(INTERVAL + 0.5);
    END_IF;

    (* A tray never served has no history to be overdue against, so it goes
       first; one whose stored day is in the FUTURE is a clock that moved
       backwards, and waiting for it to catch up would strand the tray. *)
    IF LAST = 0 OR LAST > DAY THEN elapsed := due_after;
    ELSE elapsed := DAY - LAST;
    END_IF;

    IF elapsed >= due_after THEN
      (* Held back by hand, and ONCE: the skip is spent on the watering it was
         due for, and the tray goes to the back of the queue rather than
         arriving again tomorrow with the flag gone. *)
      IF FORCE_OFF THEN
        SKIP := TRUE;
      ELSE
        DUE     := TRUE;
        OVERDUE := elapsed - due_after;
      END_IF;
    END_IF;
  END_IF;
END_FUNCTION_BLOCK
