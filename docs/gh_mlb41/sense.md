# Sensing {#gh_mlb41_sense}

<!-- source of truth: src/sites/gh_mlb41/programs/sense/. The program
     descriptions below are the desc column of tables/programs.csv. The physics
     these implement is @ref gh_mlb41_principles. -->

Twelve temperature and humidity probes, in groups of three: three high and three
low inside, three in the crawlspace, three outside. Each group is voted to a
median and smoothed, so one probe stuck high or low can never be the value used.
Losing one raises that group's quorum alarm without taking the greenhouse out of
control.

Between levels the least favourable reading is carried forward: the highest
temperature for cooling, the smallest dew-point margin for rot, the lowest VPD
for irrigation. The system fails to the safe side with no extra logic.

Alongside the probes sit the two weather stations, the four DC rails, and the
two electricity meters - one on the mains, one on the generator-backed side.

## Solar position

Not a site program: `SunPositionTracker` is library-owned and bound by
`BotanikFirmware`. It is here because the `sol_` rows it publishes are what the
day's phases are cut from.

## Control programs

| program | every | what it does |
|---|---|---|
| `temp_rh_sensors` | sense | The four measurement locations, each voted down to one reading and filtered: what the greenhouse is, before any law is set in it. |
| `temp_rh_zone` | sense | The psychrometrics every other law is set in, off the two interior levels: one temperature for the house, and the worst case per quantity. |
| `strat` | sense | The stratification gap: how much warmer the ceiling is than the floor, unsigned. |
| `wx_sensors` | sense | The two weather stations, voted down to one reading per quantity: the worse of them, because two cannot say which is lying. |
| `climate_watch` | control | The interior conditions, watched: the alarms that are a reading crossing a line rather than a law failing to hold one. |
| `weather_watch` | control | The weather station, watched: whether it is answering, and whether what it says is a storm. |
| `ac_grid_watch` | control | The utility feed, watched. |
| `ac_local_watch` | control | The generator-backed side of the transfer switch, watched. |
| `dc_watch` | control | The DC rails and the two batteries, watched - and the plant-wide lockout they raise. |
