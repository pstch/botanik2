# Actuators, and who gets them {#gh_mlb41_actuators}

<!-- source of truth: src/sites/gh_mlb41/programs/actuators/, rates in
     tables/programs.csv. The arbitration prose is the spec's "Logique
     d'arbitrage". -->

The three climate ladders and irrigation share four actuators: the windows, the
intake fans, the circulation fans and the skirt valve. More than one subsystem
can want the same actuator at the same time, so there has to be a rule.

Which rule depends on what the competing demands are asking for:

- **Demands in the same direction** - "more air movement", say - take the
  **maximum**: the actuator follows the strongest. More stirring or more
  incoming air generally does no harm, so the highest demand wins without
  conflict.
- **Demands in opposite directions** - open against shut, run against cut - have
  no meaningful maximum, so a **priority** settles it and one subsystem wins
  outright.

Each actuator resolves in this order:

1. **Safety and mechanical lockouts.** Absolute priority, no appeal. Rain or
   wind shuts the windows; a screen in motion cuts the fans.
2. **Directional priorities**, for the open-against-shut conflicts.
3. **The maximum of what is left**, for the same-direction demands, which act as
   floors.

Concretely:

- **Circulation fans**: the lockout while the screen is moving comes first,
  otherwise the maximum of the destratification demand, the demand raised while
  a tray is being watered (`cli_fanIrrDem`) and the cooling demand.
- **Intake fans**: cooling, anti-rot ventilation and humidifying or
  dehumidifying all want incoming air, so the maximum of the three is used.
- **Windows**: when cooling wants them open and the anti-dryness logic wants
  them shut, **cooling wins** - dry air does not endanger *Haworthia*, heat
  does.
- **Skirt valve**: the anti-rot lockout comes first and forces it shut. Between
  the cooling demand and the anti-dryness humidifying demand, the stronger is
  used.

That principle - **maximum for intensities, priority for directions** - is what
guarantees two subsystems never drive the same actuator in opposite directions
within one scan.

## windows_controller

The four roof windows. Each is a 24 V linear actuator with built-in limits,
driven by reversing polarity, so position is estimated from travel time and only
a seating stroke against a limit proves 0 or 1. `cli_window_pos1..4` is that
estimate, `cli_window_tgt1..4` what the arbitration asked for.

It is one of the site's two parking programs: on the way out of service it keeps
being called until the roof has finished shutting, rather than being dropped
wherever it stood.

## roof_lockout

The safety that outranks everything on the windows: rain, or wind past the
shut threshold, closes them and holds them closed.

## screen_controller

The thermal screen, deployed on negative cooling demand and permitted only by
the phase - see @ref gh_mlb41_climate.

It deliberately does not park. An actuator whose safe state is where it already
is has nothing to travel to, and a screen left as it stands harms nothing.

## fan_controller

The four ceiling fans, three speeds each, driven by the arbitration above.
`cli_fanDemHyst` is the demand change needed before a speed switch, so the fans
do not hunt at a band edge.

## intractor_controller

The two intake fans, on a 0-10 V command, from the maximum of the three demands
that want incoming air - and gated on whether the crawlspace air is actually on
the right side of the greenhouse temperature.

## skirt_controller

The skirt valve, duty-cycled to approximate a proportional command from an
all-or-nothing valve, with the anti-rot lockout able to force it shut.
