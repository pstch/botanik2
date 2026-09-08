# Sensing {#gh_mlb41_sense}

<!-- source of truth: src/sites/gh_mlb41/programs/sense/, rates in
     tables/programs.csv. The physics these implement - the median vote, the
     EWMA, the dew point and the VPD - is @ref gh_mlb41_principles. -->

Twelve temperature and humidity probes, in groups of three: three high and three
low inside, three in the crawlspace, three outside. Each group is voted to a
median and smoothed, and losing one probe raises that group's quorum alarm
without taking the greenhouse out of control.

Between levels the least favourable reading is the one carried forward: the
highest temperature for cooling, the smallest dew-point margin for rot, the
lowest VPD for irrigation.

## temp_rh_sensors

## temp_rh_zone

## strat_sensor

## climate_watch

## weather_watch

## wx_sensors

## ac_grid_watch

## ac_local_watch

## dc_watch

## Solar position

<!-- not a site program: SunPositionTracker is library-owned and bound by
     BotanikFirmware. It is here because the sol_ rows it publishes are what
     day_phase_controller reads. -->

