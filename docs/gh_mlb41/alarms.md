# Alarms {#gh_mlb41_alarms}

<!-- source of truth: tables/alarms.csv. Codes, dwell and registers are in
     @ref gh_mlb41_variables; this page is what each one means and what to do.
     The spec listed these under names from the old PLC IDE convention
     (too_hot_flt, ceilSns_flt); the names below are the firmware's. -->

The site declares these on top of the platform alarms the library brings. Each
row carries its own dwell policy: how long the condition must hold before it is
announced, how long it must clear before it is withdrawn, and how long an
episode is held visible after the fact.

## Climate

| code | row | what it means | what to do |
|---|---|---|---|
| `TOO_HOT` | `alarm_too_hot` | interior temperature above the margin the plants are allowed | check the emergency cooling |
| `TOO_COLD` | `alarm_too_cold` | interior temperature below it | check the standalone heater |
| `TOO_HUM` | `alarm_too_humid` | interior humidity too high | check the standalone dehumidifier |
| `ROT` | `alarm_rot` | dew-point margin too low: condensation and rot risk | check the standalone dehumidifier |
| `STRAT` | `alarm_strat` | stratification gap the fans are not closing | check or replace the destratification fans |

`TOO_HOT` and `TOO_COLD` sit above and below the standby heating and cooling.
They mean the greenhouse has left its allowed margins, not that a ladder is
working hard.

## Sensors

| code | row | what it means |
|---|---|---|
| `SNS_CEIL` | `alarm_sns_ceiling` | the ceiling temperature/RH probes lost quorum |
| `SNS_FLR` | `alarm_sns_floor` | the floor probes lost quorum |
| `SNS_CS` | `alarm_sns_crawlspace` | the crawlspace probes lost quorum |
| `SNS_EXT` | `alarm_sns_exterior` | the exterior probes lost quorum |
| `SNS_FLT` | `alarm_sensor_fault` | a probe is faulty |
| `WX_FLT` | `alarm_wx_station` | the wind and rain station is not answering |
| `ZONE_BLND` | `alarm_zone_blind` | no interior temperature at all: every cooling law is running on a held reading |

A point has three probes and the median is taken, so one failure does not move
the value. It does raise that point's quorum alarm: the greenhouse stays
controlled, and the operator knows to act before a second probe goes. See
@ref gh_mlb41_principles.

## Water

| code | row | what it means |
|---|---|---|
| `FILL_FLT` | `alarm_irr_fill` | a tray would not fill in its window |
| `DRAIN_FLT` | `alarm_irr_drain` | a tray would not drain in its window |
| `FILL1` .. `FILL8` | `alarm_irr_fill_tray1..8` | which tray would not fill |
| `DRAIN1` .. `DRAIN8` | `alarm_irr_drain_tray1..8` | which tray would not drain |
| `WTR_LK` | `alarm_water_leak` | flow at the outlet with every valve shut |
| `WTR_BAR` | `alarm_water_pressure` | water pressure too low |

## Weather

| code | row | what it means |
|---|---|---|
| `WH_STORM` | `alarm_weather_storm` | storm conditions: check the windows are properly closed |

A storm here is sustained wind past the threshold, not a passing shower or a
gust over the vent-closing limit. Those are reasons to shut the roof, which is a
much lower bar than the weather being severe.

## Supply

| code | row | what it means | what to do |
|---|---|---|---|
| `AC_GRID` | `alarm_ac_grid` | grid AC out of spec or offline | check the mains breaker and the utility feed |
| `AC_LOCAL` | `alarm_ac_local` | local AC out of spec or offline | check the local feed |
| `GEN_FLT` | `alarm_gen` | the generator failed to start | inspect the generator and its starting battery |
| `GEN_BATT` | `alarm_generator_battery` | generator starting battery low | check or replace it |
| `DC_PS1` | `alarm_dc_ps1` | DC supply 1 volts too low | check the utility feed, or replace the supply |
| `DC_PS2` | `alarm_dc_ps2` | DC supply 2 volts too low | check the local feed, or replace the supply |
| `DC_UPS` | `alarm_dc_ups` | UPS battery low | restore mains and check the battery |
