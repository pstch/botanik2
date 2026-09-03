# Control authority and the safe state {#user_control}

Whether the firmware may drive the field at all is **control authority**. Everything
that touches the outputs goes through it, and it has two states: the control programs
drive, or they do not.

Two **independent** sources decide, and the **stricter** one wins:

| Source | Set by | Scope |
|---|---|---|
| operator | the `sys_control` parameter, or `programs disable` | all programs, or one |
| system | the firmware's own lifecycle: bring-up, shutdown, a health fault | all programs |

That is why it is not a single setting. An operator who turned `sys_control` off
stays off across a health fault that comes and goes, and the console cannot talk over
the firmware's own judgement that it must not drive.

## What stopping a program does

A program taken out of the scan does **not** freeze what it was driving. Every row it
writes drops to its **declared safe state** - for the reference site, every relay coil
de-energised - and the firmware logs one line per program saying how many rows moved:

```
[!] HEALTH: irrigation: out of the scan, 9 row(s) safe
```

That happens on the **edge**, once, whichever source stopped it. The rows then read
back as invalid with the reason `dropped to its safe state`, so a stopped program is
visible on the MONITOR page rather than looking like live zeroes.

Nothing runs a law outside `RUN`, so the same drop happens on the way down at
shutdown, and control starts withheld at power-on: nothing part-way through bringing
the firmware up can energise an actuator on the way past.

## Taking control by hand

`sys_control` is an ordinary parameter, so it is read and set like any other:

```
botanik> params get sys_control
sys_control = 1
botanik> params set sys_control 0
botanik> programs status
```

One program at a time instead of all of them:

```
botanik> programs disable irrigation     # out of the scan, its rows go safe
botanik> programs disable *              # or a glob
botanik> programs status                 # how many, and which cannot run
botanik> programs enable irrigation
```

### Driving an actuator by hand

Stopping the programs does not take the outputs away from you, it takes them away
from the control programs. `overrides set` still reaches them, and with the scan
stopped there is nothing left to fight it:

```
botanik> params set sys_control 0
botanik> overrides set irr_tray1 1     # open tray 1's valve
botanik> overrides set irr_tray1 0     # close it
botanik> overrides clear               # hand everything back
botanik> params set sys_control 1
```

There is deliberately no stricter rung that mutes overrides while leaving them set.
It would differ in nothing else, and an override that stops driving without saying so
is worse than one that keeps driving visibly. `overrides clear` is how a force comes
off.

@warning Nothing supervises a hand-driven output. The interlocks live in the control
programs, and those are exactly what you just stopped. Release the overrides before
turning `sys_control` back on, or the programs come back to find their outputs already
held.

A **saved** override survives a reboot, and an output's is no exception: a restored
one is driving the actuator from the moment the firmware comes up. An override you
never saved dies with the session, which is the safer default. Either way the firmware
announces each restored one as a **critical** naming the row, and `overrides clear all`
before you leave is the habit worth having. See @ref user_variables.

## Restarting, and what the outputs do

The four lifecycle verbs all take `confirm` (@ref user_console). They differ in
whether the plant is parked on the way out:

| | Goes through the lifecycle | The outputs |
|---|---|---|
| `halt confirm` | yes | every program leaves the scan, its rows go safe |
| `reboot confirm` | yes | same, then the watchdog resets the unit |
| `reset confirm` | **no** | reset immediately; the firmware drops nothing |
| `exit confirm` | **no** | same (on hardware `exit` is a reset) |

`reboot` and `halt` move the unit out of `RUN` first, so `sys_shutdown_sec` runs, the
retained rows and owed parameters reach flash, and every program's rows are dropped
before anything stops. `reboot` then simply stops kicking the watchdog and lets it
reset the unit, which is the same path an unattended unit takes when it wedges.

`reset` and `exit` save the retained rows and go. Nothing is parked, so what the
outputs do is whatever the reset itself does to them. Prefer `reboot` on a plant that
is actually driving something.

## Reading a boot

Start-up ends with one self-test verdict on the `HEALTH` topic, so a boot is never
ambiguous:

```
[-] HEALTH: POST ok (site gh_mlb41)
```

or, on failure, one critical per failed check followed by:

```
[@] HEALTH: POST failed (site gh_mlb41)
```

The self-test covers structural wiring - that the alarm codes the firmware asserts
actually exist in the table, and that the logger has somewhere to write. It is not a
hardware diagnostic.

A failure is **loud but not fatal**. The unit keeps running so the diagnostics stay
readable over the terminal; a silently broken unit would be worse than a noisy one.
Expect `SYS_RST` to be active for the first five minutes after any restart - that is
the platform telling you it rebooted, not a problem in itself.
