# The installation {#gh_mlb41_plant}

<!-- From the spec's "Caractéristiques de la serre" and "Architecture du
     système de contrôle". The TODOs are the spec's own: those figures were
     never filled in, and inventing them would be worse than leaving them. -->

## The house

|                        |                           |
|------------------------|---------------------------|
| Floor area             | TODO m x TODO m = TODO m2 |
| Height, maximum / mean | TODO m / TODO m           |
| Interior volume        | TODO m3                   |

The volume is what gives the thermal mass of the air and its renewal rate.

## Envelope

|                                 |                                                    |
|---------------------------------|----------------------------------------------------|
| Glazed area                     | TODO m2                                            |
| Glazing                         | single glass, TODO mm                              |
| Opaque walls                    | TODO m2, cob - insulating and thermal mass at once |
| Summer whitening, in place      | TODO to TODO                                       |
| Irradiance cut by the whitening | TODO                                               |

## Openings and ventilation

|                                           |                             |
|-------------------------------------------|-----------------------------|
| Free area, windows fully open             | TODO m2                     |
| Where the openings are                    | the sloped roof panels only |
| Travel time, one window                   | TODO s                      |
| Intake fans                               | 2 x 1800 m3/h nominal       |
| Air renewal rate                          | TODO volumes/h              |
| Circulation fans, nominal at full speed   | TODO m3/h                   |
| Thermal screen travel, deployed to stowed | TODO s                      |

## Thermal mass

The floor, layer by layer, is TODO (timber under the insulation), TODO (the
insulation), TODO (timber under the tiling), TODO (tiling), TODO (resin). The
growing trays hold about TODO cm3 of soil between them, and the heavy walls are
straw-clay, about TODO kg.

## Observed behaviour

A few days of watching gives these directly, and they are enough to size the
process gain and the time constants the PID needs.

|                                          |             |
|------------------------------------------|-------------|
| Peak interior temperature, hot day, shut | TODO degC   |
| Interior minus exterior, shut, full sun  | TODO degC   |
| Interior minus exterior, open, full sun  | TODO degC   |
| Rate of rise, clear morning, no cooling  | TODO degC/h |
| Settling time, intake fans at 100%       | TODO min    |

The initial slope gives the air's time constant; the settling time gives the
ventilation's.

## Where it stands

|                                         |                                                   |
|-----------------------------------------|---------------------------------------------------|
| Latitude, longitude                     | TODO                                              |
| Nearby shade                            | TODO - describe the shading from the nearby trees |
| Prevailing wind and exposure            | TODO                                              |
| Irrigation water source and temperature | TODO degC                                         |
| Crawlspace temperature, summer / winter | TODO degC / TODO degC                             |

## Power

Everything runs on 24 V DC, from two independent DC supplies - one of them fed
by the standby generator - joined through a diode module to a UPS rated for at
least an hour on its own.

With the generator, that keeps most of the actuators working through a mains
failure:

- **the PLC, the valves and the window actuators** are behind both generator and
  UPS, so a power cut does not disturb the controller at all;
- **the circulation fans, the intake fans and the dehumidifier** are behind the
  generator only, so they stop for as long as it takes to start - 15 to 30
  seconds.

During that start the anti-rot defence is briefly without its fans, which is
acceptable here.

The generator has a starting battery of its own, and its voltage is watched.

## Subsystems

Six of them:

- **heating**, from a standalone petrol heater;
- **cooling**, from motorised windows, circulation fans, air intake fans and
  evaporative cooling, alongside an air conditioner kept for the hottest days;
- **destratification**, from the ceiling fans;
- **humidity control**, from the fans, from regulating irrigation and airflow,
  alongside a standalone dehumidifier - and in the extreme, the petrol heater
  serves as one;
- **irrigation**, from motorised valves on 8 sub-irrigated trays, filling and
  draining them, modulated in frequency and volume by how dry the air has been;
- **notification**, to reach the operator: flashing lamps, a beeper, and a
  message over the internet.

The petrol heater and the supplementary dehumidifier are entirely autonomous and
use their own sensors. Their trigger points have to be set to agree with this
system's configuration.

The other four share sensors and actuators, and the rules that settle a conflict
between them are in @ref gh_mlb41_arbitration.

## Sensors

- **12 temperature and humidity probes**, in groups of 3: three high and three
  low inside the greenhouse, three in the crawlspace under it inside the
  evaporative skirt, and three outside in spots sheltered from disturbance.
- **1 wind, rain and solar irradiance station**, outside.
- **4 DC voltage inputs**: DC supply 1 and 2, the UPS battery, the generator
  battery.
- **1 three-phase multifunction meter** on the mains supply.
- **1 single-phase multifunction meter** on the generator-backed circuit.
- **1 pulse water meter** on the irrigation inlet.
- **1 Hall-effect flow meter** on the irrigation outlet.

They reach the controller over Modbus RTU directly, or through an analogue input
module (0-10 V or 4-20 mA).

Solar irradiance is recorded only. Nothing controls on it; it is there to help
tune the rest.

## Actuators

- **4 linear actuators** opening 4 roof windows;
- **4 ceiling fans** for destratification and air movement;
- **2 air intake fans**, 1800 m3/h nominal each, drawing air into the greenhouse
  from the crawlspace under its floor - the space wrapped in summer by a wetted
  skirt, which is what makes the evaporative cooling;
- **1 motorised valve** feeding the skirt, from an agricultural pump drawing on
  the nearby river;
- **1 motorised valve** on the irrigation inlet, from the town water;
- **1 motorised valve** on the irrigation outlet, to a buried drain beside the
  greenhouse;
- **1 motorised valve per growing tray**.

They are driven over Modbus RTU directly, through a 0-10 V analogue output
module, or through a 10 A relay output module - optionally chained to a second
two-contact changeover relay, or to a 240 V AC relay.

Three details worth knowing:

- The motorised valves and linear actuators are 24 V DC units with built-in
  limits, driven on two wires by reversing polarity. Everything that does not
  need a partial opening - all of them except the windows - uses an external
  two-contact changeover relay, which halves the relays needed on the modules
  and simplifies the wiring.
- The intake fans take a 0-10 V analogue command.
- The circulation fans are three-speed, with a separate winding per speed,
  driven by two groups of 3 SPDT relays which in turn drive external relays in
  the junction boxes that feed the fans at 240 V AC.
