# gh_mlb41: the control system, scenario by scenario

What the control system does, scenario by scenario. Every number here was
one an assertion in `tests/test_site_scenarios.cpp` was made against when
this was written; the cases are still the authority, and this is the
readable account of them.

## What is being driven

The whole site, in the order the firmware registers it: the instruments, then the ladders that read their rows, then the actuators that read the demands. One `Vars::Bank` behind all of them, stepped at the site's own period, exactly as the firmware steps it. No program holds a reference to another - what one decides reaches the next through the table, so what a scenario changes is the WEATHER and everything after that is the control system's own doing.

Two programs are deliberately absent. `sun_watch` needs a fix and a wall clock, and `day_phase` would then follow whichever hour the suite happened to run at, so the phase is an input here and the two have cases of their own.

A field row that stops being refreshed goes stale, so each case holds its weather every beat: that is what makes a probe DYING a thing a scenario can do simply by stopping.

17 scenarios.

| scenario | what it asks |
|---|---|
| A hot afternoon | 32 degC under the ridge with 18 degC in the crawlspace: the whole cooling ladder, from the roof down to the wet skirt. |
| A cold night | 9 degC inside against 4 degC out, and 14 degC of stored heat under the floor: the negative half of the same ladder. |
| A shower on a hot afternoon | The weather interlock against the ladder: which of the two owns the roof, and how long the answer outlives the rain. |
| A damp night | 96 % at 14 degC, under a degree of dew-point margin: the rot guard, and the one thing the wet skirt must never do. |
| A dry spell | 15 % at 28 degC: the other arm of the humidity ladder, and the only humidifier the plant has. |
| The ceiling probes dying | All three of one level stop answering in the middle of a hot afternoon: what the plant does with a reading it no longer has. |
| The supply going down | The UPS reports its battery going while the plant is working hard: the lockout is the plant's safe state, and it is reversible. |
| A long hot spell, then relief | Ten minutes of the cooling loop pinned at its limit, and what that costs when the house finally reaches its target. |
| A house parked on a threshold | A tenth of a degree of probe noise either side of a gate's own margin, for a minute: the failure mode that wears hardware out. |
| An evening, in order | The house cooling through the whole ladder in one run: which of the roof and the screen moves first, and what the fans do meanwhile. |
| A dawn watering | The end of a mild night, every tray owed water: one cycle per tray, and the day's allowance closing the slot behind them. |
| Forcing a tray, and holding one back | Both forces are ONE-SHOT: they change the NEXT cycle and are spent doing it, or they would ask for the same thing every morning for ever and no other tray would be reached. |
| A cold dawn | 8 degC outside at the end of the night: watering cold roots is what the dawn gate exists to prevent, and the morning slot is where that watering goes instead. |
| Damp air against dry | The same morning twice, at 92 % and at 25 %: the modulation is the whole of this greenhouse's seasons, and it moves both the dose and the interval. |
| A tray that will not drain | The fill arrives and only a tenth of it comes back: a clog is a MEASUREMENT, made against the dose that tray actually got. |
| A leak, and a main with no water in it | Both are read off the inlet meter alone: water moving with every valve shut, and water not arriving with the inlet open. |
| The supply going down mid-fill | Ten valves, and they do not all fail the same way: the inlet shuts and the drains open, so a tray half full empties instead of sitting there. |

<!-- page -->

## A hot afternoon

32 degC under the ridge with 18 degC in the crawlspace: the whole cooling ladder, from the roof down to the wet skirt.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**The plant settled in the heat**

- the interior is well over the target, so the ladder is calling for everything it has
- windows: the ladder is past cli_windowFullDem, so wide open
- intractors: the space under the greenhouse is 14 degC cooler, so the gate is open and they run
- fans: a cooling demand past cli_fanHighDem is speed 3
- skirt: past cli_skirtCoolDem, and the air is dry enough for a wet pad to cool rather than just wet it
- and the screen stays off the glass: DAY forbids deploying it

### What it measured

| what it is | row | value |
|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 32.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 18.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 13.14 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY |

### What it decided

| what it is | row | value |
|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 1.00 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 |

### What it moved

| what it is | row | value |
|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 1.00 |
| thermal screen | `cli_screen` | no |
| thermal screen travelling | `cli_screenMoving` | no |
| intractor 1 speed | `cli_intr1` | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 3.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 1.00 |
| wet skirt valve | `cli_skirtValve` | yes |

### What was allowed

| what it is | row | value |
|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes |
| rain holds the windows shut | `cli_rainLockout` | no |
| wind holds the windows shut | `cli_windLockout` | no |
| the most the roof may open | `cli_windowCap` | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no |
| dew-point margin low: rot risk | `alarm_rot` | no |
| inside temperature too high | `alarm_too_hot` | no |
| inside temperature too low | `alarm_too_cold` | no |
| current storm conditions | `alarm_weather_storm` | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no |

<!-- page -->

## A cold night

9 degC inside against 4 degC out, and 14 degC of stored heat under the floor: the negative half of the same ladder.

**The weather it held.** ceiling 9.0 degC, floor 8.0 degC, crawlspace 14.0 degC, outside 4.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase NIGHT.

### What happened

**The plant settled in the cold**

- the interior is well under the target, so the ladder is negative
- screen: deployed, and NIGHT is what permits it
- intractors: the ground is warmer than the house, so the mirror gate opens and they run at the low ground-heat speed - not at the ladder
- windows: shut, because a negative demand ladders to zero
- and nothing is calling for the skirt in the middle of a cold night
- 70% on a cold night is an ordinary night, and the alarm says so: the margin is wide, the ladder is idle, and TOO_HUM agrees with both

### What it measured

| what it is | row | value |
|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 9.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 4.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 14.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.70 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 5.14 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC |
| phase of the day in force | `cli_phase` | NIGHT |

### What it decided

| what it is | row | value |
|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | -1.00 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.00 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 0.00 |

### What it moved

