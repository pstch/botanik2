# Variables and overrides {#user_variables}

Everything the firmware exposes is a **variable**: a meter reading, a relay coil, a
value the control programs compute, a setting. They all have a name, a type, a unit,
a description and a plausibility band, and they are all read the same way.

What differs is what a variable is *for*:

| Role | Behaviour |
|---|---|
| input | comes from the field; goes invalid if the driver stops refreshing it, or if the reading leaves its declared band |
| output | drives the field; drops to its safe state when control is withdrawn |
| computed | worked out by the control programs; never expires |
| parameter | a setting: editable and persisted (see @ref user_settings) |

An input is invalidated two ways, and the second one matters more than it sounds: a
reading can be perfectly fresh and still be nonsense. A dead 4-20 mA loop reads full
scale forever, which no timeout catches - the plausibility band does.

## Reading one

`get` prints any variable; MONITOR shows them grouped with detail
(see @ref user_pages).

```
botanik> get pwr_L1_V
  pwr_L1_V             237.4        V
```

Three annotations matter:

- `(param)` - this is a setting, editable with `set`.
- `OVERRIDDEN` - someone has forced it by hand; the number is not the live one.
- `(default: nothing has written it)` - no driver has ever published this variable,
  or it timed out. The number shown is the **declared default**, not a measurement.

That last one is the important one to recognise. A dead sensor does not read as
zero-because-broken; it reads as whatever the site declared as its fallback, and the
firmware tells you so.

## Overrides

An override forces a variable to a value you choose. It is a maintenance tool: fake
a sensor to exercise the logic watching it, freeze a computed value, or drive an
actuator by hand.

```
botanik> override temp_zone1 35.0       # force a value
botanik> override temp_zone1            # freeze it at whatever it reads now
botanik> override temp_zone1 35.0 invalid   # force the value AND make it read as dead
botanik> override temp_zone1 off        # release it
botanik> override list                  # list everything currently forced
botanik> override off                   # release everything
```

Four things are worth understanding:

**An override is seen by everything.** It is applied at the single point every
reader goes through, so the control programs, the console, the alarm model and MQTT
telemetry all see the forced value. Forcing a temperature really will make the
control programs act on it. That is the point, and it is also the risk.

**It shadows, it does not replace.** The driver keeps publishing underneath. Clearing
the override snaps straight back to the live reading, with no gap and nothing to
restore.

**It survives a reboot.** Each override is persisted under its own storage key,
separate from any parameter setting underneath it, so clearing one leaves the other
alone. This is the opposite of what you might expect, and it is the safer of the two:
an override explains behaviour nothing else accounts for, and after a reboot nobody
remembers setting it. So the firmware announces restored overrides as a **critical**
at boot, `override list` says they survive a reboot, and `override off` clears the
lot - the stored copy included, so nothing comes back.

@note **Outputs are the exception.** A restored override on an output is *not*
reapplied. Control authority starts disabled at power-on, so a restored actuator
override would sit armed until bring-up enabled control and then fire from a session
long since walked away from.

**It can force the reading's quality too.** A trailing `valid` / `invalid` says what
the forced value reads as, so "force this to 20 C" and "make this read as a dead
sensor" are both expressible. MONITOR reports the second as
`OVERRIDDEN (forced invalid)` and `override list` flags it as `(reading as invalid)`.
The forced value is still what a reader gets, so this is not the same as simulating
the timeout fallback.

Unlike `set`, an override is checked only against the variable's *type*, not its
declared plausibility band - so you can push a reading past what the site considers
possible, which is usually the whole reason for forcing it.

@note Control authority outranks an override. Disabling control releases any
override holding an output, and while control is off an override on an output is
refused outright. See @ref user_control.
