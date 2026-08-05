# Control authority and the safe state {#user_control}

Whether the firmware may drive the field at all is one latch: **control authority**.
Everything that touches the outputs goes through it.

It is held down by two **independent** inhibits, and control returns only when both
let go:

| Inhibit | Held by |
|---|---|
| operator | you, with `control disable` |
| health | the firmware: bring-up not finished, a health fault, a pending reset |

That is why it is not a single flag. An operator who typed `control disable` stays
disabled across a health fault that comes and goes, and equally the console cannot
talk over the firmware's own judgement that it must not drive.

## What withdrawing control does

Two things, at once:

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
botanik> control disable
control: DISABLED - outputs at their safe state, scan inhibited; `override` still drives them by hand
botanik> control enable
control: enabled
```

Use `control disable` before working on the plant. It is the one command that
guarantees the firmware is not about to move something on its own.

### Driving an actuator by hand

Disabling control does **not** take the outputs away from you - it takes them away
from the control programs. `override` still reaches them, and with the scan stopped
there is nothing left to fight it, so the two commands together are how you command a
valve or a fan by hand:

```
botanik> control disable
botanik> override irr_tray1 1        # open tray 1's valve
botanik> override irr_tray1 0        # close it
botanik> override clear              # hand everything back
botanik> control enable
```

That is the whole reason the operator's inhibit leaves overrides alone. Wiping them
would leave nothing at all able to move an actuator, which is the opposite of what
someone standing at the machine with a wrench needs.

@warning Nothing supervises a hand-driven output. The interlocks live in the control
programs, and those are exactly what you just stopped. Release the overrides before
`control enable`, or the programs come back to find their outputs already held.

An override on an output is deliberately **not** persisted: it dies with the session
rather than commanding the plant again at the next boot. Overrides on everything else
- readings, computed values, parameters - do survive a reboot. See
@ref user_variables.

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
[@] HEALTH: control disabled: outputs at their safe state, scan inhibited
```

Recovery logs a warning and hands control back:

```
[!] HEALTH: control enabled, scan resuming
```

This is edge-triggered on the fault, not re-decided every beat, and it moves only the
**health** inhibit. A recovery therefore hands control back only if you had not also
disabled it by hand: a fault that comes and goes while you are working on the plant
leaves your hold exactly where you put it.

The firmware's own withdrawal is the stricter of the two, and it is the one case that
**does release** an override holding an output. It runs when the firmware can no
longer reason about what it is driving, or is about to stop running altogether, and
nobody is necessarily watching - an actuator held energised by hand through that is
the one outcome the safe state exists to prevent. So a health fault will take a
hand-driven valve off you; `control disable` will not.

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