| what it is | row | value |
|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 0.00 |
| thermal screen | `cli_screen` | yes |
| thermal screen travelling | `cli_screenMoving` | no |
| intractor 1 speed | `cli_intr1` | 0.30 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 1.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 0.00 |
| wet skirt valve | `cli_skirtValve` | no |

### What was allowed

| what it is | row | value |
|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | no |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | yes |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes |
| rain holds the windows shut | `cli_rainLockout` | no |
| wind holds the windows shut | `cli_windLockout` | no |
| the most the roof may open | `cli_windowCap` | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no |
| dew-point margin low: rot risk | `alarm_rot` | no |
| inside temperature too high | `alarm_too_hot` | no |
| inside temperature too low | `alarm_too_cold` | no |
| current storm conditions | `alarm_weather_storm` | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no |

<!-- page -->

## A shower on a hot afternoon

The weather interlock against the ladder: which of the two owns the roof, and how long the answer outlives the rain.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**1. Before the shower: the roof wide open**


**2. Raining: the demand stands, the roof does not**

- it starts raining on a hot afternoon: the ladder still wants the roof open, and stage one of the arbitration does not care

**3. A minute after the last drop**

- the shower stops, and the lockout OUTLIVES it: a shower that has just stopped is about to be one that has not

### What it measured

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 32.00 degC | 32.00 degC | 32.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC | 30.00 degC | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 18.00 degC | 18.00 degC | 18.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 | 0.45 | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 13.14 degC | 13.14 degC | 13.14 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC | 20.00 degC | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY | DAY | DAY |

### What it decided

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 1.00 | 1.00 | 1.00 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 | 0.20 | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 | 0.00 | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 | 0.00 | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 | 1.00 | 1.00 |

### What it moved

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 1.00 | 0.00 | 0.00 |
| thermal screen | `cli_screen` | no | no | no |
| thermal screen travelling | `cli_screenMoving` | no | no | no |
| intractor 1 speed | `cli_intr1` | 1.00 | 1.00 | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 3.00 | 3.00 | 3.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 1.00 | 1.00 | 1.00 |
| wet skirt valve | `cli_skirtValve` | yes | yes | yes |

### What was allowed

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes | yes | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no | no | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes | yes | yes |
| rain holds the windows shut | `cli_rainLockout` | no | yes | yes |
| wind holds the windows shut | `cli_windLockout` | no | no | no |
| the most the roof may open | `cli_windowCap` | 1.00 | 0.00 | 0.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no | no |
| dew-point margin low: rot risk | `alarm_rot` | no | no | no |
| inside temperature too high | `alarm_too_hot` | no | no | no |
| inside temperature too low | `alarm_too_cold` | no | no | no |
| current storm conditions | `alarm_weather_storm` | no | no | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no | no | no |

<!-- page -->

## A damp night

96 % at 14 degC, under a degree of dew-point margin: the rot guard, and the one thing the wet skirt must never do.

**The weather it held.** ceiling 14.0 degC, floor 13.5 degC, crawlspace 13.0 degC, outside 8.0 degC, humidity 96 % / 75 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase NIGHT.

### What happened

**The rot guard up, the skirt locked out**

- 96% at 14 degC is under a degree of dew-point margin: the rot guard is up and the ladder is asking to dehumidify
- the skirt is locked out: wetting air that is already condensing is the one thing that valve must never do
- and TOO_HUM is up, because the dehumidify arm is saturated and the air is still this wet - the alarm the operator can act on
- and the anti-rot ventilation runs the intractors, ungated: the dew point it is fighting is the one inside

### What it measured

| what it is | row | value |
|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 14.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 8.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 13.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.96 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 0.63 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC |
| phase of the day in force | `cli_phase` | NIGHT |

### What it decided

| what it is | row | value |
|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | -0.77 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.00 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | -1.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.37 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 0.00 |

### What it moved

| what it is | row | value |
|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 0.00 |
| thermal screen | `cli_screen` | yes |
| thermal screen travelling | `cli_screenMoving` | no |
| intractor 1 speed | `cli_intr1` | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 1.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 0.00 |
| wet skirt valve | `cli_skirtValve` | no |

### What was allowed

| what it is | row | value |
|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes |
| rain holds the windows shut | `cli_rainLockout` | no |
| wind holds the windows shut | `cli_windLockout` | no |
| the most the roof may open | `cli_windowCap` | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no |
| dew-point margin low: rot risk | `alarm_rot` | yes |
| inside temperature too high | `alarm_too_hot` | no |
| inside temperature too low | `alarm_too_cold` | no |
| current storm conditions | `alarm_weather_storm` | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no |

<!-- page -->

## A dry spell

15 % at 28 degC: the other arm of the humidity ladder, and the only humidifier the plant has.

**The weather it held.** ceiling 28.0 degC, floor 26.0 degC, crawlspace 20.0 degC, outside 30.0 degC, humidity 15 % / 20 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**The skirt wetting the intake**

- 15% at 28 degC is a very large dew-point margin: the ladder's other arm, asking to humidify
- the skirt modulates, and the intractors draw the air in through it - the wetted textile is the only humidifier the plant has

### What it measured

| what it is | row | value |
|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 28.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 20.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.15 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 28.66 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY |

### What it decided

| what it is | row | value |
|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 1.00 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 1.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 |

### What it moved

| what it is | row | value |
|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 1.00 |
| thermal screen | `cli_screen` | no |
| thermal screen travelling | `cli_screenMoving` | yes |
| intractor 1 speed | `cli_intr1` | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 0.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 1.00 |
| wet skirt valve | `cli_skirtValve` | yes |

### What was allowed

| what it is | row | value |
|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | no |
| rain holds the windows shut | `cli_rainLockout` | no |
| wind holds the windows shut | `cli_windLockout` | no |
| the most the roof may open | `cli_windowCap` | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no |
| dew-point margin low: rot risk | `alarm_rot` | no |
| inside temperature too high | `alarm_too_hot` | no |
| inside temperature too low | `alarm_too_cold` | no |
| current storm conditions | `alarm_weather_storm` | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no |

