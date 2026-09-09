# The day and its phases {#gh_mlb41_phases}

<!-- source of truth: programs/climate/day_phase and target_temp, rates in
     tables/programs.csv. Adapted from the spec's "Machine a etat journaliere". -->

A phase of the day is what sets the target the ladders work against, and what
says whether the thermal screen may move. A phase never drives an actuator.

## The day's phases

The day is cut into 6 phases, each defined as an interval in minutes before or
after sunrise or sunset.

After a hot day, the morning is allowed to give up a little temperature. Then,
in the early morning, a light breeze blows for a few minutes. The day runs. After
a cold night, the late afternoon is allowed to gain a little, then before sunset
a light breeze blows again, and the night runs.

|    | phase               | when                                                     | target  | why                                                                      |
|----|---------------------|----------------------------------------------------------|---------|--------------------------------------------------------------------------|
| P1 | `PHASE_NIGHT`       | after sunset                                             | 20 degC | normal operation                                                         |
| P2 | `PHASE_COLD_BANK`   | shortly before sunrise, only if the previous day was hot | 14 degC | pre-cool at the end of the night, so a typically hot day starts cooler   |
| P3 | `PHASE_DAWN_BREEZE` | from sunrise to a few minutes after                      | 20 degC | the morning breeze, helping the plants cross from night to day           |
| P4 | `PHASE_DAY`         | until late afternoon                                     | 20 degC | normal operation                                                         |
| P5 | `PHASE_HEAT_BANK`   | late in the day, until shortly before sunset             | 27 degC | bank heat at the end of the day, so a typically cold night starts warmer |
| P6 | `PHASE_DUSK_BREEZE` | until sunset                                             | 20 degC | the evening breeze, from day to night                                    |

`COLD_BANK` and `HEAT_BANK` are conditional, and each has parameters for its
length, its trigger and its effect on the target.

## The temperature target

In normal operation - NIGHT, DAY, DAWN_BREEZE, DUSK_BREEZE - the cooling target
is the base value, `cli_coolSet_degC`, 20 degC by default, and the ladder works
on `dT = T - target`.

The two banking phases shift that target and then clamp it:

    COLD_BANK: target = max(cli_coolSet_degC - cli_coldBankOfs_degC, cli_targetMin_degC)
    HEAT_BANK: target = min(cli_coolSet_degC + cli_heatBankOfs_degC, cli_targetMax_degC)

The floor `cli_targetMin_degC` (14 degC) and the ceiling `cli_targetMax_degC`
(27 degC) guarantee that banking never pushes the greenhouse to a dangerous
temperature, however aggressively the offsets are set.

Both phases are conditional on what yesterday did:

- **COLD_BANK** arms only if the previous day's high went above
  `cli_coldBankTrg_degC` (24 degC);
- **HEAT_BANK** arms only if the previous night's low fell below
  `cli_heatBankTrg_degC` (10 degC).

So the greenhouse pre-cools before dawn only after a hot day, and banks heat in
the late afternoon only after a cold night. A clear late-spring day can trigger
**both** - hot day, cold night - and they are handled independently.

`cli_targetTau_s` (300 s) smooths the target so a phase change does not step it.

## The thermal screen

The screen is driven by negative cooling demand, past
`cli_thermalScreenDem` (-0.2), but whether it may move at all depends on the
phase. The phase gives permission; the demand decides the wanted movement.

| phase                               | the screen may |
|-------------------------------------|----------------|
| `NIGHT`, `DUSK_BREEZE`, `HEAT_BANK` | **deploy**     |
| `DAY`, `DAWN_BREEZE`, `COLD_BANK`   | **retract**    |

The rule follows the direction of the day: the screen may deploy when the night
is coming or heat is being kept, and retract when the day is coming or the house
is being pre-cooled. A passing call for heat in the middle of a cold day
therefore never closes the screen against the sun, and a mild night never
reopens it and loses the heat it was holding.

When the demand asks for a movement the phase forbids, the screen stays where it
is and the thermal need is met, if at all, by drawing warm air from the ground
through the intake fans.

## Control programs

| program       | every   | what it does                                                                                                                   |
|---------------|---------|--------------------------------------------------------------------------------------------------------------------------------|
| `day_phase`   | control | The day's phase, and the temperature target it puts in force.                                                                  |
| `target_temp` | control | The temperature the greenhouse is being held to: the base setting, shifted by whichever bank phase is both in force and armed. |
