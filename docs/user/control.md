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

Three things, at once:

1. every output goes to its **declared safe state** - for the reference site, every
   relay coil de-energised;
2. any override holding an output is **released**, so a hand-forced actuator cannot
   outlive the withdrawal;
3. the scan is **inhibited** - the control programs stop running, so nothing
   re-drives the outputs on the next beat.

Control starts **disabled** at power-on and is enabled as the last step of start-up,
so nothing part-way through bringing the firmware up can energise an actuator on the
way past.

## Taking control by hand

```
botanik> control                 # report
control: enabled
botanik> control disable
control: DISABLED - outputs at their safe state, output overrides released
botanik> control enable
control: enabled
```

Use `control disable` before working on the plant. It is the one command that
guarantees the firmware is not about to move something.

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