<!-- page -->

## The ceiling probes dying

All three of one level stop answering in the middle of a hot afternoon: what the plant does with a reading it no longer has.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**1. Five minutes without a ceiling reading**

- all three ceiling probes stop answering, and the FLOOR carries the law: the hottest level is what cooling controls on
- the row HOLDS its last average rather than reading a default of zero
- the plant keeps running on the floor: still hot, still open
- and the laws that read it say they are working on a reading they do not have
- the dew-point margin is still MEASURED, and it is the FLOOR's: a level holding an old reading does not speak for the house

**2. The house with no measured margin at all**

- the floor goes too, and now nothing measures the house: the margin keeps the number it last had and says it is not current
- and the rot guard fails toward acting on a margin nobody has, which is the branch it has always documented and never reached

### What it measured

| what it is | row | 1 | 2 |
|---|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 29.00 degC | 32.00 degC (held, its inputs are gone) |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 18.00 degC | 18.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 13.14 degC | 13.14 degC (held, its inputs are gone) |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY | DAY |

### What it decided

| what it is | row | 1 | 2 |
|---|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 0.87 | 0.87 (held, its inputs are gone) |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 (held, its inputs are gone) | 0.20 (held, its inputs are gone) |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 | 0.00 (held, its inputs are gone) |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 | 1.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 | 1.00 (held, its inputs are gone) |

### What it moved

| what it is | row | 1 | 2 |
|---|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 1.00 | 0.25 |
| thermal screen | `cli_screen` | no | no |
| thermal screen travelling | `cli_screenMoving` | no | no |
| intractor 1 speed | `cli_intr1` | 0.95 | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 3.00 | 1.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 0.56 | 0.00 |
| wet skirt valve | `cli_skirtValve` | yes | no |

### What was allowed

| what it is | row | 1 | 2 |
|---|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | no | no |
| rain holds the windows shut | `cli_rainLockout` | no | no |
| wind holds the windows shut | `cli_windLockout` | no | no |
| the most the roof may open | `cli_windowCap` | 1.00 | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no |
| dew-point margin low: rot risk | `alarm_rot` | no | no |
| inside temperature too high | `alarm_too_hot` | no | no |
| inside temperature too low | `alarm_too_cold` | no | no |
| current storm conditions | `alarm_weather_storm` | no | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | yes | yes |

<!-- page -->

## The supply going down

The UPS reports its battery going while the plant is working hard: the lockout is the plant's safe state, and it is reversible.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**1. The battery going, and the plant parked**

- the UPS says its battery is going: the whole plant is held, and every actuator goes where the table says it fails to
- the lockout IS the plant's safe state: every program was asked to park, through the machinery a halt already uses
- windows: shut, whatever the ladder wants - the lockout is stage one
- skirt: shut

**2. The supply back, and the plant with it**

- ...and the plant is released the moment the battery is not: the hold goes down on the same edge that raised it, and the roof reopens

### What it measured

| what it is | row | 1 | 2 |
|---|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 32.00 degC | 32.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 18.00 degC | 18.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 13.14 degC | 13.14 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY | DAY |

### What it decided

| what it is | row | 1 | 2 |
|---|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 1.00 | 1.00 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 | 1.00 |

### What it moved

| what it is | row | 1 | 2 |
|---|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 0.00 | 0.91 |
| thermal screen | `cli_screen` | yes | no |
| thermal screen travelling | `cli_screenMoving` | yes | yes |
| intractor 1 speed | `cli_intr1` | 0.15 | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 0.00 | 0.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 0.00 | 1.00 |
| wet skirt valve | `cli_skirtValve` | no | yes |

### What was allowed

| what it is | row | 1 | 2 |
|---|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes | yes |
| rain holds the windows shut | `cli_rainLockout` | no | no |
| wind holds the windows shut | `cli_windLockout` | no | no |
| the most the roof may open | `cli_windowCap` | 0.00 | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | yes | no |
| dew-point margin low: rot risk | `alarm_rot` | no | no |
| inside temperature too high | `alarm_too_hot` | no | no |
| inside temperature too low | `alarm_too_cold` | no | no |
| current storm conditions | `alarm_weather_storm` | no | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no | no |

<!-- page -->

## A long hot spell, then relief

Ten minutes of the cooling loop pinned at its limit, and what that costs when the house finally reaches its target.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**1. Ten minutes hard against the limit**


**2. The roof shut again, and no minutes of winding to pay off**

- the house reaches its target and the demand comes off the limit at once, keeping only what the integral had needed: the proportional term was doing the work, and conditional integration would not let the integral accumulate underneath it while the output was pinned
- ...and it does not fall to nothing either: some ventilation IS what is holding the house at its target
- ...and once the house is genuinely COLDER than wanted, the loop lets go from the FIRST beat rather than sitting on stored effort
- and the roof shuts within a time the tuning explains, not within one the winding does: the integral was clamped to what the output can span, so an afternoon of saturation costs no more than a minute of it

### What it measured

| what it is | row | 1 | 2 |
|---|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 32.00 degC | 17.44 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 18.00 degC | 18.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 13.14 degC | 11.98 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY | DAY |

### What it decided

| what it is | row | 1 | 2 |
|---|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 1.00 | -0.06 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 | 0.00 |

### What it moved

| what it is | row | 1 | 2 |
|---|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 1.00 | 0.00 |
| thermal screen | `cli_screen` | no | no |
| thermal screen travelling | `cli_screenMoving` | no | no |
| intractor 1 speed | `cli_intr1` | 1.00 | 0.15 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 3.00 | 1.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 1.00 | 0.00 |
| wet skirt valve | `cli_skirtValve` | yes | no |

### What was allowed

