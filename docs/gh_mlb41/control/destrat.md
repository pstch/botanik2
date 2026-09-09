# Destratification {#gh_mlb41_destrat}

<!-- source of truth: programs/climate/destrat, thresholds in tables/params.csv.
     Adapted from the spec's "Destratification". -->

![](destratification.svg)

A simple three-band regulator with hysteresis on the gap between the high and
low interior temperatures: `cli_dTStratLow_degC` (2 degC),
`cli_dTStratMed_degC` (4 degC) and `cli_dTStratHigh_degC` (6 degC) select fan
speeds 1, 2 and 3. After a speed change the gap must move by
`cli_dTStratHyst_degC` before the speed may change again, which keeps the fans
from hunting.

@note The spec gives that hysteresis as 2 degC; the shipped default is 1 degC.

Given the fans installed and the air volume, stratification should always be
controllable.

The fans are positioned to pull air from the bottom upward, in summer as in
winter. That looks wrong at first, but the plants are too close to the fans:
blowing air straight at them in summer would dry them too fast and put them in
danger. To spread the flow properly, deflectors are needed above each fan, one
per fan, to push the air out toward the sides of the greenhouse. Getting the
shape right would properly need a dynamic simulation; that costs more than this
project can carry, so intuition it is. Papier mache under resin is the current
thought, and not yet settled.

## Control programs

| program   | every   | what it does                                                                                                 |
|-----------|---------|--------------------------------------------------------------------------------------------------------------|
| `destrat` | control | The destratification ladder: three bands with hysteresis on the ceiling-to-floor gap, published as a demand. |
