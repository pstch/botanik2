# Control logic {#gh_mlb41_control}

<!-- scope: the spec's "Logique de controle", "Logique d'irrigation" and
     "Logique d'arbitrage". Source of truth for every threshold below is
     src/sites/gh_mlb41/tables/params.csv; the laws are under programs/. -->

Three ladders shape the climate, and they share a form: each reduces its
quantity to a demand, and the actuators arrange themselves along it. A phase of
the day sets the target and arms the ladder; the ladder decides how hard.

| ladder                                   | acts on                          | measure               | regulator                          |
|------------------------------------------|----------------------------------|-----------------------|------------------------------------|
| @ref gh_mlb41_cooling "cooling"          | warming and cooling              | dT degC, signed       | PID plus trigger thresholds        |
| @ref gh_mlb41_destrat "destratification" | stratification                   | dTstrat degC          | 3-band hysteresis                  |
| @ref gh_mlb41_humidity "humidity"        | condensation, or extreme dryness | dew-point margin degC | 4-band hysteresis with a dead zone |

Each rung normalises the same way:

    demand = clamp((value - low) / (high - low), 0, 1)

For the signed ladders - cooling and humidity - that form applies either side of
zero, a positive branch and a negative one, separated by a dead zone, giving a
demand between -1 and 1.

Irrigation has no ladder; it is modulated by a continuous measure of evaporative
power instead. See @ref gh_mlb41_irrigation.

- @subpage gh_mlb41_phases
- @subpage gh_mlb41_cooling
- @subpage gh_mlb41_destrat
- @subpage gh_mlb41_humidity
- @subpage gh_mlb41_irrigation
- @subpage gh_mlb41_arbitration
- @subpage gh_mlb41_blocks
