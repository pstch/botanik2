# Irrigation {#gh_mlb41_irrigation}

<!-- source of truth: src/sites/gh_mlb41/programs/irrigation/, rates in
     tables/programs.csv. Prose adapted from the spec's "Logique d'irrigation";
     the thresholds were checked against tables/params.csv. -->

![](irrigation.svg)

Irrigation is fairly separate from the three climate ladders, but not
independent of them:

- **when** water goes on is decided by the climate: hot weather waters at the
  end of the night, cold weather at the end of the morning;
- **how much and how often** is cut back when the air is humid - more precisely,
  when the vapour pressure deficit is low.

The trays are sub-irrigated. A tray is filled to a set volume with the inlet
open, the tray open and the outlet shut, then left to drain with the inlet shut,
the tray open and the outlet open. That cycle is slow enough - the dose and the
drain time decide it - that only a few trays can be served in a day.

## Per tray

For each of the 8 trays:

- **the frequency and the volume**, in days and litres - `irr_days_tray1..8` and
  `irr_liters_tray1..8`;
- **a manual force**, `irr_forceOn_tray1..8` and `irr_forceOff_tray1..8`, to make
  the next cycle water that tray or skip it, once.

The frequency and volume given are **summer** values: hot, dry. Both are
modulated automatically as the air gets more humid, from the tracked vapour
pressure deficit.

## Scheduling

Beyond the per-tray settings, a few parameters govern the whole cycle:

- **`irr_traysPerDay`** (2) caps how many trays are served in a day. If more are
  due on the same day, the surplus waits for tomorrow. A minimum spread between
  trays emerges from that cap on its own, though it is worth keeping some margin
  so waterings do not drift too far apart in a dry spell.
- **`irr_dryEwmaDays`** (3 days) is the dryness smoothing window. The modulation
  does not react to a single hour's weather but to a running average of the
  vapour pressure deficit over a few days, so one dry afternoon cannot trigger
  excessive watering.
- **`irr_dawnGate_degC`** (12 degC) is a floor before dawn: below it, end-of-night
  watering is suspended so cold plants are not watered, and the cycle moves to
  the start of the day instead.
- **`irr_forceNow`** commands a cycle immediately, generally used together with
  the per-tray force parameters.

The dryness tracking runs on a moving average updated **once an hour**. Not
every scan - too frequent, and the filter would freeze numerically - and not
once a day, where the result would depend on the hour the measurement was taken.
That interval is not a parameter, to keep the moving average simple.

## Modulation

Volume and frequency, configured per tray, are summer values. As the air gets
more humid the system cuts watering back, on a normalised demand from the mean
vapour pressure deficit:

    demand = clamp((VPD_mean - irr_dryLo_kPa) / (irr_dryHi_kPa - irr_dryLo_kPa), 0, 1)
    mod    = irr_modMin + (1 - irr_modMin) * demand ^ irr_modGamma

`mod` runs between `irr_modMin` (0.25 - humid weather, least water) and 1 - dry
summer weather, full watering. `irr_dryLo_kPa` (0.15) and `irr_dryHi_kPa` (1.20)
are the VPD range over which the demand runs from 0 to 1.

That factor acts on both volume and period, and `irr_volFreqSplit` (0.5) decides
how it is shared:

    volume = summer_volume * mod ^ irr_volFreqSplit
    period = summer_period / mod ^ (1 - irr_volFreqSplit)

The important property: total water rate - volume over period - is proportional
to `mod` whatever `irr_volFreqSplit` is. So the split does not change how much
water is delivered, only how:

- more often in smaller doses, with the split near 0;
- more rarely in larger doses, with it near 1.

For safety, `irr_modGamma` >= 1 guarantees that watering climbs toward its
maximum only once dryness is properly established. The modulation always fails
to the dry side - less water - never toward excess.

## The cycle

One tray at a time: fill, soak, drain, flush. The fill is judged against what
the inlet meter reports and the drain against the outlet, and a tray that will
not fill or will not empty inside its window raises `alarm_irr_fill_tray1..8`
or `alarm_irr_drain_tray1..8`.

The manifold does not park, and needs nothing on the way out. Leaving the scan
mid-cycle drops every valve to its declared value, and those are wired
active-low: the inlet shuts, the drain opens, every tray opens, and the water
goes where it was going to go anyway. See @ref gh_mlb41_actuators.

## The meters

The inlet is a pulse train, one pulse per whole litre, differentiated into a
rate and totalised into a volume. The outlet is a 4-20 mA Hall-effect flow
meter, so the pipework answers with a rate directly; it is averaged and then
deadbanded.

A rate that is deadbanded only writes when it moves more than the band, so a
flow decaying toward zero would otherwise leave the row parked on the last
figure it reported - a drain meter saying water is moving with every valve
shut, which is the one thing a leak detector must not be told.

## Control programs

| program          | every   | what it does                                                                   |
|------------------|---------|--------------------------------------------------------------------------------|
| `irr_schedule`   | control | Which tray is owed water, and whether the climate will allow a cycle to start. |
| `irr_modulation` | control | The irrigation ladder: how thirsty the air has been, as one factor.            |
| `irrigation`     | control | Irrigation: one tray at a time, filled to a dose, steeped, and drained.        |
| `irr_valves`     | control | The irrigation manifold: which valves each state of the cycle opens.           |
| `irr_mtr_input`  | control | The inlet meter, and the two supply faults it alone can see.                   |
| `irr_mtr_output` | control | The outlet meter: what came back off the trays.                                |
