# Control authority and the safe state {#user_control}

Whether the firmware may drive the field at all is **control authority**, and it
has three modes. Everything that touches the outputs goes through it.

| Mode | The programs | The outputs | An `override` on an output |
|---|---|---|---|
| `enable` | run | driven normally | drives |
| `inhibit` | stopped | at their safe state | **still drives** |
| `disable` | stopped | at their safe state | **ignored** |

Two **independent** sources decide which mode is in force, and the **stricter**
one wins:

| Source | Set by |
|---|---|
| operator | you, with `control enable` / `inhibit` / `disable` |
| system | the firmware: bring-up not finished, a health fault, a pending reset |

That is why it is not a single setting. An operator who typed `control disable`
stays disabled across a health fault that comes and goes, and equally the console
cannot talk over the firmware's own judgement that it must not drive. `control`
reports both, so an `enable` that changes nothing reads as a health fault rather
than a broken command.

## What withholding control does

Two things, at once, in both withheld modes:

1. every output goes to its **declared safe state** - for the reference site, every
   relay coil de-energised;
2. the scan is **inhibited** - the control programs stop running, so nothing
   re-drives the outputs on the next beat.

Control starts **disabled** at power-on and is enabled as the last step of start-up,
so nothing part-way through bringing the firmware up can energise an actuator on the
way past.

## Taking control by hand

```
botanik> control                 # report
control: enabled
botanik> control inhibit
control: INHIBITED - outputs at their safe state, scan stopped; `override` still drives them by hand
botanik> control disable
control: DISABLED - outputs at their safe state, scan stopped, output overrides ignored (kept, not cleared)
botanik> control enable
control: enabled
```

Use `control inhibit` before working on the plant: it stops the firmware deciding
anything while leaving the outputs reachable by hand. `control disable` is the
stricter one - nothing at all drives an output, not even you.

**A disabled override is muted, not deleted.** Going back to `inhibit` or `enable`
restores whatever you had forced, with nothing to re-enter. The one thing that
does erase output overrides is the *system* withholding control - a health fault
or a pending reset - because then nobody is necessarily watching, and an actuator
held energised by hand through a fault (or resurrected at the next boot) is
exactly what the safe state exists to prevent.

### Driving an actuator by hand

Inhibiting control does **not** take the outputs away from you - it takes them away
from the control programs. `override` still reaches them, and with the scan stopped
there is nothing left to fight it, so the two commands together are how you command a
valve or a fan by hand:

```
botanik> control inhibit
botanik> override irr_tray1 1        # open tray 1's valve
botanik> override irr_tray1 0        # close it
botanik> override clear              # hand everything back
botanik> control enable
```

That is the whole reason `inhibit` leaves overrides alone. Wiping them would leave
nothing at all able to move an actuator, which is the opposite of what someone
standing at the machine with a wrench needs. Reach for `disable` when you want the
opposite guarantee: that nothing moves, including you.

@warning Nothing supervises a hand-driven output. The interlocks live in the control
programs, and those are exactly what you just stopped. Release the overrides before
`control enable`, or the programs come back to find their outputs already held.

Overrides survive a reboot, and an output's is no exception - a restored one is
driving the actuator from the moment the firmware comes up. The firmware announces
each restored override as a **critical** naming the row, but `override clear` before
you leave is the habit worth having. See @ref user_variables.

`control enable` releases the **operator's** hold and nothing else, then reports what
is left. So a `control enable` that changes nothing reads as a health fault rather
than as a broken command:

```
botanik> control enable
control: DISABLED by the system (bring-up, health, or a pending reset)
```

## When the firmware withdraws it

Control is withdrawn automatically on a health fault, logged as a critical on the
`HEALTH` topic:

```
[@] HEALTH: control disabled
                        > outputs at their safe state, scan inhibited
```

Recovery logs a warning and hands control back:

```
[!] HEALTH: health recovered
                        > control enabled, scan resuming
```

(or `control still disabled by the operator` on the second line, if you also
hold it down by hand.)

This is edge-triggered on the fault, not re-decided every beat, and it moves only the
**health** inhibit. A recovery therefore hands control back only if you had not also
disabled it by hand: a fault that comes and goes while you are working on the plant
leaves your hold exactly where you put it.

The firmware's own withdrawal is the stricter of the two, and it is the one case that
**does release** an override holding an output. It runs when the firmware can no
longer reason about what it is driving, or is about to stop running altogether, and
nobody is necessarily watching - an actuator held energised by hand through that is
the one outcome the safe state exists to prevent. So a health fault will take a
hand-driven valve off you, and keep it off across a reboot; the operator's own
`control disable` only mutes it.

`reset` and `exit` also drop the outputs to safe state before leaving, because the
reboot itself gives no guarantee about what the outputs do on the way through.

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
