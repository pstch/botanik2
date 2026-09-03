# Variables {#user_variables}

Everything the unit exposes is a **variable**: a meter reading, a relay coil, a
value the control programs compute, a setting. All of them have a name, a type, a
unit and a plausibility band, and all are read the same way.

What differs is what a variable is *for*:

| Role | Behaviour |
|---|---|
| input | comes from the field; goes invalid past its timeout, or outside its band |
| output | drives the field; drops to its safe state when control is withdrawn |
| computed | worked out by the control programs; never expires |
| parameter | editable and saved (@ref user_params) |

A reading can be fresh and still be nonsense: a dead 4-20 mA loop reads full scale
for ever, and no timeout catches that. The band does.

## Reading one

`vars list` browses, optionally filtered. Variables are named by domain, so a
bare prefix is the subsystem view:

```
botanik> vars list grid_L1_
  grid_L1_V            237.4        V    (MI)
  grid_L1_A            7.6          A    (MI)
  2 variable(s)
```

A pattern carrying `*` or `?` matches the whole name rather than its start, for
the rows a prefix cannot gather:

```
botanik> vars list cli_win*_pos
  cli_window1_pos      0.0               (SV)
  cli_window2_pos      0.0               (SV)
  ...
```

Three annotations on a row matter:

- `(P)`, `(MI)`, `(RO)`, ... what kind of row it is, which is what decides whether
  `params set` or `overrides set` applies to it.
- `OVERRIDDEN` - forced by hand; the number is not the live one.
- `(default: nothing has written it)` - the number shown is the **declared
  default**, not a measurement. A dead sensor reads its fallback, not zero.

`vars get` is the value alone - one line, so it reads at a glance and a script can
take field 1:

```
botanik> vars get temp_zone1
35.0 C
```

It is the value every other reader sees, the override applied. The only thing that
rides along is `INVALID`, when the number is not a believable reading - a bare one
would be scraped as if it were.

`vars info` is the detail view of the same row:

```
botanik> vars info temp_zone1
temp_zone1 = 35.0 C
Scope ....................... : AI       (input, calibrated)
Group ....................... : temp
Access ...................... : read-only
Raw value ................... : 21.3 C   (valid)
Override value .............. : 35.0 C   (valid, unsaved)
Plausible ................... : -20.0 .. 60.0 C
Source ...................... : Modbus dev 11 fc 3 reg 4
Written by .................. : -        (no control program drives it)
Read by ..................... : climate_watch, cooling
Description ................. : zone 1 air temperature
```

**Raw value** is what the row reads underneath; **Override value** is what it is
being held at, or `-` when nothing is forced. That pair answers the question behind
releasing an override: has the sensor come back?

Three more lines appear where they have something to say:

- **Retained** - the row's value is written back to storage and survives a reboot.
  Only the rows whose value IS the history carry it: litres delivered, hours run.
- **Written by** - the control program that drives this row, `-` when none does (a
  field input is filled by the bus, and **Source** is what says so). A row declared
  shared between programs reads `several`.
- **Read by** - the programs that watch this row, and go degraded without it. Both
  are read off the running scan rather than a list anyone maintains, so they cannot
  go stale. A **parameter** is not watched - an operator's number is always valid -
  so it names no readers whoever reads it.

The MONITOR page shows the same set on screen, filtered by typing (@ref user_console).

### Why a reading is invalid

Both `vars info` and the MONITOR panel say *why*, because the causes want different
next moves:

| Reads | What happened | Where to look |
|---|---|---|
| `valid` | a real value from whoever owns the row | - |
| `invalid: nothing has written it` | never published | wiring, or a device that never answered |
| `invalid: past its timeout` | it was live and stopped | the device went away mid-run |
| `invalid: outside its band` | answering, with nonsense | the sensor: a dead 4-20 mA loop reads full scale |
| `invalid: dropped to its safe state` | an **output**, not a fault | control is withdrawn (@ref user_control) |

A relay reading invalid usually means the firmware is deliberately not driving it.

## Overrides {#user_variables_overrides}

An override forces a variable to a value you choose: fake a sensor to exercise the
logic watching it, freeze a computed value, drive an actuator by hand.

```
botanik> overrides set temp_zone1 35.0          # force a value
botanik> overrides set temp_zone1               # hold it where it is now
botanik> overrides set temp_zone1 35.0 invalid  # force it, and make it read as dead
botanik> overrides list                         # everything currently forced
botanik> overrides clear temp_zone1             # release it
botanik> overrides clear all                    # release the lot
botanik> overrides save all                     # keep what is forced across a reboot
```

A forced value reads as **good** unless you say `invalid`, the bare form included.

The MONITOR page does the same on the row under the cursor, without naming it: `^O`
forces, `^V` flips it between reading good and reading dead, `^R` releases and `^S`
saves that row (@ref user_console). The field `^O` opens takes the value alone -
validity is `^V`'s, and `save all` stays a shell thing.

What to know before using one:

- **Everything sees it.** Control programs, alarms, telemetry and the screen all
  read the forced value, so forcing a temperature really does make the programs act
  on it.
- **It shadows, it does not replace.** The driver keeps publishing underneath, so
  clearing snaps straight back to the live reading.
- **It dies at the next reboot unless you save it.** A new override is marked
  `(unsaved)`. Save the ones meant to stay - a failed sensor bypassed until the
  spare arrives - and leave the rest to expire with the session.
- **Only the type is checked**, not the plausibility band, so a reading can be
  pushed past what the site considers possible.

Releasing one forgets the stored copy too, so nothing comes back at the next boot.

@warning A **saved** override on an output is driving that actuator from the moment
the unit boots. Each restored override is announced as a critical naming the row.
Run `overrides list` before you walk away.

With the programs stopped (`sys_control` off, or `programs disable`) an override is
the only thing that can move an actuator, which is how you drive a valve by hand.
See @ref user_control.

## Parameters

A parameter is a variable you may edit, with two extras: it is range-checked, and it
can be saved. Everything above applies to it unchanged.

See @ref user_params for editing, saving, and the parameters the library itself
reads.