| what it is | row | 1 | 2 |
|---|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes | no |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes | no |
| rain holds the windows shut | `cli_rainLockout` | no | no |
| wind holds the windows shut | `cli_windLockout` | no | no |
| the most the roof may open | `cli_windowCap` | 1.00 | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no |
| dew-point margin low: rot risk | `alarm_rot` | no | no |
| inside temperature too high | `alarm_too_hot` | no | no |
| inside temperature too low | `alarm_too_cold` | no | no |
| current storm conditions | `alarm_weather_storm` | no | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no | no |

<!-- page -->

## A house parked on a threshold

A tenth of a degree of probe noise either side of a gate's own margin, for a minute: the failure mode that wears hardware out.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**A minute of noise later, nothing has moved**

- the gate holds through the noise: it opens at its margin and closes a band under it, so a difference sitting on the margin cannot flip it
- ...and the fans hold their speed through the same noise

### What it measured

| what it is | row | value |
|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 32.00 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 30.00 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 31.05 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 13.14 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC |
| phase of the day in force | `cli_phase` | DAY |

### What it decided

| what it is | row | value |
|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | 1.00 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 1.00 |

### What it moved

| what it is | row | value |
|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 1.00 |
| thermal screen | `cli_screen` | no |
| thermal screen travelling | `cli_screenMoving` | no |
| intractor 1 speed | `cli_intr1` | 1.00 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 3.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 1.00 |
| wet skirt valve | `cli_skirtValve` | yes |

### What was allowed

| what it is | row | value |
|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | yes |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | no |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes |
| rain holds the windows shut | `cli_rainLockout` | no |
| wind holds the windows shut | `cli_windLockout` | no |
| the most the roof may open | `cli_windowCap` | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no |
| dew-point margin low: rot risk | `alarm_rot` | no |
| inside temperature too high | `alarm_too_hot` | no |
| inside temperature too low | `alarm_too_cold` | no |
| current storm conditions | `alarm_weather_storm` | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no |

<!-- page -->

## An evening, in order

The house cooling through the whole ladder in one run: which of the roof and the screen moves first, and what the fans do meanwhile.

**The weather it held.** ceiling 32.0 degC, floor 29.0 degC, crawlspace 18.0 degC, outside 30.0 degC, humidity 45 % / 40 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DUSK_BREEZE.

### What happened

**Shut, deployed, and the breeze back on**

- dusk, and the house loses heat at a couple of degrees a minute
- both happen, and the ROOF FIRST: the ladder passes zero long before it passes the screen's rung, so the fabric is never asked to hold heat through an open window
- the fans are cut while the fabric travels: it is moving through the air they blow into
- and once it has arrived they are allowed back - the breeze phase is asking for one

### What it measured

| what it is | row | value |
|---|---|---|
| interior temperature: the hotter of the two levels | `cli_temp_in` | 12.06 degC |
| exterior temperature, voted and filtered | `cli_temp_ext` | 10.06 degC |
| crawlspace temperature, voted and filtered | `cli_temp_cs` | 18.00 degC |
| ceiling relative humidity, voted and filtered | `cli_rh_hi` | 0.45 |
| interior dew-point margin: how far from condensing | `cli_dpMargin_degC` | 11.32 degC |
| cooling target in force for the current phase | `cli_targetTemp_degC` | 20.00 degC |
| phase of the day in force | `cli_phase` | DUSK_BREEZE |

### What it decided

| what it is | row | value |
|---|---|---|
| cooling demand, -1 warming to +1 cooling | `cli_coolDem` | -0.46 |
| destratification demand, 0 to 1 | `cli_stratDem` | 0.20 |
| humidity demand, -1 dehumidify to +1 humidify | `cli_humidDem` | 0.00 |
| anti-rot ventilation demand, 0 to 1 | `cli_rotVentDem` | 0.00 |
| how far the cooling ladder wants the windows open | `cli_windowDem` | 0.00 |

### What it moved

| what it is | row | value |
|---|---|---|
| window 1 position, 0 to 1 | `cli_window1_pos` | 0.00 |
| thermal screen | `cli_screen` | yes |
| thermal screen travelling | `cli_screenMoving` | no |
| intractor 1 speed | `cli_intr1` | 0.30 |
| destratification fan speed in force, 0 to 3 | `cli_fanSpeed` | 1.00 |
| skirt valve on-time, 0 to 1 | `cli_skirtDuty` | 0.00 |
| wet skirt valve | `cli_skirtValve` | no |

### What was allowed

| what it is | row | value |
|---|---|---|
| intake air is cool enough to be worth drawing | `cli_intakeCoolOk` | no |
| intake air is warm enough to be worth drawing | `cli_groundHeatOk` | yes |
| outside air is dry enough to be worth drawing | `cli_intakeDryOk` | yes |
| rain holds the windows shut | `cli_rainLockout` | no |
| wind holds the windows shut | `cli_windLockout` | no |
| the most the roof may open | `cli_windowCap` | 1.00 |
| supply unhealthy: actuators held off | `sys_supply_lockout` | no |
| dew-point margin low: rot risk | `alarm_rot` | no |
| inside temperature too high | `alarm_too_hot` | no |
| inside temperature too low | `alarm_too_cold` | no |
| current storm conditions | `alarm_weather_storm` | no |
| ceiling temp/RH probe quorum lost | `alarm_sns_ceiling` | no |

<!-- page -->

## A dawn watering

The end of a mild night, every tray owed water: one cycle per tray, and the day's allowance closing the slot behind them.

**The weather it held.** ceiling 16.0 degC, floor 15.0 degC, crawlspace 14.0 degC, outside 13.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAWN_BREEZE.

### What happened

**1. Filling the first tray**

- the end of the night, and 13 degC outside clears the dawn gate: the cycle takes the first tray owed water and opens the inlet on it
- ...and only that tray: one cycle can never water two
- the dose is the tray's SUMMER figure scaled down by the weather, and damp air at dawn is a long way from summer
- ...and one end of the circuit is open: water arriving at the inlet meter, and the drain shut behind the tray

**2. The tray sitting full through its soak**

- the fill ends at the DOSE and not at a time: the meter says when, and nothing is blamed for a fill that arrived
- ...and the tray keeps it: both ends of the manifold shut for the soak, the tray still connected, and neither meter reading anything

