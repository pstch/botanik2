# Cooling {#gh_mlb41_cooling}

<!-- source of truth: programs/climate/cooling and intake_gates, thresholds in
     tables/params.csv. Adapted from the spec's "Refroidissement". -->

![](cooling.svg)

Every process on this ladder shares one path: get the hot air out, replace it
with cooler air from under the floor. The ladder also has a slight heating role,
because when the demand goes negative the intake fans run slowly so that
incoming air is warmed by the ground, and when it goes further negative the
thermal screen is allowed to deploy.

A PID sets the cooling effort from the conditions, and the equipment arranges
itself along it. In the shipped configuration:

| demand                              | what happens                                                                                                  |
|-------------------------------------|---------------------------------------------------------------------------------------------------------------|
| below `cli_thermalScreenDem` (-0.2) | the thermal screen deploys, at night or with the sun low                                                      |
| below `cli_groundHeatDem` (-0.1)    | intake fans run slow with the windows shut, only if the crawlspace air is warmer than the greenhouse          |
| above `cli_windowStartDem` (0.1)    | the windows begin to open                                                                                     |
| above `cli_fanLowDem` (0.2)         | the circulation fans start at speed 1                                                                         |
| above `cli_intakeStartDem` (0.3)    | the intake fans start, only if the crawlspace air is cooler than the greenhouse                               |
| above `cli_fanMedDem` (0.4)         | the circulation fans go to speed 2                                                                            |
| above `cli_windowFullDem` (0.5)     | the windows are fully open                                                                                    |
| above `cli_skirtCoolDem` (0.7)      | the crawlspace skirt is wetted - in practice a little above, because the valve has a minimum command interval |
| above `cli_fanHighDem` (0.8)        | the circulation fans go to speed 3                                                                            |
| above `cli_intakeFullDem` (0.9)     | the intake fans reach full speed                                                                              |

Every threshold is a parameter and can be moved without reprogramming, though a
good deal of modelling went into them. The estimate is that outside temperatures
up to 35 degC are manageable without risk to the plants, and that days above
40 degC remain dangerous.

@note The two associated alarms, `alarm_too_hot` and `alarm_too_cold`, sit above
and below the standby heating and cooling systems. They mean the greenhouse has
left the margins it is allowed, not that a ladder is working hard.

@note The PID's P/I/D terms - `cli_coolKp`, `cli_coolTi_sec`, `cli_coolTd_sec` -
are set during commissioning. They are hard to determine and need a rigorous
approach: real tests, then Ziegler-Nichols or Lambda-T.

## Control programs

| program        | every   | what it does                                                                                                      |
|----------------|---------|-------------------------------------------------------------------------------------------------------------------|
| `cooling`      | sense   | The cooling ladder: a PID on the interior temperature against the phase's target, published as one signed demand. |
| `intake_gates` | control | The two intake gates: whether crawlspace air is worth drawing, for cooling and for ground heat.                   |
