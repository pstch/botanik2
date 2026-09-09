# Actuators {#gh_mlb41_actuators}

<!-- source of truth: src/sites/gh_mlb41/programs/actuators/, rates in
     tables/programs.csv. -->

The three climate ladders and irrigation share four of these, so more than one
subsystem can want the same actuator at once. What settles that is
@ref gh_mlb41_arbitration.

## What each one is

**The windows.** Four 24 V linear actuators with built-in limits, driven by
reversing polarity, so position is estimated from travel time and only a seating
stroke against a limit proves 0 or 1. `cli_window_pos1..4` is that estimate,
`cli_window_tgt1..4` what the arbitration asked for. One moves at a time.

**The thermal screen.** One coil, deployed on negative cooling demand and only
where the phase permits - see @ref gh_mlb41_phases.

**The ceiling fans.** Three relayed speeds. `cli_fanDemHyst` is the demand change
needed before a speed switch, so they do not hunt at a band edge.

**The intake fans.** A 0-10 V command, gated on whether the crawlspace air is
actually on the right side of the greenhouse temperature.

**The skirt valve.** All-or-nothing, duty-cycled to approximate a proportional
command, with the anti-rot lockout able to force it shut.

## Parking

Two of these finish what they were doing on the way out of service rather than
being dropped where they stood: the windows, so the roof is shut before the unit
halts, and the irrigation manifold, so no tray is stranded with its inlet open.

The screen deliberately does not. An actuator whose safe state is where it
already is has nothing to travel to.

## Control programs

| program        | every   | what it does                                                                                                                   |
|----------------|---------|--------------------------------------------------------------------------------------------------------------------------------|
| `windows`      | control | The four roof windows: one target from the cooling ladder, and one moving at a time.                                           |
| `roof_lockout` | control | What forbids the roof being open at all, whatever the ladder asks.                                                             |
| `screen`       | control | The thermal screen: one coil, deployed when the greenhouse is losing heat rather than gaining it.                              |
| `fans`         | control | The two destratification fans: three relayed speeds, driven by whichever of three demands wants the most.                      |
| `intractors`   | control | The two intractors: variable-speed fans drawing air from under the floor, driven by whichever of three demands wants the most. |
| `skirt`        | control | The wet skirt: one valve pulsed against two demands.                                                                           |
