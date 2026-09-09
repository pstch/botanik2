# Humidity and rot {#gh_mlb41_humidity}

<!-- source of truth: programs/climate/humidity and intake_dry, thresholds in
     tables/params.csv. Adapted from the spec's "Humidite/pourriture". -->

![](humidity.svg)

Rot and condensation are among the main risks to *Haworthia*. More precisely,
two things: permanent moisture around the roots, which favours fungal growth,
and permanent moisture on the plant itself, which stops it breathing properly.
It matters more for plants using crassulacean acid metabolism - the stomata open
only at night, and it is at night that the risk of moisture and condensation is
highest. In the day, water loss is limited because the stomata are closed.

Several principles limit that:

- night watering happens only in dry conditions; in humid conditions it moves to
  the start of the day, so the tray has time to dry before night;
- at sunset a light breeze circulates to evaporate any moisture on the plants
  before their stomata open, which helps them breathe at night;
- at sunrise a light breeze circulates to evaporate the condensation left on the
  leaves by the morning dew;
- when humidity is high, internal ventilation is increased and wetting the skirt
  is forbidden;
- when the outside is more humid than the inside, ventilation is limited;
- when humidity is too low, the skirt is wetted and the intake fans run.

The ladder itself is a four-band regulator with hysteresis and a dead zone,
aiming at a range of dew-point margin in which the plants are comfortable
without developing rot:

| dew-point margin                       | what happens                                                       |
|----------------------------------------|--------------------------------------------------------------------|
| below `cli_dehumFull_degC` (3 degC)    | dehumidifying at full - fans at full power                         |
| below `cli_dehumStart_degC` (4.5 degC) | dehumidifying starts - fans at low power                           |
| above `cli_humidStart_degC` (18 degC)  | humidifying starts - the skirt is wetted, air comes in at low rate |
| above `cli_humidFull_degC` (28 degC)   | humidifying at full - air at full rate through the wetted skirt    |

`cli_rotMargin_degC` (1 degC) is the margin below which the rot lockout takes
hold, and `cli_rotRelease_sec` (300 s) how long it runs on past the condition.

@note A standalone dehumidifier sits alongside this, and only comes on in the
most extreme cases.

## Modulating the skirt

The skirt valve is all-or-nothing; it cannot meter water. To adjust the amount
anyway it is driven on a duty cycle: over a fixed period, the valve stays open
for a fraction of the time proportional to the humidifying demand.

At the trigger the duty is near zero - the skirt barely wets - and at maximum
demand the valve stays open continuously.

The valve respects a minimum on-time, to avoid short cycles that would wear the
solenoid and cause water hammer. So when the asked-for duty is below
minimum-on over period, the valve stays shut rather than produce a micro-pulse.
That small dead zone at the bottom of the range does not matter: wetting the
skirt is a guard against extreme dryness, not a precision adjustment.

## Control programs

| program      | every   | what it does                                                                                                                          |
|--------------|---------|---------------------------------------------------------------------------------------------------------------------------------------|
| `humidity`   | control | The humidity ladder: four bands and a dead zone on the dew-point margin, published as one signed demand - and the rot guard under it. |
| `intake_dry` | control | The intake dry gate: whether the air outside is drier than the air in.                                                                |
