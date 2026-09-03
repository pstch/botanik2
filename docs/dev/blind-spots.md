# What the plant cannot see, on purpose {#dev_blind_spots}

Every entry here is a DECISION that reads like an omission. Each one was raised
as a finding by a review of the ST that had no way to tell the difference, which
is the argument for the file existing: an unwritten decision gets re-litigated
every time someone competent looks at the code.

Add to it when the answer to "why is there no check for X?" is "because we chose
not to", rather than "nobody has done it yet". The second kind belongs in
`TODO.md`.

## Actuator faults are undetectable

**The roof rams and the screen drive have internal limit switches, and no fault
contact is brought back to the controller. That is a cost decision, taken
deliberately.**

The consequence is that a jammed ram, a dropped contactor and a snapped linkage
are indistinguishable from a working one. `ACTUATOR` estimates position by
timing the strokes, so `cli_windowN_pos` will keep reporting an estimate for a
ram that has not moved in a week. **No actuator alarm is possible**, and there
is none in `tables/alarms.csv`. The only window-related entry is
`alarm_weather_storm`, whose operator note asks a human to go and check that the
windows are properly shut - which is the system saying, correctly, that it
cannot.

Two things follow, and both are visible in the code:

- **`ACTUATOR.DRIFT` is the only diagnostic there is.** It counts travel since
  the last datum, and `windows_controller` and `screen_controller` publish it
  per drive. A ram whose drift climbs while its three siblings reset to zero is
  the closest thing to a failure indication this plant can produce. That is why
  the rows exist despite nothing reading them: they are for a person.
- **`SEAT_MS` is safe BECAUSE of the limit switches.** Every stroke to either
  end deliberately over-runs the estimate to re-datum against the mechanical
  stop. On a drive without a limit switch that would be a stall; here the switch
  cuts the motor and the over-run is free.

`cli_windowRedatum` is the operator's handle on the same problem: past that much
accumulated travel, the next partial move detours to fully shut first, so the
estimate is re-established against the stop rather than drifting for ever.

## `cli_phase_qlt` is about provenance, not validity

**The phase value is never invalid.** `day_phase_controller` HOLDS the phase
when `sol_ready` is false and initialises it to `PHASE_NIGHT`, so every consumer
always reads a legal `DayPhase` code. Nothing can read a phase that is not one
of the six.

What the companion carries is where the phase CAME FROM: "the sun put us here"
against "this is NIGHT because the tracker has no location or no wall clock
yet". With an on-board RTC and an SNTP sync the second case is a short window at
boot - but it is also every case where the tracker stops computing mid-run, and
then the phase and `sol_fromRise_min` freeze together.

**The rule the four consumers follow: gate on the companion where the boot
default would cause a WRONG ACTION, not where it causes NO action.**

| consumer | gates on it? | why |
|---|---|---|
| `target_temp_controller` | yes | an unplaced NIGHT would apply a cold-bank offset - a wrong action |
| `irrigation_schedule` | yes | a tracker lost inside the morning window freezes the slot OPEN, all day and into the night |
| `fan_controller` | no | its breeze runs in phases 2 and 5 only, so NIGHT does nothing |
| `screen_controller` | no | NIGHT deploys the fabric, which is its declared safe state anyway |

The two that do not gate are safe in NIGHT by construction, and that is the
reason - not an oversight. A fifth consumer has to answer the same question
before it is written.
