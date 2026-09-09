# Arbitration {#gh_mlb41_arbitration}

<!-- source of truth: src/sites/gh_mlb41/programs/actuators/. Adapted from the
     spec's "Logique d'arbitrage". -->

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

The actuators themselves are in @ref gh_mlb41_actuators.
