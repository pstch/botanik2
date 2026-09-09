# Worked scenarios {#gh_mlb41_scenarios}

<!-- source of truth: scripts/scenarios.tsv, and the SimScenario enum in
     src/sites/gh_mlb41/sim.h that names them. The one-line description under
     each heading is that file's third column. -->

The simulator can be run on a named day, one that puts a particular part of the
plant under load. Each is a whole day: the hour below is where it starts, which
is the hour it has something to show.

    scripts/sim_scenario.sh <name> [when]

`when` is `2026-07-01T18:30:00Z`, or `18:30Z` for that time on the default day.
Omitted, the scenario starts at its own hour.

The screen under each is HOME ten simulated minutes in, on 2026-07-01. There is
a tenth day, `cycle`, with no screen of its own: it runs a different one of
these each simulated day.

## fair

Starts at 06:00. An ordinary day: dawn, the roof opening, an irrigation cycle.

@verbinclude fair.txt

## hot

Starts at 18:00. Past `cli_tooHot_degC`: the whole cooling ladder, and the cold
bank it arms.

@verbinclude hot.txt

## cold

Starts at 02:00. Past `cli_tooCold_degC`: the screen deployed, the roof shut.

@verbinclude cold.txt

## storm

Starts at 11:00. 75 kmph sustained: the roof shut, `alarm_weather_storm`.

@verbinclude storm.txt

## damp

Starts at 02:00. Air at saturation: `alarm_rot`, the skirt refusing to add water.

@verbinclude damp.txt

## dry

Starts at 11:00. The humidifier arm of the ladder.

@verbinclude dry.txt

## probes

Starts at 03:50. At 04:00 one ridge probe dies and at 05:00 a second: the quorum
holds, then it does not.

@verbinclude probes.txt

## supply

Starts at 08:50. At 09:00 a sag, at 13:00 the grid gone and the generator on, at
14:00 the UPS.

@verbinclude supply.txt
