# The gh_mlb41 greenhouse {#gh_mlb41}

<!-- Translated and adapted from design/gh_mlb41_spec.odt (French, July 2026).
     Where the spec and the firmware disagree, the firmware wins and the
     difference is noted. -->

A horticultural greenhouse of mostly *Haworthia*, at Le Moulin Bleu, 34 Rue de
Vernouillet, 41160 Saint-Jean-de-Froidmentel, in the Loir-et-Cher.

![](greenhouse.png)

Before the control system, the plants lived in conditions that were not good for
them and took a great deal of manual work and heavy handling to follow the
seasons. This unit automates most of that, and holds conditions in which the
plants grow, cross, set seed and can be sold.

## What it has to be reliable enough for

A catastrophic loss of the crop - to rot, to overheating, to drowning - is the
thing the design is built against. That is why the 24 V DC system is fed from
two independent channels, and why one phase of the 230/380 V three-phase 32 A
supply is backed by a 5 kW petrol generator that starts itself.

It is also why the unit has to show its state plainly, and raise alarms for the
conditions that need somebody to come. That needs an internet connection of its
own, provided by a 4G modem.

## Design principles

- **The day is cut into phases**, and a phase sets the targets: temperature,
  humidity, stratification. A phase never drives an actuator. It sets the target
  and arms or disarms the three control ladders.
- **Strategy is anticipated, safety is fed back.** The sun's position and the
  daily temperature bias place the system ahead of the weather; the sensors
  correct what actually happened. A missed prediction costs little, because
  feedback catches it - which is what lets the unit work without a weather
  forecast, and without the complexity one would bring.
- **The plants come before the fuel bill.** Frost and rot kill in a moment; a
  merely suboptimal temperature does not.
- **There are no seasons.** Behaviour comes from astronomy (sun position, peak
  solar elevation) and from measured temperature. Heating is fixed at 5 °C by
  the standalone heater, and cooling always aims below 20 °C. The seasons do
  show up in irrigation, which is cut back in humid weather so the plants are
  not overwatered when the air is already wet.
- **Irrigation is one tray at a time**, on the inlet, outlet and per-tray
  valves. A tray is filled to a set volume (inlet open, tray open, outlet shut),
  then left to drain (inlet shut, tray open, outlet open). That cycle is slow
  enough that only a few trays can be served in a day.

- @subpage gh_mlb41_plant
- @subpage gh_mlb41_principles
- @subpage gh_mlb41_sense
- @subpage gh_mlb41_climate
- @subpage gh_mlb41_irrigation
- @subpage gh_mlb41_actuators
- @subpage gh_mlb41_blocks
- @subpage gh_mlb41_alarms
- @subpage gh_mlb41_scenarios
- @subpage gh_mlb41_maintenance
- @subpage gh_mlb41_variables
