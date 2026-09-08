# The physics it controls on {#gh_mlb41_principles}

<!-- From the spec's "Principes physiques fondamentaux". The formulas are the
     spec's; the row and parameter names are the firmware's. -->

Four ideas run through every ladder in this greenhouse. Two are about how wet
the air is, one is about how the probes are read, one about how the readings are
smoothed.

## Dew point, and the margin above it

The dew point is the temperature below which dew settles: below it the vapour in
the air condenses onto surfaces. It follows from the air temperature and the
relative humidity, and in this greenhouse we avoid reaching it wherever we can.

People usually talk about limiting humidity so that plants - *Haworthia*
especially - grow well. The real risk here is narrower: how much water can
condense on the plant and around its roots, and that is what the dew point
decides.

So the control system works on the **dew-point margin**, the gap between the air
temperature and its dew point:

    margin = T - T_dew

The larger it is, the drier. The smaller it is, the more water is driven out of
the air onto surfaces and into anything that absorbs it. Keeping that value off
the floor is what the humidity ladder is for.

The dew point comes from the Magnus formula:

    gamma = ln(RH) + (17.62 * T) / (243.12 + T)
    T_dew = (243.12 * gamma) / (17.62 - gamma)

@note RH is a fraction from 0 to 1, not a percentage, and it must be clamped
above 0.05 before the logarithm: ln(0) is undefined, so a probe reading 0%
would otherwise fault the calculation. The formula holds for T between -40 and
+50 degC.

## Vapour pressure deficit

The other measure is the vapour pressure deficit: the gap between the water the
air could hold and the water it does hold, in kPa.

The higher the deficit, the drier the air and the harder it pulls water out of
the plants - it is what drives transpiration. Too low a deficit favours
condensation, and with it fungal disease and rot at the plant or its roots.

    P_sat(T) = 0.6108 * exp(17.27 * T / (T + 237.3))
    VPD      = P_sat(T) - P_sat(T_dew)

@note 0.6108 / 17.27 / 237.3 give P_sat in kPa for T in degC. That is Tetens'
formula.

The deficit and the dew-point margin tell the same story - how thirsty the air
is - in two units, and the relation between them is not linear, because the
water air can hold grows exponentially with temperature. The same 3 degC of
dew-point margin is about 0.4 kPa at 10 degC and close to 1.0 kPa at 30 degC.

That is why the two are used for different jobs:

- **rot and condensation are set in degC**, on the dew-point margin, because
  that is what decides whether condensation actually happens at the temperature
  the greenhouse is at;
- **irrigation is set in kPa**, on the vapour pressure deficit, because that is
  what has a linear relation to the water moving between the air, the plants and
  their substrate.

## Cooling and humidity are the same lever

There is no real refrigeration here. Cooling is ventilation: push the hot air
out and replace it with cooler air from the crawlspace under the floor.

The incoming air passes through a textile skirt that can be fed with water. Wet,
it **cools** the incoming air and makes it wetter, and that adds to the fact
that air under the greenhouse radiates some of its energy into the ground, which
sits at a fairly constant temperature. The skirt is never wetted in winter - it
could only cool - but it is kept, because it gives a thermal screen and lets the
crawlspace air **warm** by taking heat from the ground.

When the outside air is too humid we avoid ventilating hard, and lean on the
standalone dehumidifier inside instead. But when the inside humidity gets truly
high and the outside air is colder, we let our heat escape through the windows -
taking the water with it - and lean on the petrol heater. Humidity is one of the
largest risks to these plants, so it wins that trade.

## Median vote across three probes

Every measurement point has three probes, and the value taken is **the median of
the three**: keep the middle, discard both extremes. A probe stuck high or stuck
low can therefore never be the value used.

If one fails, the system carries on with the remaining two and raises the quorum
alarm for that point - `alarm_sns_floor`, `alarm_sns_ceiling`,
`alarm_sns_crawlspace`, `alarm_sns_exterior`. The greenhouse stays controlled,
and the operator knows an intervention is due before a second probe goes.

Between measurement levels the system always takes **the least favourable
case**: the highest temperature for cooling, the smallest dew-point margin for
rot, the lowest VPD for irrigation. It fails to the safe side with no extra
logic.

## EWMA smoothing

An exponential filter is applied to the median values so that measurement noise
does not propagate into the command:

    y <- y + alpha * (x - y)

where x is the new measurement, y the filtered value, and alpha how fast the
filter is: near 1 it reacts at once, near 0 it smooths hard.

Irrigation's dryness tracking uses the same filter, deliberately very slow:

    alpha = 1 / (24 * irr_dryEwmaDays)

@note That filter is updated **once an hour**. At every scan the increment would
be too small and the filter would freeze numerically; once a day, the result
would depend on the hour the measurement happened to be taken. The interval is
not a parameter.

The cooling PID has to be tuned with the small lag this smoothing adds.