**3. Draining the first tray**

- then the drain opens on the same tray
- draining: the inlet shut, the tray still open, the drain carrying what comes back past the outlet meter
- ...and the day is stamped at the END OF THE FILL, so a fault during the long drain cannot water the tray twice
- water is leaving and none is arriving: one meter reads and the other does not, which is what a drain looks like at the two ends

**4. Rinsing the manifold between two trays**

- enough came back for the tray to be clear of a clog
- ...and the manifold is rinsed with no tray between the two ends, so what sat in the line is not what the next tray gets first
- both meters read, and read the SAME thing: fresh water going in one end of the manifold and out of the other, with no tray to hold it

**5. The day's allowance spent**

- then the second tray, because tray 1 is no longer owed anything
- and there it stops: irr_traysPerDay is the day's allowance, and the trays still owed water wait for tomorrow

### What it measured

| what it is | row | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 13.00 degC | 13.00 degC | 13.00 degC | 13.00 degC | 13.00 degC |
| phase of the day in force | `cli_phase` | DAWN_BREEZE | DAWN_BREEZE | DAWN_BREEZE | DAWN_BREEZE | DAWN_BREEZE |
| minutes since sunrise; negative before it | `sol_fromRise_min` | -30.00 min | -30.00 min | -30.00 min | -30.00 min | -30.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.51 kPa | 0.51 kPa | 0.51 kPa | 0.51 kPa | 0.51 kPa |

### What it decided

| what it is | row | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.51 | 0.51 | 0.51 | 0.51 | 0.51 |
| modulated interval, for the tray due next | `irr_period_days` | 19.63 days | 19.63 days | 19.63 days | 19.63 days | 19.63 days (held, its inputs are gone) |
| modulated dose, for the tray being served | `irr_volume_L` | 107.00 L | 107.00 L | 107.00 L | 0.00 L | 0.00 L |

### What it moved

| what it is | row | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | yes | no | no | yes | no |
| irrigation output valve 1 | `irr_output_ro1` | no | no | yes | yes | no |
| tray 1 valve | `irr_tray1` | yes | yes | yes | no | no |
| tray 2 valve | `irr_tray2` | no | no | no | no | no |

### What the water did

| what it is | row | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | FILLING | SOAKING | DRAINING | FLUSHING | IDLE |
| tray being served, 0 when none | `irr_activeTray` | 1.00 | 1.00 | 1.00 | 0.00 | 0.00 |
| trays served since the day rolled over | `irr_servedToday` | 0.00 | 1.00 | 1.00 | 1.00 | 2.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days | 20689.00 days | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no | no | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | no | no | no | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no | no | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 0.00 days | 20689.00 days | 20689.00 days | 20689.00 days | 20689.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 26.00 L | 107.00 L | 107.00 L | 107.00 L | 0.00 L |
| drained back out of the tray being served | `irr_drained_L` | 0.00 L | 0.00 L | 4.90 L | 85.70 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 36.00 Lpm | 0.00 Lpm | 0.00 Lpm | 36.00 Lpm | 0.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 26.00 L | 107.00 L | 107.00 L | 112.00 L | 226.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 0.00 Lpm | 0.00 Lpm | 30.00 Lpm | 39.91 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 0.00 L | 0.00 L | 4.90 L | 90.90 L | 184.73 L |

### What was allowed

| what it is | row | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no | no | no | no |
| a tray would not fill in time | `alarm_irr_fill` | no (nothing has written it) | no | no | no | no |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | no (nothing has written it) | no (nothing has written it) | no | no |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | no (nothing has written it) | no (nothing has written it) | no | no |
| leak in irrigation circuit | `alarm_water_leak` | no | no | no | no | no |
| water pressure too low | `alarm_water_pressure` | no | no | no | no | no |

<!-- page -->

## Forcing a tray, and holding one back

Both forces are ONE-SHOT: they change the NEXT cycle and are spent doing it, or they would ask for the same thing every morning for ever and no other tray would be reached.

**The weather it held.** ceiling 16.0 degC, floor 15.0 degC, crawlspace 14.0 degC, outside 13.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAWN_BREEZE.

### What happened

**1. Filling the tray that was asked for**

- tray 5 is forced on. Every tray is owed water and the schedule would have taken the first, so the tray that goes is the one asked for

**2. Back to the schedule for the second cycle**

- the force is spent as the fill completes, and owed to flash with the rest of the schedule
- ...so the day's second cycle goes back to the schedule, which has been asking for tray 1 all along
- ...and the drain is shut behind it: a tray being filled has one end open, and the meter at the other end reads nothing

**3. The held-back tray passed over**

- the other force, on a fresh plant: tray 1 is due and held back by hand, so the cycle passes over it and takes the next one owed
- the skip is spent doing it, and the tray is STAMPED as though it had been watered - so it waits its period rather than arriving again tomorrow with the flag gone

### What it measured

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 13.00 degC | 13.00 degC | 13.00 degC |
| phase of the day in force | `cli_phase` | DAWN_BREEZE | DAWN_BREEZE | DAWN_BREEZE |
| minutes since sunrise; negative before it | `sol_fromRise_min` | -30.00 min | -30.00 min | -30.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.51 kPa | 0.51 kPa | 0.51 kPa |

### What it decided

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.51 | 0.51 | 0.51 |
| modulated interval, for the tray due next | `irr_period_days` | 19.63 days | 19.63 days | 19.63 days |
| modulated dose, for the tray being served | `irr_volume_L` | 107.00 L | 107.00 L | 107.00 L |

### What it moved

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | yes | yes | yes |
| irrigation output valve 1 | `irr_output_ro1` | no | no | no |
| tray 1 valve | `irr_tray1` | no | yes | no |
| tray 2 valve | `irr_tray2` | no | no | yes |

