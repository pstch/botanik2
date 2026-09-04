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

## Nothing on this unit authenticates anybody

**The panel is behind a locked door on a segment that carries no traffic from
outside the holding. Physical and network isolation ARE the access control, and
no second one is implemented anywhere.** That is a deliberate call, not an
omission, and it is why there is no password, no PIN, no role and no session.

Three surfaces follow from it, and all three are open by design:

- **The console.** Anyone who can reach the RS-232 socket gets the whole shell:
  `params set`, `overrides set` (which will drive an output whatever control
  authority says - see `Bank::override_set`), `programs disable`, `reboot` and
  `halt`. The only guard is `_confirmed()`, which asks the four lifecycle verbs
  to be typed with `confirm` - a defence against a slip, not against a person.
- **The Modbus TCP server.** It answers any peer that connects, with no source
  allowlist and no unit-id check beyond the map's own. Its single gate is
  `mb_write_enable`, which is global: writes are on for everyone or off for
  everyone. `TcpSlave` bounds what a stranger can do (only mapped registers,
  only through `param_check()`), and that bound is the real protection.
- **MQTT.** Plaintext, no credentials, no TLS. Publish-only, so the broker
  cannot command anything - the worst a hostile broker gets is the plant's
  readings, and the worst a hostile network does is drop them.

**What would end the decision:** a routable plant network, a second tenant on
the segment, remote access of any kind, or a site where the panel is not behind
a lock. Any one of those and this entry stops being true, which is why the
consequences are spelled out above rather than left as "it is fine".

Two things exist BECAUSE there is no login, and should not be removed on the
grounds that nobody is authenticated:

- **The audit line.** `Bank::param_set()` (text form) and `Bank::override_set()`
  / `override_clear()` each write one `USER` notice saying what changed and what
  it read before. With no identity to record, WHAT happened and WHEN is the
  whole of the account, and an override restored at boot is logged `CRITICAL`
  for the same reason: it explains behaviour nothing else does.
- **`mb_write_enable` is the one switch worth knowing about.** It decides
  whether the plant network is a readable surface or a writable one, and it
  **defaults to 1** (`vars/tables/library.csv`, reg 953) - which only follows
  from the isolation above. On a site where that stops holding, this is the
  first row to change, and it is a parameter rather than a build flag precisely
  so it can be changed without a reflash.
