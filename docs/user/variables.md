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
| parameter | editable, and saved when you say so (see @ref user_params) |

An input is invalidated two ways, and the second one matters more than it sounds: a
reading can be perfectly fresh and still be nonsense. A dead 4-20 mA loop reads full
scale forever, which no timeout catches - the plausibility band does.

## Reading one

Two commands, two depths. `list` browses - one row per variable, optionally
filtered by name prefix; MONITOR shows the same set grouped, on screen
(see @ref user_pages).

```
botanik> list pwr_L1_
  pwr_L1_V             237.4        V    (MI)
  pwr_L1_A             7.6          A    (MI)
  2 variable(s)
```

`get` is the detail view of ONE row:

```
botanik> get temp_zone1
temp_zone1 = 35.0 C
Scope ....................... : AI       (input, calibrated)
Group ....................... : temp
Access ...................... : read-only
Raw value ................... : 21.3 C   (valid)
Override value .............. : 35.0 C   (valid)
Plausible ................... : -20.0 .. 60.0 C
Source ...................... : Modbus dev 11 fc 3 reg 4
Description ................. : zone 1 air temperature
```

Laid out like `stats` and `net`, so the three commands that answer "what is the
state of X" read the same way.

**Raw value** and **Override value** are the pair `list` cannot give you: what the
row reads underneath, and what it is being held at. "Has the sensor come back
yet?" is the question someone deciding whether to release an override actually
has. Each says whether it is worth believing - a forced value can be good or made
to read as a dead sensor, and so can the reading under it. `Override value : -`
means nothing is forced.

Three annotations on a `list` row matter:

- `(P)`, `(MI)`, `(RO)`, ... - what kind of row it is. Every row carries it:
  which one you are looking at is what decides whether `params set` or `overrides set`
  applies.
- `OVERRIDDEN` - someone has forced it by hand; the number is not the live one.
- `(default: nothing has written it)` - no driver has ever published this variable,
  or it timed out. The number shown is the **declared default**, not a measurement.

That last one is the important one to recognise. A dead sensor does not read as
zero-because-broken; it reads as whatever the site declared as its fallback, and the
firmware tells you so.

### Why a reading is invalid

`get` and the MONITOR panel do not just say a value is bad, they say what made it
bad - because the four causes want four different next moves:

| Reads | What happened | Where to look |
|---|---|---|
| `valid` | a real value from whoever owns the row | - |
| `invalid: nothing has written it` | no driver has ever published it | wiring, or a device that never answered |
| `invalid: past its timeout` | it was live and stopped | the device went away mid-run |
| `invalid: outside its band` | it is answering, with nonsense | the sensor - a dead 4-20 mA loop reads full scale, and no timeout catches that |
| `invalid: dropped to its safe state` | an **output**, not a fault | control authority is withdrawn (see @ref user_control) |

The last one is worth recognising: a relay reading invalid usually means the
firmware is deliberately not driving it, not that anything is broken.

## Overrides

An override forces a variable to a value you choose. It is a maintenance tool: fake
a sensor to exercise the logic watching it, freeze a computed value, or drive an
actuator by hand.

```
botanik> overrides set temp_zone1 35.0     # force a value
botanik> overrides set temp_zone1          # hold it at whatever it reads now
botanik> overrides set temp_zone1 35.0 invalid  # force it AND make it read as dead
botanik> overrides set temp_zone1 off      # release it
botanik> overrides                         # list everything currently forced
botanik> overrides clear                   # release everything
botanik> overrides save                    # ...make them outlive this session
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

**It does NOT survive a reboot unless you save it.** A forced value lives as long
as the unit does and no longer, which is what a maintenance tool should do: an
actuator somebody drove by hand must not come back driven after a power cycle.
`overrides save` says otherwise - for the dead sensor you have to work around
until the replacement arrives - and writes each override under its own storage
key, separate from any parameter setting underneath it, so clearing one leaves
the other alone.

`overrides clear` releases the lot, stored copies included, so nothing comes
back. Unlike *setting* one, that reaches flash at once: releasing is unambiguous,
and an override that outlived its own release would be the worst of both.

@warning **A saved override on an output is driving the actuator from the moment
it is restored.** Every restored override is announced as a **critical** on the
`BOOT` topic naming the row, and an output's says so in as many words - that
announcement is the only thing between a forgotten override and a surprise. Check
`overrides` before you walk away, and think twice before saving one on an output
at all.

**It can force the reading's quality too.** A trailing `valid` / `invalid` says what
the forced value reads as, so "force this to 20 C" and "make this read as a dead
sensor" are both expressible. MONITOR reports the second as
`OVERRIDDEN (forced invalid)` and `overrides` flags it as `(reading as invalid)`.
The forced value is still what a reader gets, so this is not the same as simulating
the timeout fallback.

Unlike `params set`, an override is checked only against the variable's *type*, not its
declared plausibility band - so you can push a reading past what the site considers
possible, which is usually the whole reason for forcing it.

@note **Control authority and overrides are complementary, not ranked.**
`control disable` stops the control *programs*; an override still reaches an output,
and with the scan stopped it is the only thing that does - which is how you drive a
valve by hand. The exception is the firmware withdrawing control *itself* (a health
fault, a pending reset): that one does release an override on an output, because
nobody is necessarily watching. See @ref user_control.