### What the water did

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | FILLING | FILLING | FILLING |
| tray being served, 0 when none | `irr_activeTray` | 5.00 | 1.00 | 2.00 |
| trays served since the day rolled over | `irr_servedToday` | 0.00 | 1.00 | 0.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | yes | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 0.00 days | 0.00 days | 20689.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 26.00 L | 7.00 L | 26.00 L |
| drained back out of the tray being served | `irr_drained_L` | 0.00 L | 0.00 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 36.00 Lpm | 36.00 Lpm | 36.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 26.00 L | 120.00 L | 26.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 0.00 Lpm | 0.00 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 0.00 L | 92.37 L | 0.00 L |

### What was allowed

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no | no |
| a tray would not fill in time | `alarm_irr_fill` | no (nothing has written it) | no | no (nothing has written it) |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | no | no (nothing has written it) |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | no (nothing has written it) | no (nothing has written it) |
| leak in irrigation circuit | `alarm_water_leak` | no | no | no |
| water pressure too low | `alarm_water_pressure` | no | no | no |

<!-- page -->

## A cold dawn

8 degC outside at the end of the night: watering cold roots is what the dawn gate exists to prevent, and the morning slot is where that watering goes instead.

**The weather it held.** ceiling 11.0 degC, floor 10.0 degC, crawlspace 12.0 degC, outside 8.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAWN_BREEZE.

### What happened

**1. Cold, at the end of the night**

- the end of the night, and too cold: the trays are owed water and the cycle does not start anyway

**2. Watering in the morning slot instead**

- the sun comes up and the day's own slot opens: the same tray, the same dose, an hour and a half later

**3. The afternoon, with trays still owed water**

- ...and the afternoon is not a slot at all: the cycle in hand finishes and no second one starts, because a tray filled then is still wet when the stomata open at dusk

### What it measured

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 8.00 degC | 8.00 degC | 8.00 degC |
| phase of the day in force | `cli_phase` | DAWN_BREEZE | DAY | DAY |
| minutes since sunrise; negative before it | `sol_fromRise_min` | -30.00 min | 90.00 min | 400.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.37 kPa | 0.37 kPa | 0.37 kPa |

### What it decided

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.41 | 0.41 | 0.41 |
| modulated interval, for the tray due next | `irr_period_days` | 0.00 days (held, its inputs are gone) | 21.96 days | 21.96 days (held, its inputs are gone) |
| modulated dose, for the tray being served | `irr_volume_L` | 0.00 L | 95.65 L | 0.00 L |

### What it moved

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | no | yes | no |
| irrigation output valve 1 | `irr_output_ro1` | no | no | no |
| tray 1 valve | `irr_tray1` | no | yes | no |
| tray 2 valve | `irr_tray2` | no | no | no |

### What the water did

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | IDLE | FILLING | IDLE |
| tray being served, 0 when none | `irr_activeTray` | 0.00 | 1.00 | 0.00 |
| trays served since the day rolled over | `irr_servedToday` | 0.00 | 0.00 | 1.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | no | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 0.00 days | 0.00 days | 20689.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 0.00 L | 6.00 L | 0.00 L |
| drained back out of the tray being served | `irr_drained_L` | 0.00 L | 0.00 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 0.00 Lpm | 36.00 Lpm | 0.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 0.00 L | 6.00 L | 102.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 0.00 Lpm | 0.00 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 0.00 L | 0.00 L | 83.32 L |

### What was allowed

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no | no |
| a tray would not fill in time | `alarm_irr_fill` | no (nothing has written it) | no (nothing has written it) | no |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | no (nothing has written it) | no |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | no (nothing has written it) | no |
| leak in irrigation circuit | `alarm_water_leak` | no | no | no |
| water pressure too low | `alarm_water_pressure` | no | no | no |

<!-- page -->

## Damp air against dry

The same morning twice, at 92 % and at 25 %: the modulation is the whole of this greenhouse's seasons, and it moves both the dose and the interval.

**The weather it held.** ceiling 18.0 degC, floor 17.0 degC, crawlspace 15.0 degC, outside 14.0 degC, humidity 92 % / 90 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAY.

### What happened

**1. The damp morning**


**2. The dry morning**


### What it measured

| what it is | row | 1 | 2 |
|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 14.00 degC | 24.00 degC |
| phase of the day in force | `cli_phase` | DAY | DAY |
| minutes since sunrise; negative before it | `sol_fromRise_min` | 90.00 min | 90.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.16 kPa | 2.38 kPa |

### What it decided

| what it is | row | 1 | 2 |
|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.25 | 1.00 |
| modulated interval, for the tray due next | `irr_period_days` | 27.80 days | 14.00 days |
| modulated dose, for the tray being served | `irr_volume_L` | 75.55 L | 150.00 L |

### What it moved

| what it is | row | 1 | 2 |
|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | yes | yes |
| irrigation output valve 1 | `irr_output_ro1` | no | no |
| tray 1 valve | `irr_tray1` | yes | yes |
| tray 2 valve | `irr_tray2` | no | no |

### What the water did

| what it is | row | 1 | 2 |
|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | FILLING | FILLING |
| tray being served, 0 when none | `irr_activeTray` | 1.00 | 1.00 |
| trays served since the day rolled over | `irr_servedToday` | 0.00 | 0.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 0.00 days | 0.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 20.00 L | 20.00 L |
| drained back out of the tray being served | `irr_drained_L` | 0.00 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 48.00 Lpm | 48.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 20.00 L | 20.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 0.00 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 0.00 L | 0.00 L |

### What was allowed

| what it is | row | 1 | 2 |
|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no |
| a tray would not fill in time | `alarm_irr_fill` | no (nothing has written it) | no (nothing has written it) |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | no (nothing has written it) |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | no (nothing has written it) |
| leak in irrigation circuit | `alarm_water_leak` | no | no |
| water pressure too low | `alarm_water_pressure` | no | no |

<!-- page -->

## A tray that will not drain

The fill arrives and only a tenth of it comes back: a clog is a MEASUREMENT, made against the dose that tray actually got.

**The weather it held.** ceiling 16.0 degC, floor 15.0 degC, crawlspace 14.0 degC, outside 13.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAWN_BREEZE.

