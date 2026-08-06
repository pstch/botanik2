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
| parameter | editable and persisted (see @ref user_params) |

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

A forced variable gets a second line, giving both halves of the picture - what it is
being held at, and what it would read with the override released:

```
botanik> get temp_zone1
temp_zone1 = 35.0 C
Scope                  : AI input, calibrated   group temp
Access                 : read-only (`override` forces it by hand)
Quality                : forced by hand, reading valid
Override               : 35.0 C (valid)   live 21.3 C (valid)
Plausible              : -20.0 .. 60.0 C
Source                 : Modbus dev 11 fc 3 reg 4
Description            : zone 1 air temperature
```

The `Override` line is the one `list` cannot give you: what the row is being held at,
and what it reads *underneath*. "Has the sensor come back yet?" is the question
someone deciding whether to release an override actually has.

Three annotations matter:

- `(param)` - this is a parameter, editable with `set`.
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
botanik> override temp_zone1            # hold it at whatever it reads now
botanik> override temp_zone1 35.0 invalid   # force the value AND make it read as dead
botanik> override temp_zone1 off        # release it
botanik> override list                  # list everything currently forced
botanik> override clear                 # release everything (`off` also works)
```

A forced value reads as **good** unless you say `invalid`, the bare form included: a
variable nothing had ever written still comes out valid once you hold it, because
holding a value is asserting it.

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
at boot, `override list` says they survive a reboot, and `override clear` clears the
lot - the stored copy included, so nothing comes back.

@warning **That includes overrides on outputs.** One restored at boot is *driving the
actuator* from that moment. Every restored override is announced as a **critical** on
the `BOOT` topic naming the row, and an output's says so in as many words - that
announcement is the only thing standing between a forgotten override and a surprise.
`override list` before you walk away.

**It can force the reading's quality too.** A trailing `valid` / `invalid` says what
the forced value reads as, so "force this to 20 C" and "make this read as a dead
sensor" are both expressible. MONITOR reports the second as
`OVERRIDDEN (forced invalid)` and `override list` flags it as `(reading as invalid)`.
The forced value is still what a reader gets, so this is not the same as simulating
the timeout fallback.

Unlike `set`, an override is checked only against the variable's *type*, not its
declared plausibility band - so you can push a reading past what the site considers
possible, which is usually the whole reason for forcing it.

@note **Control authority and overrides are complementary, not ranked.**
`control disable` stops the control *programs*; an override still reaches an output,
and with the scan stopped it is the only thing that does - which is how you drive a
valve by hand. The exception is the firmware withdrawing control *itself* (a health
fault, a pending reset): that one does release an override on an output, because
nobody is necessarily watching. See @ref user_control.
