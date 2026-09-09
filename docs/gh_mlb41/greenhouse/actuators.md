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

Most of these need nothing on the way out of service, because the wiring
already puts them where they belong. A de-energised coil is the only thing a
dead PLC can guarantee, so an actuator that must fail to an asserted position is
wired active-low and declared that way.

Everything except the windows takes a single module relay driving an external
two-contact changeover relay, so one coil decides the position:

| coil de-energised  | leaves it  |
|--------------------|------------|
| `cli_screen`       | deployed   |
| `irr_input_ro1/2`  | inlet shut |
| `irr_output_ro1/2` | drain open |
| `irr_tray1` .. `8` | tray open  |
| `cli_skirtValve`   | shut       |

So the manifold empties itself with no firmware involved: the inlet shuts, the
drain opens, every tray opens, and what is in them runs to the buried drain.

The circulation fans are the exception to the pattern. Their safe state is
speed 1 RUNNING, because still air is itself a rot risk, and that is the one
state here that needs the coil energised - so it is a safe state the firmware
asserts, not one the wiring falls into.

The windows are why `windows` parks. They take two coils, open and close, and
both de-energised means the motor stopped wherever it stood: there is no
position a dead PLC leaves them in. The program keeps being called while the
roof travels, and the unit holds in STOP until it answers.

They are the only one. Nothing else here parks, the manifold included: an
actuator whose safe state is where the wiring already puts it has nothing to
travel to.

## Control programs

| program        | every   | what it does                                                                                                                   |
|----------------|---------|--------------------------------------------------------------------------------------------------------------------------------|
| `windows`      | control | The four roof windows: one target from the cooling ladder, and one moving at a time.                                           |
| `roof_lockout` | control | What forbids the roof being open at all, whatever the ladder asks.                                                             |
| `screen`       | control | The thermal screen: one coil, deployed when the greenhouse is losing heat rather than gaining it.                              |
| `fans`         | control | The two destratification fans: three relayed speeds, driven by whichever of three demands wants the most.                      |
| `intractors`   | control | The two intractors: variable-speed fans drawing air from under the floor, driven by whichever of three demands wants the most. |
| `skirt`        | control | The wet skirt: one valve pulsed against two demands.                                                                           |