### What happened

**1. The tray filled, and now draining**

- the fill itself was fine: the water went in, and the alarm below is about where it went afterwards

**2. The drain window closed on a clog**

- the drain window closes with a tenth of the dose recovered, which is under what the tray owes even with the margin allowed
- ...and the per-tray row says WHICH one, so somebody goes to the right end of the plant

**3. Moving on to the next tray anyway**

- the cycle carries on regardless: a blocked tray is not a reason to leave the other seven dry

### What it measured

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 13.00 degC | 13.00 degC | 13.00 degC |
| phase of the day in force | `cli_phase` | DAWN_BREEZE | DAWN_BREEZE | DAWN_BREEZE |
| minutes since sunrise; negative before it | `sol_fromRise_min` | -30.00 min | -30.00 min | -30.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.51 kPa | 0.51 kPa | 0.51 kPa |

### What it decided

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.51 | 0.51 | 0.51 |
| modulated interval, for the tray due next | `irr_period_days` | 19.63 days | 19.63 days | 19.63 days |
| modulated dose, for the tray being served | `irr_volume_L` | 107.00 L | 0.00 L | 107.00 L |

### What it moved

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | no | yes | yes |
| irrigation output valve 1 | `irr_output_ro1` | yes | yes | no |
| tray 1 valve | `irr_tray1` | yes | no | no |
| tray 2 valve | `irr_tray2` | no | no | yes |

### What the water did

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | DRAINING | FLUSHING | FILLING |
| tray being served, 0 when none | `irr_activeTray` | 1.00 | 0.00 | 2.00 |
| trays served since the day rolled over | `irr_servedToday` | 1.00 | 1.00 | 1.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | no | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 20689.00 days | 20689.00 days | 20689.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 107.00 L | 107.00 L | 7.00 L |
| drained back out of the tray being served | `irr_drained_L` | 4.90 L | 10.80 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 0.00 Lpm | 36.00 Lpm | 36.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 107.00 L | 112.00 L | 120.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 30.00 Lpm | 39.91 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 4.90 L | 16.00 L | 17.47 L |

### What was allowed

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no | no |
| a tray would not fill in time | `alarm_irr_fill` | no | no | no |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | yes | yes |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | yes | yes |
| leak in irrigation circuit | `alarm_water_leak` | no | no | no |
| water pressure too low | `alarm_water_pressure` | no | no | no |

<!-- page -->

## A leak, and a main with no water in it

Both are read off the inlet meter alone: water moving with every valve shut, and water not arriving with the inlet open.

**The weather it held.** ceiling 16.0 degC, floor 15.0 degC, crawlspace 14.0 degC, outside 13.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase NIGHT.

### What happened

**1. A litre a minute with everything shut**

- the middle of the night: no cycle is running, so every valve in the circuit is shut and anything the meter counts is going somewhere nobody opened

**2. Filling, with nothing coming out of the main**

- the main is repaired, and gives no water at all: the mirror image, raised while the inlet is open and nothing is arriving
- ...and the leak is not one any more: the meter is counting into a tray somebody opened on purpose

**3. The fill given up on, and no clog claimed**

- and the fill gives up at its timeout rather than holding the inlet open on a fault, which is how a tray overflows
- ...and it goes STRAIGHT to the drain, past the soak: a soak steeps a tray in the dose it was given, and this one got nothing
- ...and a fill that delivered nothing gets no drain verdict either: a clog is a measurement, and there is nothing to measure

### What it measured

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 13.00 degC | 13.00 degC | 13.00 degC |
| phase of the day in force | `cli_phase` | NIGHT | DAWN_BREEZE | DAWN_BREEZE |
| minutes since sunrise; negative before it | `sol_fromRise_min` | -240.00 min | -240.00 min | -240.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.51 kPa | 0.51 kPa | 0.51 kPa |

### What it decided

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.51 | 0.51 | 0.51 |
| modulated interval, for the tray due next | `irr_period_days` | 0.00 days (held, its inputs are gone) | 19.63 days | 19.63 days |
| modulated dose, for the tray being served | `irr_volume_L` | 0.00 L | 107.00 L | 0.00 L |

### What it moved

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | no | yes | yes |
| irrigation output valve 1 | `irr_output_ro1` | no | no | yes |
| tray 1 valve | `irr_tray1` | no | yes | no |
| tray 2 valve | `irr_tray2` | no | no | no |

### What the water did

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | IDLE | FILLING | FLUSHING |
| tray being served, 0 when none | `irr_activeTray` | 0.00 | 1.00 | 0.00 |
| trays served since the day rolled over | `irr_servedToday` | 0.00 | 0.00 | 0.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | no | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 0.00 days | 0.00 days | 0.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 0.00 L | 0.00 L | 0.00 L |
| drained back out of the tray being served | `irr_drained_L` | 0.00 L | 0.00 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 12.00 Lpm | 0.00 Lpm | 0.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 1.00 L | 1.00 L | 1.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 0.00 Lpm | 0.00 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 0.00 L | 0.00 L | 0.00 L |

### What was allowed

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | no | no |
| a tray would not fill in time | `alarm_irr_fill` | no (nothing has written it) | no (nothing has written it) | yes |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | no (nothing has written it) | no |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | no (nothing has written it) | no |
| leak in irrigation circuit | `alarm_water_leak` | yes | no | no |
| water pressure too low | `alarm_water_pressure` | no | yes | no |

<!-- page -->

## The supply going down mid-fill

Ten valves, and they do not all fail the same way: the inlet shuts and the drains open, so a tray half full empties instead of sitting there.

**The weather it held.** ceiling 16.0 degC, floor 15.0 degC, crawlspace 14.0 degC, outside 13.0 degC, humidity 70 % / 80 % outside, sky 0.0 mm/h, wind 1.0 m/s, phase DAWN_BREEZE.

### What happened

**1. Filling, on a plant with power**


**2. Parked, with the tray draining itself**

