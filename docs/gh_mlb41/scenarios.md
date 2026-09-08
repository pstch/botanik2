# Worked scenarios {#gh_mlb41_scenarios}

<!-- source of truth: scripts/scenarios.tsv, and the SimScenario enum in
     src/sites/gh_mlb41/sim.h that names them.

     Run one:      scripts/sim_scenario.sh <name> [when]
     Recapture:    python3 scripts/screens.py --scenarios

     Each screen is HOME ten simulated minutes after the hour the scenario
     starts at, on 2026-07-01. `cycle` has no screen: it runs a different one of
     these each simulated day. -->

## fair

<!-- from 06:00: an ordinary day: dawn, the roof opening, an irrigation cycle -->

@verbinclude fair.txt

## hot

<!-- from 18:00: cli_tooHot_degC, the whole cooling ladder, the cold bank it arms -->

@verbinclude hot.txt

## cold

<!-- from 02:00: cli_tooCold_degC, the screen deployed, the roof shut -->

@verbinclude cold.txt

## storm

<!-- from 11:00: 75 kmph sustained: the roof shut, alarm_weather_storm -->

@verbinclude storm.txt

## damp

<!-- from 02:00: air at saturation: alarm_rot, the skirt refusing to add water -->

@verbinclude damp.txt

## dry

<!-- from 11:00: the humidifier arm of the ladder -->

@verbinclude dry.txt

## probes

<!-- from 03:50: 04:00 one ridge probe dies, 05:00 a second: the quorum, then not -->

@verbinclude probes.txt

## supply

<!-- from 08:50: 09:00 a sag, 13:00 the grid gone and the generator on, 14:00 the UPS -->

@verbinclude supply.txt
