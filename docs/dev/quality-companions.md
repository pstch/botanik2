# Which quality reads earn a `_qlt` companion

Decided 2026-08-30. 40-odd `.valid()` reads across 21 programs, judged one at
a time against one test:

> `Ref::get()` already substitutes the row's DECLARED value. So a quality read
> earns a companion row only when the law must do something STRUCTURALLY
> different - not merely compute on a different number.

**12 rows keep a `_qlt`. Everything else goes.**

## Keep

| row | who needs it | why substitution is not enough |
|---|---|---|
| `cli_rain_mmph_qlt` | roof_lockout | declared 0 mm/h reads as FINE WEATHER and opens the roof. An unmeasured sky must shut it. The most safety-critical of the twelve |
| `cli_wind_mps_qlt` | roof_lockout | same: declared 0 m/s is calm |
| `cli_coolDem_qlt` | screen, intake_gates | the screen deploys at `cli_thermalScreenDem` = **-0.2** and the declared demand is **+0.20**, so its safe state is unreachable by substitution. The gates must SHUT, which no demand value expresses |
| `cli_temp_in_qlt` | cooling, climate_watch | cooling RESETS the PID integral - an act, not a value. climate_watch would raise a spurious COLD on the declared 14 degC |
| `cli_dpMargin_degC_qlt` | humidity, climate_watch | the rot guard fails toward ACTING: unknown means full vent, and the declared 2.0 gives a high but partial one. climate_watch would raise a spurious HUMID |
| `cli_phase_qlt` | target_temp, irr_schedule | an unplaced phase must apply NO bank offset. The declared PHASE_NIGHT would apply one |
| `cli_temp_hi_qlt` | temp_rh_zone | the worst-case vote is among MEASURED levels. A level holding an old reading must not speak for the house |
| `cli_temp_lo_qlt` | temp_rh_zone | the other half of that vote |
| `irr_inTotal_L_qlt` | irrigation_cycle | a totalizer DIFFERENCE cannot take a substituted constant at either end. `Ref::held()` is what reads it; the companion is what flags the volume |
| `irr_outTotal_L_qlt` | irrigation_cycle | the drain half of the same |
| `irr_outFlow_Lpm_qlt` | irrigation_cycle | the drain verdict DECLINES TO JUDGE on a silent meter. Declared 0 Lpm reads as a clog |
| `irr_mod_qlt` | irrigation_cycle | the cycle still runs on the declared modulation - the companion is what marks the dose a guess |

## Drop, and why

- **fan_controller** - `strat_dem.valid() ? get() : low_dem`. `cli_stratDem`
  declares 0.30 and `cli_fanLowDem` is 0.2; the ladder floors at the low band
  either way. The test computes the answer it already had.
- **intractor, skirt** - an unknown cooling demand contributes 0 through the
  ladder, which is what the explicit test forced.
- **destrat** - `bands.reset()` on an unmeasured gap. The substituted gap is a
  CONSTANT, so the staircase is deterministic without the reset.
- **strat_sensor** - flags its output only, and its one reader (destrat) has
  just dropped the read. Nobody is left to care.
- **day_phase** `temp_ok` - flags `prev_day_max` / `prev_night_min`, which
  nothing reads structurally.
- **weather_watch** - `_seen`, the storm gate and the dry clock. Declared 0
  wind never storms; the clock simply stops advancing.
- **climate_watch** `strat.valid()` - the stratification alarm at declared
  2.0 degC sits under `cli_dTStratHi`, so it does not fire.
- **dc_watch, irr_mtr_output, irr_modulation** - `measured ? filter(x) :
  filter.value()`. A filter must not eat a substituted constant, but that is
  the BLOCK's business: it becomes a `_qlt` INPUT on the filter, not a read in
  the law.
- **intake_dry_gate, ac_supply** - the four-probe `have` and the volts/Hz
  verdict both resolve to "the block did not produce", which the block says on
  its own outputs.

## Shape

`<row>_qlt : BOOL` beside the row, TRUE when believable. In ST it reads like
any other input:

```
blind := NOT cli_rain_mmph_qlt OR NOT cli_wind_mps_qlt;
```

The bank keeps them in step - nothing in ST may WRITE a `_qlt` it did not
earn, which is the one rule this design has to enforce.

## What it costs

12 rows against ~40 reads, so **most of the validity logic goes away**. The
survivors are the ones where getting it wrong opens a roof in the rain, calls
for heat into a house nobody can see, or reports a clog that is not there.