- the UPS says its battery is going, and the lockout parks the whole plant - irrigation with it, through the machinery a halt uses
- each valve where its own row says a dead controller leaves it: the inlet shut, the drain and the trays open

**3. The supply back, and the fill started again**

- and the plant comes back when the battery does: the cycle starts again from IDLE rather than resuming a fill it stopped counting

### What it measured

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| exterior temperature, voted and filtered | `cli_temp_ext` | 13.00 degC | 13.00 degC | 13.00 degC |
| phase of the day in force | `cli_phase` | DAWN_BREEZE | DAWN_BREEZE | DAWN_BREEZE |
| minutes since sunrise; negative before it | `sol_fromRise_min` | -30.00 min | -30.00 min | -30.00 min |
| dryness tracker: VPD averaged over irr_dryEwmaDays | `irr_vpdAvg_kPa` | 0.51 kPa | 0.51 kPa | 0.51 kPa |

### What it decided

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation modulation, irr_modMin (wet) to 1 (summer dry) | `irr_mod` | 0.51 | 0.51 | 0.51 |
| modulated interval, for the tray due next | `irr_period_days` | 19.63 days | 19.63 days | 19.63 days |
| modulated dose, for the tray being served | `irr_volume_L` | 107.00 L | 107.00 L | 107.00 L |

### What it moved

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| irrigation input valve 1 | `irr_input_ro1` | yes | no | yes |
| irrigation output valve 1 | `irr_output_ro1` | no | yes | no |
| tray 1 valve | `irr_tray1` | yes | yes | yes |
| tray 2 valve | `irr_tray2` | no | yes | no |

### What the water did

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| what the irrigation cycle is doing | `irr_state` | FILLING | IDLE | FILLING |
| tray being served, 0 when none | `irr_activeTray` | 1.00 | 0.00 | 1.00 |
| trays served since the day rolled over | `irr_servedToday` | 0.00 | 0.00 | 0.00 |
| the day the schedule is running against, 0 before the clock is set | `irr_today` | 20689.00 days | 20689.00 days | 20689.00 days |
| water now, then clear this flag | `irr_forceNow` | no | no | no |
| force watering this tray next cycle | `irr_forceOn_tray5` | no | no | no |
| force skipping this tray next cycle | `irr_forceOff_tray1` | no | no | no |
| last watering (days since 1 Jan 1970) | `irr_last_tray1` | 0.00 days | 0.00 days | 0.00 days |
| delivered into the tray being filled | `irr_delivered_L` | 26.00 L | 27.00 L | 6.00 L |
| drained back out of the tray being served | `irr_drained_L` | 0.00 L | 0.00 L | 0.00 L |
| inlet flow, differentiated from the counter | `irr_inFlow_Lpm` | 36.00 Lpm | 0.00 Lpm | 36.00 Lpm |
| inlet water counter, since it was last zeroed | `irr_inTotal_L` | 26.00 L | 28.00 L | 34.00 L |
| outlet flow, averaged over irr_out_avg_ms | `irr_outFlow_Lpm` | 0.00 Lpm | 0.00 Lpm | 0.00 Lpm |
| outlet water counter, since it was last zeroed | `irr_outTotal_L` | 0.00 L | 0.00 L | 0.00 L |

### What was allowed

| what it is | row | 1 | 2 | 3 |
|---|---|---|---|---|
| supply unhealthy: actuators held off | `sys_supply_lockout` | no | yes | no |
| a tray would not fill in time | `alarm_irr_fill` | no (nothing has written it) | no (nothing has written it) | no (nothing has written it) |
| a tray would not drain in time | `alarm_irr_drain` | no (nothing has written it) | no (nothing has written it) | no (nothing has written it) |
| tray 1 would not drain in time | `alarm_irr_drain_tray1` | no (nothing has written it) | no (nothing has written it) | no (nothing has written it) |
| leak in irrigation circuit | `alarm_water_leak` | no | no | no |
| water pressure too low | `alarm_water_pressure` | no | no | no |

<!-- page -->

## What the set reaches

Taken together the cases exercise every tier of the arbitration the plant actually has, in the order the plant resolves it:

- **The safe state**, raised by the supply lockout and released with it. Every program is asked to park through the same machinery a halt uses, and each one drives its OWN actuators to the position its table row declares - the fabric out, the fans at speed 1, the skirt shut.
- **The weather interlocks**, which own the roof outright: rain shuts it while the cooling ladder is still asking for it wide open, and the lockout outlives the shower that raised it. The gust interlock is the same path and the same row, tested where a case can state a wind speed.
- **The permissions**, which are not interlocks but gates on a demand: the intake gates on the air outside being worth drawing, the screen on the phase of the day, the skirt on the air being dry enough for a wet pad to cool rather than only wet.
- **The ladders themselves**, both halves of each: cooling and ground heat, humidify and dehumidify, and the staging that turns one demand into a roof position, three fan speeds and a valve duty.

Beside them, the failure modes: an instrument dying (the plant carries on from the level that still answers, and says it is working on a reading it does not have), a loop held against its limit for ten minutes (which costs no recovery time it did not need), a reading parked exactly on a threshold with noise either side of it (which moves nothing at all), a tray that will not drain, a main with no water in it, and water moving with every valve shut.

The irrigation cases run against a MODEL of the pipework rather than a canned meter reading: it watches the valves the cycle drove and answers at the two instruments the way the circuit would - a pulse a litre at the inlet, a 4-20 mA rate at the drain. What each case changes is the plumbing, and the cycle finds out about it the way it would on site.

## What it does not reach

- **The sun.** `sun_watch` needs a fix and a wall clock; the day phase is an input here and the two programs have cases of their own.
- **A season.** The dryness tracker folds once an hour over several days, so what the irrigation cases move is the air the tracker has just adopted, not a fortnight of it. What a fortnight does to the average is arithmetic, and it has a case of its own.
- **The field bus.** The rows are published and read directly, so what is under test is the control system and not the Modbus driver that fills those rows on hardware.

