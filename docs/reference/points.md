# gh_mlb41: variables and programs

The point list for `gh_mlb41`: every row the firmware exposes, what it is
for, and where an integrator finds it. Maintained by hand - the generator
that first wrote it has been retired.

494 rows: 430 declared by the site, 64 inherited from libbotanik, 2 restated.

| kind | rows |
|---|---|
| Parameters | 225 |
| Alarms | 52 |
| Status values | 109 |
| Direct inputs | 71 |
| Analog inputs | 5 |
| Digital inputs | 2 |
| Analog outputs | 2 |
| Relay outputs | 28 |

## Parameters

### gh_mlb41 (177)

| name | type | default | accepts | unit | access | reg | description | notes |
|---|---|---|---|---|---|---|---|---|
| cli_coldBankOfs_degC | T_F32 | 6.0 | 0.0 .. 15.0 | degC | RW | 100 | how much cooler before a hot day | - |
| cli_heatBankOfs_degC | T_F32 | 7.0 | 0.0 .. 15.0 | degC | RW | 101 | how much warmer before a cold night | - |
| cli_heatBankTrg_degC | T_F32 | 10.0 | 6.0 .. 18.0 | degC | RW | 102 | yesterday's low below this: bank heat | - |
| cli_coldBankTrg_degC | T_F32 | 24.0 | 12.0 .. 40.0 | degC | RW | 103 | yesterday's high above this: bank cold | - |
| cli_coolSet_degC | T_F32 | 20.0 | 10.0 .. 30.0 | degC | RW | 104 | base cooling target temperature | every cooling stage keys off this |
| cli_coolKp | T_F32 | 0.08 | 0.0 .. 1.0 | - | RW | 105 | cooling PID proportional gain | leave as set |
| cli_coolTi_sec | T_F32 | 200.0 | 0.0 .. 3600.0 | sec | RW | 106 | cooling PID integral time | leave as set |
| cli_coolTd_sec | T_F32 | 0.0 | 0.0 .. 10.0 | sec | RW | 107 | cooling PID derivative time | leave at zero |
| cli_ventTempMgn_degC | T_F32 | 1.0 | 0.0 .. 15.0 | degC | RW | 108 | intake air must be this much cooler | - |
| cli_ventHyst_degC | T_F32 | 0.5 | 0.0 .. 5.0 | degC | RW | 148 | how far a gate has to close before it opens | the three intake gates are comparisons of two temperatures that converge; without a band they flip beat to beat |
| cli_skirtDryMin_degC | T_F32 | 4.0 | 0.0 .. 10.0 | degC | RW | 109 | air this dry lets the pad cool | - |
| cli_thermalScreenDem | T_F32 | -0.2 | -1.0 .. 0.0 | - | RW | 110 | cooling effort that deploys the screen | negative demand = heating |
| cli_groundHeatDem | T_F32 | -0.1 | -1.0 .. 0.0 | - | RW | 111 | cooling effort that draws ground air | negative demand = heating |
| cli_groundSpeed | T_F32 | 0.3 | 0.0 .. 1.0 | - | RW | 128 | intractor speed while pulling ground heat | the spec's low speed: the ladder's negative half has one rung, not a ramp |
| cli_windowStartDem | T_F32 | 0.1 | 0.0 .. 1.0 | - | RW | 112 | cooling effort that opens windows | - |
| cli_intakeStartDem | T_F32 | 0.3 | 0.0 .. 1.0 | - | RW | 113 | cooling effort that starts intake fans | - |
| cli_windowFullDem | T_F32 | 0.5 | 0.0 .. 1.0 | - | RW | 114 | cooling effort at fully open windows | - |
| cli_skirtCoolDem | T_F32 | 0.7 | 0.0 .. 1.0 | - | RW | 115 | cooling effort that starts the skirt | - |
| cli_fanLowDem | T_F32 | 0.2 | 0.0 .. 1.0 | - | RW | 116 | cooling effort for fan speed 1 | - |
| cli_fanMedDem | T_F32 | 0.4 | 0.0 .. 1.0 | - | RW | 117 | cooling effort for fan speed 2 | > cli_fanLowDem |
| cli_fanHighDem | T_F32 | 0.8 | 0.0 .. 1.0 | - | RW | 118 | cooling effort for fan speed 3 | > cli_fanMedDem |
| cli_breezeDem | T_F32 | 0.2 | 0.0 .. 1.0 | - | RW | 149 | brassage floor during the two breezes | a light breeze at dawn and dusk: enough to dry the leaves, not to dry the plant |
| cli_intakeFullDem | T_F32 | 0.9 | 0.0 .. 1.0 | - | RW | 119 | cooling effort at full intake speed | - |
| cli_dTStratLow_degC | T_F32 | 2.0 | 0.0 .. 10.0 | degC | RW | 120 | stratification gap for fan speed 1 | - |
| cli_dTStratMed_degC | T_F32 | 4.0 | 0.0 .. 10.0 | degC | RW | 121 | stratification gap for fan speed 2 | - |
| cli_dTStratHigh_degC | T_F32 | 6.0 | 0.0 .. 10.0 | degC | RW | 122 | stratification gap for fan speed 3 | - |
| cli_dTStratHyst_degC | T_F32 | 2.0 | 0.5 .. 5.0 | degC | RW | 123 | gap change needed to switch fan speed | - |
| cli_dehumFull_degC | T_F32 | 3.0 | 0.0 .. 10.0 | degC | RW | 124 | dryness margin: dehumidify fully | < cli_dehumStart_degC |
| cli_dehumStart_degC | T_F32 | 4.5 | 0.0 .. 10.0 | degC | RW | 125 | dryness margin: dehumidify starts | > cli_dehumFull_degC |
| cli_humidStart_degC | T_F32 | 18.0 | 0.0 .. 30.0 | degC | RW | 126 | dryness margin: humidify starts | > cli_dehumStart_degC; skirt starts pulsing |
| cli_humidFull_degC | T_F32 | 28.0 | 0.0 .. 40.0 | degC | RW | 127 | dryness margin: humidify fully | > cli_humidStart_degC; skirt at full pulse |
| cli_ventDryMgn_degC | T_F32 | 1.0 | 0.0 .. 5.0 | degC | RW | 130 | outside air must be this much drier | - |
| cli_tooCold_degC | T_F32 | 4.0 | 0.0 .. 10.0 | degC | RW | 131 | alarm: too COLD | match the gas-heater thermostat |
| cli_tooHot_degC | T_F32 | 35.0 | 20.0 .. 50.0 | degC | RW | 132 | alarm: too HOT | - |
| cli_rotMargin_degC | T_F32 | 1.0 | 1.0 .. 5.0 | degC | RW | 133 | dryness margin: rot risk | < cli_dehumFull_degC |
| cli_intr_rawMin_mV | T_F32 | 0.0 | 0.0 .. 24000.0 | mV | RW | 134 | intractor raw value at zero speed | per intractor datasheet |
| cli_intr_rawMax_mV | T_F32 | 10000.0 | 0.0 .. 24000.0 | mV | RW | 135 | intractor raw value at full speed | per intractor datasheet |
| cli_intr_engMin | T_F32 | 0.0 | 0.0 .. 1.0 | - | RW | 136 | intractor speed at the low end | per intractor datasheet |
| cli_intr_engMax | T_F32 | 1.0 | 0.0 .. 1.0 | - | RW | 137 | intractor speed at the high end | - |
| cli_intr_min | T_F32 | 0.0 | 0.0 .. 1.0 | - | RW | 138 | lowest believable intractor speed | under this: a dead sensor |
| cli_intr_max | T_F32 | 1.0 | 0.0 .. 1.0 | - | RW | 139 | highest believable intractor speed | over this: a dead sensor |
| cli_windShutEnd_mps | T_F32 | 20.8 | 0.0 .. 50.0 | mps | RW | 140 | wind that shuts the roof and is a storm | 20.8 mps is 75 kmph |
| cli_windShutBegin_mps | T_F32 | 13.9 | 0.0 .. 50.0 | mps | RW | 161 | wind that starts capping the opening | 13.9 mps is 50 kmph |
| cli_windShutMax | T_F32 | 0.25 | 0.0 .. 1.0 | - | RW | 162 | how far the roof may open in that band | - |
| cli_windowTravel_sec | T_U16 | 15 | 1 .. 60 | sec | RW | 141 | time to fully open/close a window | per actuator datasheet |
| cli_windowTmo_sec | T_U16 | 20 | 1 .. 90 | sec | RW | 142 | safe time to fully open/close a window | per actuator datasheet |
| cli_screenTravel_sec | T_U16 | 60 | 1 .. 300 | sec | RW | 143 | time to deploy/remove the screen | per actuator datasheet |
| cli_screenTmo_sec | T_U16 | 80 | 1 .. 400 | sec | RW | 144 | safe time to deploy/remove the screen | per actuator datasheet |
| cli_rainLockout_sec | T_U16 | 300 | 0 .. 3600 | sec | RW | 145 | keep windows shut this long after rain | - |
| cli_skirtPeriod_sec | T_U16 | 60 | 10 .. 600 | sec | RW | 146 | skirt valve modulation period | humidify effort sets the on-time in it |
| cli_skirtMinOn_sec | T_U16 | 10 | 1 .. 600 | sec | RW | 147 | shortest skirt valve pulse | <= cli_skirtPeriod_sec; under it, stay off |
| sol_coldBankRise_min | T_I16 | -30 | -180 .. 60 | min | RW | 200 | cold banking starts, from sunrise | - |
| sol_breezeRise_min | T_I16 | 30 | -60 .. 180 | min | RW | 201 | dawn breeze starts, from sunrise | - |
| sol_dayRise_min | T_I16 | 60 | 0 .. 180 | min | RW | 202 | day operation starts, from sunrise | - |
| sol_heatBankSet_min | T_I16 | -180 | -300 .. 0 | min | RW | 203 | heat banking starts, from sunset | - |
| sol_duskSet_min | T_I16 | -15 | -120 .. 0 | min | RW | 205 | dusk breeze starts, from sunset | the breeze runs into the sunset; night follows at sol_nightSet_min |
| sol_nightSet_min | T_I16 | 30 | 0 .. 180 | min | RW | 204 | night operation starts, from sunset | - |
| irr_forceNow | T_BOOL | 0 | 0 .. 1 | - | RW | 250 | water now, then clear this flag | - |
| irr_liters_tray1..8 | T_U16 | 150 | 0 .. 500 | L | RW | 251..286 by 5 | litres to flood this tray in summer | - |
| irr_days_tray1..8 | T_U8 | 14 | 1 .. 60 | days | RW | 252..287 by 5 | days between waterings in summer | - |
| irr_last_tray1..8 | T_U32 | 0 | 0 .. 100000 | days | RW | 253..288 by 5 | last watering (days since 1 Jan 1970) | read to decide when the tray is due, stamped by the cycle once the fill completes. Editable: moving it skips or brings forward a watering |
| irr_forceOn_tray1..8 | T_BOOL | 0 | 0 .. 1 | - | RW | 254..289 by 5 | force watering this tray next cycle | - |
| irr_forceOff_tray1..8 | T_BOOL | 0 | 0 .. 1 | - | RW | 255..290 by 5 | force skipping this tray next cycle | - |
| irr_dryEwmaDays | T_U16 | 3 | 1 .. 30 | days | RW | 291 | days of dryness history set the dose | sampled every 2 h |
| irr_dryLo_kPa | T_F32 | 0.15 | 0.0 .. 2.0 | kPa | RW | 292 | air dryness giving the least water | - |
| irr_dryHi_kPa | T_F32 | 1.2 | 0.0 .. 5.0 | kPa | RW | 293 | air dryness giving the most water | > irr_dryLo_kPa |
| irr_modMin | T_F32 | 0.25 | 0.1 .. 1.0 | - | RW | 294 | least water given, in humid weather | the floor even in still, humid air |
| irr_modGamma | T_F32 | 1.0 | 1.0 .. 4.0 | - | RW | 295 | how fast watering ramps up with dryness | - |
| irr_volFreqSplit | T_F32 | 0.5 | 0.0 .. 1.0 | - | RW | 296 | cut the volume, or stretch the period | 0 = period, 1 = volume |
| irr_traysPerDay | T_U16 | 2 | 1 .. 5 | - | RW | 297 | most trays watered in one day | - |
| irr_dawnGate_degC | T_F32 | 12.0 | 5.0 .. 30.0 | degC | RW | 298 | skip pre-dawn watering below this | - |
| irr_morningWin_min | T_U16 | 180 | 30 .. 480 | min | RW | 314 | how long into the day the morning slot runs | the tray has to dry before night; watering at four in the afternoon does not |
| irr_maxFill_sec | T_U16 | 300 | 100 .. 1800 | sec | RW | 299 | alarm if a tray takes longer to fill | - |
| irr_soak_sec | T_U16 | 300 | 0 .. 3600 | sec | RW | 315 | how long a filled tray sits before draining | 0 skips the soak and opens the drain at the end of the fill |
| irr_drainWindow_sec | T_U16 | 300 | 100 .. 1800 | sec | RW | 300 | time allowed for a tray to drain | - |
| irr_expDrainFrac | T_F32 | 0.6 | 0.0 .. 1.0 | - | RW | 301 | fraction expected to drain back | - |
| irr_drainMargin | T_F32 | 0.3 | 0.0 .. 1.0 | - | RW | 302 | clog if drained is this far under | against irr_expDrainFrac, so 0.6 expected and 0.3 here clogs under 0.3 |
| irr_flush_sec | T_U16 | 10 | 5 .. 300 | sec | RW | 303 | how long the manifold is rinsed | after every drain: the inlet and the outlet share a line with the trays, and what sat in it is what the next tray gets first |
| irr_input_qtyPerImp | T_F32 | 1.0 | 0.01 .. 10.0 | L | RW | 304 | litres per pulse from the water meter | per meter datasheet |
| irr_out_rawMin_uA | T_U16 | 4000.0 | 0 .. 24000 | uA | RW | 305 | output meter uA at zero flow | 4 mA is the live zero of the loop, not a reading of nothing |
| irr_out_rawMax_uA | T_U16 | 20000.0 | 0 .. 24000 | uA | RW | 306 | output meter uA at full scale | per output-meter datasheet |
| irr_out_engMin_Lpm | T_F32 | 0.0 | 0.0 .. 500.0 | Lpm | RW | 307 | output litres/min at zero signal | per output-meter datasheet |
| irr_out_engMax_Lpm | T_F32 | 50.0 | 0.0 .. 500.0 | Lpm | RW | 308 | output litres/min at full scale | per output-meter datasheet |
| irr_out_min | T_F32 | 0.0 | 0.0 .. 500.0 | Lpm | RW | 309 | lowest believable output flow | under this: a dead sensor |
| irr_out_max | T_F32 | 50.0 | 0.0 .. 500.0 | Lpm | RW | 310 | highest believable output flow | over this: a dead sensor |
| irr_out_avg_ms | T_U16 | 200 | 0 .. 10000 | ms | RW | 311 | rolling average for this flow meter | per output-meter datasheet |
| irr_leak_Lpm | T_F32 | 0.5 | 0.1 .. 10.0 | Lpm | RW | 312 | inlet flow with the cycle idle is a leak | every valve is shut: whatever is moving is moving through something broken |
| irr_minFill_Lpm | T_F32 | 5.0 | 0.5 .. 50.0 | Lpm | RW | 313 | a fill under this rate is a pressure fault | the inlet is open and the water is not arriving: supply pressure, or a part-shut valve |
| pwr_dc1_rawMin_mV | T_U16 | 4000 | 0 .. 10000 | mV | RW | 400 | DC PS1 raw value at the low end | per divider setup |
| pwr_dc1_rawMax_mV | T_U16 | 6000 | 0 .. 10000 | mV | RW | 401 | DC PS1 raw value at the high end | per divider setup |
| pwr_dc1_engMin_V | T_F32 | 20.0 | 0.0 .. 48.0 | V | RW | 402 | DC PS1 volts at the low end | per divider setup |
| pwr_dc1_engMax_V | T_F32 | 30.0 | 0.0 .. 48.0 | V | RW | 403 | DC PS1 volts at the high end | per divider setup |
| pwr_dc1_min | T_F32 | 20.0 | 0.0 .. 48.0 | V | RW | 404 | lowest believable dc ps1 volts | under this: a dead sensor |
| pwr_dc1_max | T_F32 | 30.0 | 0.0 .. 48.0 | V | RW | 405 | highest believable dc ps1 volts | over this: a dead sensor |
| pwr_dc1_avg_ms | T_U16 | 1000 | 10 .. 10000 | ms | RW | 406 | rolling average for DC PS1 | per divider setup |
| pwr_dc2_rawMin_mV | T_U16 | 4000 | 0 .. 10000 | mV | RW | 407 | DC PS2 raw value at the low end | per divider setup |
| pwr_dc2_rawMax_mV | T_U16 | 6000 | 0 .. 10000 | mV | RW | 408 | DC PS2 raw value at the high end | per divider setup |
| pwr_dc2_engMin_V | T_F32 | 20.0 | 0.0 .. 48.0 | V | RW | 409 | DC PS2 volts at the low end | per divider setup |
| pwr_dc2_engMax_V | T_F32 | 30.0 | 0.0 .. 48.0 | V | RW | 410 | DC PS2 volts at the high end | per divider setup |
| pwr_dc2_min | T_F32 | 20.0 | 0.0 .. 48.0 | V | RW | 411 | lowest believable dc ps2 volts | under this: a dead sensor |
| pwr_dc2_max | T_F32 | 30.0 | 0.0 .. 48.0 | V | RW | 412 | highest believable dc ps2 volts | over this: a dead sensor |
| pwr_dc2_avg_ms | T_U16 | 1000 | 10 .. 10000 | ms | RW | 413 | rolling average for DC PS2 | per divider setup |
| pwr_upsBat_rawMin_mV | T_U16 | 4000 | 0 .. 10000 | mV | RW | 414 | UPS battery raw value at the low end | per divider setup |
| pwr_upsBat_rawMax_mV | T_U16 | 6000 | 0 .. 10000 | mV | RW | 415 | UPS battery raw value at the high end | per divider setup |
| pwr_upsBat_engMin_V | T_F32 | 20.0 | 0.0 .. 48.0 | V | RW | 416 | UPS battery volts at the low end | per divider setup |
| pwr_upsBat_engMax_V | T_F32 | 30.0 | 0.0 .. 48.0 | V | RW | 417 | UPS battery volts at the high end | per divider setup |
| pwr_upsBat_min | T_F32 | 20.0 | 0.0 .. 48.0 | V | RW | 418 | lowest believable ups battery volts | under this: a dead sensor |
| pwr_upsBat_max | T_F32 | 30.0 | 0.0 .. 48.0 | V | RW | 419 | highest believable ups battery volts | over this: a dead sensor |
| pwr_upsBat_avg_ms | T_U16 | 1000 | 10 .. 10000 | ms | RW | 420 | rolling average for the UPS battery | per divider setup |
| pwr_genBat_rawMin_mV | T_U16 | 5000 | 0 .. 10000 | mV | RW | 421 | gen. battery raw value at the low end | per divider setup |
| pwr_genBat_rawMax_mV | T_U16 | 7500 | 0 .. 10000 | mV | RW | 422 | gen. battery raw value at the high end | per divider setup |
| pwr_genBat_engMin_V | T_F32 | 20.0 | 10.0 .. 48.0 | V | RW | 423 | gen. battery volts at the low end | per divider setup |
| pwr_genBat_engMax_V | T_F32 | 30.0 | 15.0 .. 48.0 | V | RW | 424 | gen. battery volts at the high end | per divider setup |
| pwr_genBat_min | T_F32 | 20.0 | 10.0 .. 48.0 | V | RW | 425 | lowest believable gen. battery volts | under this: a dead sensor |
| pwr_genBat_max | T_F32 | 30.0 | 10.0 .. 48.0 | V | RW | 426 | highest believable gen. battery volts | over this: a dead sensor |
| pwr_genBat_avg_ms | T_U16 | 1000 | 10 .. 10000 | ms | RW | 427 | rolling average for the Gen. battery | per divider setup |
| pwr_gridLo_V | T_F32 | 200.0 | 0.0 .. 500.0 | V | RW | 428 | grid volts: low alarm | - |
| pwr_gridHi_V | T_F32 | 260.0 | 0.0 .. 500.0 | V | RW | 429 | grid volts: high alarm | - |
| pwr_gridLo_Hz | T_F32 | 47.0 | 0.0 .. 100.0 | Hz | RW | 430 | grid frequency: low alarm | - |
| pwr_gridHi_Hz | T_F32 | 53.0 | 0.0 .. 100.0 | Hz | RW | 431 | grid frequency: high alarm | - |
| pwr_localLo_V | T_F32 | 200.0 | 0.0 .. 500.0 | V | RW | 432 | local AC volts: low alarm | the generator-backed side |
| pwr_localHi_V | T_F32 | 260.0 | 0.0 .. 500.0 | V | RW | 433 | local AC volts: high alarm | - |
| pwr_localLo_Hz | T_F32 | 45.0 | 0.0 .. 100.0 | Hz | RW | 434 | local AC frequency: low alarm | wider than the grid: a generator wanders |
| pwr_localHi_Hz | T_F32 | 55.0 | 0.0 .. 100.0 | Hz | RW | 435 | local AC frequency: high alarm | wider than the grid: a generator wanders |
| pwr_dc1Lo_V | T_F32 | 22.0 | 0.0 .. 48.0 | V | RW | 436 | DC PS1 volts: low alarm | above pwr_dc1_min; that one says the sensor is dead |
| pwr_dc2Lo_V | T_F32 | 22.0 | 0.0 .. 48.0 | V | RW | 437 | DC PS2 volts: low alarm | above pwr_dc2_min; that one says the sensor is dead |
| pwr_upsBatLo_V | T_F32 | 23.0 | 0.0 .. 48.0 | V | RW | 438 | UPS battery volts: low alarm | the contact is what holds the plant; this only warns |
| pwr_genBatLo_V | T_F32 | 23.0 | 0.0 .. 48.0 | V | RW | 439 | gen. battery volts: low alarm | too low to crank the generator |
| pwr_genStart_sec | T_U16 | 30 | 5 .. 300 | sec | RW | 440 | the generator has this long to take over | grid gone and the ATS still dead past it: the set did not start |
| geo_lat_deg *(restated)* | T_F32 | 47.96 | -90.0 .. 90.0 | deg | RW | 941 | geographical latitude, in degrees | this greenhouse |
| geo_lon_deg *(restated)* | T_F32 | 1.24 | -180.0 .. 180.0 | deg | RW | 942 | geographical longitude, in degrees | this greenhouse |
| cli_groundDemHyst | T_F32 | 0.02 | 0.0 .. 0.5 | - | RW | 150 | demand given back before the rung lets go | - |
| cli_fanDemHyst | T_F32 | 0.05 | 0.0 .. 0.5 | - | RW | 151 | demand change needed to switch fan speed | - |
| cli_fanIrrDem | T_F32 | 0.34 | 0.0 .. 1.0 | - | RW | 152 | fan demand while a tray is being watered | - |
| cli_rotRelease_sec | T_U16 | 300 | 0 .. 3600 | s | RW | 153 | rot lockout overrun past the condition | - |
| cli_targetMin_degC | T_F32 | 14.0 | 0.0 .. 40.0 | degC | RW | 154 | coldest the target may be clamped to | - |
| cli_targetMax_degC | T_F32 | 27.0 | 0.0 .. 40.0 | degC | RW | 155 | warmest the target may be clamped to | - |
| cli_sensorTau_ms | T_U16 | 30000 | 1000 .. 60000 | ms | RW | 156 | probe filter time constant | - |
| cli_targetTau_s | T_U16 | 300 | 0 .. 3600 | s | RW | 160 | target temperature filter time constant | - |
| irr_flowBand_Lpm | T_F32 | 0.1 | 0.0 .. 5.0 | Lpm | RW | 157 | outlet flow change worth a status write | - |
| irr_flowCutoff_Lpm | T_F32 | 0.2 | 0.0 .. 5.0 | Lpm | RW | 158 | below this the outlet counts no litres | - |
| irr_rateWin_ms | T_U16 | 5000 | 500 .. 60000 | ms | RW | 159 | window the inlet rate is differenced over | - |

### Library-level (48)

Declared by the firmware, on every site that compiles the
feature. A site may restate any of them - the restated row
then appears above, in its own list.

| name | type | default | accepts | unit | access | reg | description | notes |
|---|---|---|---|---|---|---|---|---|
| monitor_baud | T_U32 | 115200 | 1200 .. 921600 | Bd | RW | 901 | monitor UART rate | log-only port; ignored on the OPTA, whose MONITOR is USB CDC |
| monitor_level | enum | Logs::LOG_DEBUG | `Logs::level_label` | - | RW | 905 | lowest level the monitor port carries | open by default: this port exists to be the full record |
| net_serial | T_U32 | 1 | 0 .. 9999 | - | RW | 910 | bump to apply staged net_ edits | - |
| net_enable | T_BOOL | 1 | 0 .. 1 | - | RW | 917 | bring the network up | off takes the link down; the field bus is unaffected |
| net_dhcp | T_BOOL | 0 | 0 .. 1 | - | RW | 911 | enable DHCP IP configuration | ignores the static ip/mask/gw rows |
| net_ip | text | 192.168.1.10 | STR_IPV4 | - | RW | 912 | static IP address | - |
| net_mask | text | 255.255.255.0 | STR_IPV4 | - | RW | 913 | static IP netmask | - |
| net_gw | text | 192.168.1.1 | STR_IPV4 | - | RW | 914 | static IP gateway | - |
| net_dns1..2 | text | n.n.n.n | STR_IPV4 .. | - | RW | 915..916 | static DNS resolver n |  .. |
| ntp_serial | T_U32 | 1 | 0 .. 9999 | - | RW | 920 | bump to apply staged ntp_ edits | - |
| ntp_enable | T_BOOL | 1 | 0 .. 1 | - | RW | 921 | enable NTP clock sync | - |
| ntp_poll_sec | T_U32 | 3600 | 10 .. 86400 | sec | RW | 922 | NTP poll interval, in seconds | - |
| ntp_port | T_U16 | 123 | 1 .. 65535 | - | RW | 923 | NTP server port | standard is 123 |
| ntp_srv1..2 | text | - | STR_HOST .. | - | RW | 924..925 | NTP server n | blank: falls back to net_gw. A default here would guess at somebody else's network .. |
| mqtt_serial | T_U32 | 1 | 0 .. 9999 | - | RW | 930 | bump to apply staged mqtt_ edits | - |
| mqtt_enable | T_BOOL | 1 | 0 .. 1 | - | RW | 931 | enable MQTT | - |
| mqtt_host | text | - | STR_HOST | - | RW | 932 | MQTT broker host | blank: falls back to net_gw. A default here would guess at somebody else's network |
| mqtt_port | T_U16 | 1883 | 1 .. 65535 | - | RW | 933 | MQTT broker port | standard is 1883 |
| mqtt_ka_sec | T_U16 | 20 | 10 .. 1800 | sec | RW | 934 | MQTT keep-alive interval | also the dead-broker detection |
| mqtt_tlm_sec | T_U32 | 30 | 0 .. 86400 | sec | RW | 935 | telemetry publish cadence | 0 stops publishing telemetry |
| mqtt_retain | T_BOOL | 1 | 0 .. 1 | - | RW | 936 | publish telemetry retained | on, the broker keeps serving a dead sensor's last good value |
| mqtt_level | enum | Logs::LOG_DEBUG | `Logs::level_label` | - | RW | 937 | lowest level published to the broker | every entry costs a publish, so this is the floor to raise first |
| geo_serial | T_U32 | 1 | 0 .. 9999 | - | RW | 940 | bump to apply staged geo_ edits | - |
| mb_serial | T_U32 | 1 | 0 .. 9999 | - | RW | 950 | bump to apply staged mb_ edits | - |
| mb_enable | T_BOOL | 1 | 0 .. 1 | - | RW | 951 | serve the Modbus TCP slave | - |
| mb_port | T_U16 | 502 | 1 .. 65535 | - | RW | 952 | Modbus TCP slave port | standard is 502 |
| mb_write_enable | T_BOOL | 1 | 0 .. 1 | - | RW | 953 | let a master write parameters | off makes the slave read-only |
| console_serial | T_U32 | 1 | 0 .. 9999 | - | RW | 960 | bump to apply staged console_ edits | - |
| console_baud | T_U32 | 4800 | 1200 .. 921600 | Bd | RW | 900 | console UART rate | reconnect at the new rate; unsaved, so a power cycle undoes it |
| console_type | T_U8 | 1 | 0 .. 3 | - | RW | 961 | which terminal is on the console | 0=VT100 1=VT102 2=Minitel 1B 3=Minitel 2 |
| console_cols | T_U16 | 80 | 80 .. 132 | col | RW | 962 | console width, in columns | the screen buffer bounds the maximum; a site needing another restates the row |
| console_rows | T_U16 | 24 | 20 .. 32 | row | RW | 963 | console height, in rows | the screen buffer bounds the maximum; a site needing another restates the row |
| console_home_level | enum | Logs::LOG_NOTICE | `Logs::level_label` | - | RW | 964 | lowest level HOME's log tail shows | a landing screen: only what is worth looking up for |
| console_shell_level | enum | Logs::LOG_INFO | `Logs::level_label` | - | RW | 965 | lowest level the SHELL page interleaves | `logs off` silences it entirely without moving this |
| console_logs_level | enum | Logs::LOG_DEBUG | `Logs::level_label` | - | RW | 966 | lowest level the LOGS page keeps | this page is the record; raising it loses the record |
| sys_cpu_load_pct | T_U8 | 50 | 1 .. 100 | % | RW | 970 | CPU_LOAD alarm threshold | percent of the task period |
| sys_jitter_pct | T_U8 | 50 | 1 .. 100 | % | RW | 971 | CPU_JITT alarm threshold | percent of the task period |
| sys_stack_pct | T_U8 | 75 | 1 .. 100 | % | RW | 972 | STACK alarm threshold | percent of the stack in use |
| sys_log_level | enum | Logs::LOG_DEBUG | `Logs::level_label` | - | RW | 973 | lowest level the logger forwards | the floor for EVERY sink; each page filters further |
| sys_retain_sec | T_U32 | 300 | 0 .. 86400 | sec | RW | 974 | how often retained values reach flash | 0 leaves only `save` and the way down; wear vs loss |
| rtu_timeout_ms | T_U32 | 300 | 20 .. 5000 | ms | RW | 980 | how long a slave has to answer | the bus default; a device model may declare its own |
| rtu_retries | T_U8 | 1 | 0 .. 5 | - | RW | 981 | immediate retries before a device is backed off | each costs a full timeout on a device that is really gone |
| rtu_backoff_ms | T_U32 | 1000 | 100 .. 60000 | ms | RW | 982 | first backoff after a device fails its retries | doubles per further failure, up to rtu_backoff_max_ms |
| rtu_backoff_max_ms | T_U32 | 60000 | 1000 .. 600000 | ms | RW | 983 | longest a backed-off device waits between probes | what decides whether one dead device costs the whole bus |
| rtu_write_min_ms | T_U32 | 200 | 20 .. 60000 | ms | RW | 984 | shortest interval between writes to one block | what stops a chattering demand monopolising the bus |
| rtu_rewrite_ms | T_U32 | 5000 | 1000 .. 600000 | ms | RW | 985 | how often an output block is written anyway | MATCH the slave's own failsafe timer; it is watching for this |

## Alarms

### gh_mlb41 (39)

| name | code | on | off | hold | recent | reg | description | notes |
|---|---|---|---|---|---|---|---|---|
| alarm_ac_grid | AC_GRID | 2 | 2 | 10 | 0 | 800 | grid AC out-of-spec/offline | check mains breaker / utility feed |
| alarm_dc_ps1 | DC_PS1 | 2 | 2 | 0 | 0 | 801 | DC PS1 volts too low | - |
| alarm_gen | GEN_FLT | 1 | 2 | 10 | 0 | 802 | generator failed to start | inspect generator + start battery |
| alarm_ac_local | AC_LOCAL | 2 | 2 | 0 | 0 | 803 | local AC out of spec or offline | - |
| alarm_dc_ps2 | DC_PS2 | 2 | 2 | 10 | 0 | 804 | DC PS2 volts too low | - |
| alarm_dc_ups | DC_UPS | 1 | 2 | 10 | 0 | 805 | UPS low battery: plant held | the UPS's own contact; restore mains and check the battery |
| alarm_generator_battery | GEN_BATT | 5 | 5 | 0 | 0 | 807 | gen. start battery low | - |
| alarm_sensor_fault | SNS_FLT | 2 | 2 | 0 | 0 | 812 | sensor fault | - |
| alarm_too_hot | TOO_HOT | 3 | 3 | 15 | 0 | 813 | inside temperature too high | increase ventilation / open windows |
| alarm_too_cold | TOO_COLD | 3 | 3 | 15 | 0 | 814 | inside temperature too low | - |
| alarm_too_humid | TOO_HUM | 3 | 3 | 15 | 0 | 815 | too humid: the ladder is at full | ventilate more or cut the moisture load; alarm_rot is the band below this one |
| alarm_strat | STRAT | 5 | 5 | 0 | 0 | 816 | temp. stratification too high | - |
| alarm_water_leak | WTR_LK | 1 | 2 | 0 | 0 | 817 | leak in irrigation circuit | shut irrigation valve, inspect circuit |
| alarm_water_pressure | WTR_BAR | 2 | 2 | 0 | 0 | 818 | water pressure too low | - |
| alarm_irr_drain | DRAIN_FLT | 3 | 3 | 0 | 0 | 819 | a tray would not drain in time | - |
| alarm_weather_storm | WH_STORM | 60 | 120 | 15 | 0 | 822 | current storm conditions | close roof windows, secure screens |
| alarm_sns_ceiling | SNS_CEIL | 5 | 5 | 0 | 0 | 826 | ceiling temp/RH probe quorum lost | the vote runs on the two that are left; replace before a second goes |
| alarm_sns_floor | SNS_FLR | 5 | 5 | 0 | 0 | 827 | floor temp/RH probe quorum lost | the vote runs on the two that are left; replace before a second goes |
| alarm_sns_crawlspace | SNS_CS | 5 | 5 | 0 | 0 | 828 | crawlspace temp/RH probe quorum lost | the vote runs on the two that are left; replace before a second goes |
| alarm_sns_exterior | SNS_EXT | 5 | 5 | 0 | 0 | 829 | exterior temp/RH probe quorum lost | the vote runs on the two that are left; replace before a second goes |
| alarm_rot | ROT | 5 | 5 | 0 | 0 | 830 | dew-point margin low: rot risk | what the humidity ladder is guarding against |
| alarm_wx_station | WX_FLT | 5 | 5 | 0 | 0 | 831 | weather station not answering | wind and rain unknown: the windows hold their safe state |
| alarm_irr_fill | FILL_FLT | 0 | 5 | 0 | 0 | 832 | a tray would not fill in time | water off, inlet valve stuck, or the meter has stopped counting; irr_activeTray names the tray |
| alarm_irr_fill_tray1 | FILL1 | 0 | 5 | 0 | 0 | 833 | tray 1 would not fill in time | - |
| alarm_irr_fill_tray2 | FILL2 | 0 | 5 | 0 | 0 | 834 | tray 2 would not fill in time | - |
| alarm_irr_fill_tray3 | FILL3 | 0 | 5 | 0 | 0 | 835 | tray 3 would not fill in time | - |
| alarm_irr_fill_tray4 | FILL4 | 0 | 5 | 0 | 0 | 836 | tray 4 would not fill in time | - |
| alarm_irr_fill_tray5 | FILL5 | 0 | 5 | 0 | 0 | 837 | tray 5 would not fill in time | - |
| alarm_irr_fill_tray6 | FILL6 | 0 | 5 | 0 | 0 | 838 | tray 6 would not fill in time | - |
| alarm_irr_fill_tray7 | FILL7 | 0 | 5 | 0 | 0 | 839 | tray 7 would not fill in time | - |
| alarm_irr_fill_tray8 | FILL8 | 0 | 5 | 0 | 0 | 840 | tray 8 would not fill in time | - |
| alarm_irr_drain_tray1 | DRAIN1 | 0 | 5 | 0 | 0 | 841 | tray 1 would not drain in time | - |
| alarm_irr_drain_tray2 | DRAIN2 | 0 | 5 | 0 | 0 | 842 | tray 2 would not drain in time | - |
| alarm_irr_drain_tray3 | DRAIN3 | 0 | 5 | 0 | 0 | 843 | tray 3 would not drain in time | - |
| alarm_irr_drain_tray4 | DRAIN4 | 0 | 5 | 0 | 0 | 844 | tray 4 would not drain in time | - |
| alarm_irr_drain_tray5 | DRAIN5 | 0 | 5 | 0 | 0 | 845 | tray 5 would not drain in time | - |
| alarm_irr_drain_tray6 | DRAIN6 | 0 | 5 | 0 | 0 | 846 | tray 6 would not drain in time | - |
| alarm_irr_drain_tray7 | DRAIN7 | 0 | 5 | 0 | 0 | 847 | tray 7 would not drain in time | - |
| alarm_irr_drain_tray8 | DRAIN8 | 0 | 5 | 0 | 0 | 848 | tray 8 would not drain in time | - |

### Library-level (13)

Declared by the firmware, on every site that compiles the
feature. A site may restate any of them - the restated row
then appears above, in its own list.

| name | code | on | off | hold | recent | reg | description | notes |
|---|---|---|---|---|---|---|---|---|
| alarm_net_fault | NET_FLT | 5 | 5 | 0 | 0 | 986 | the network link is down | bring-up failed or the link was lost; check the cable, the switch and net_ip |
| alarm_ntp_timeout | NTP_TIM | 5 | 5 | 0 | 0 | 987 | no NTP sync | the wall clock is free-running: anything dated, scheduled or solar is adrift |
| alarm_log_flood | LOG_FLD | 2 | 2 | 0 | 0 | 988 | log messages are being dropped | the rate limiter is shedding; something is repeating itself and the record is incomplete |
| alarm_system_reset | SYS_RST | -1 | 0 | 0 | 30*24*3600 | 990 | unit reset recently | - |
| alarm_cpu_xrun | CPU_XRUN | 0 | -1 | 300 | 24*3600 | 991 | scheduler overrun (skipped beat) | - |
| alarm_cpu_load | CPU_LOAD | 5 | 5 | 300 | 24*3600 | 992 | CPU load over threshold | - |
| alarm_stack | STACK | 5 | 5 | 300 | 30*24*3600 | 993 | stack usage over threshold | - |
| alarm_cpu_jitter | CPU_JITT | 5 | 5 | 300 | 24*3600 | 994 | CPU jitter over threshold | - |
| alarm_flash_error | FLASH_FLT | 0 | -1 | 0 | 30*24*3600 | 995 | persistent storage write failed | parameters will not survive a reboot; check the KV region |
| alarm_fieldbus | BUS_DEAD | 0 | 5 | 0 | 0 | 996 | a field device is not answering | check the RS-485 wiring, termination and the device's own power |
| alarm_scan_late | SCAN_LATE | 5 | 30 | 0 | 0 | 997 | a control program is missing its period | check the TASKS page and the per-program cost; something is overrunning or the site asks for more than the beat holds |
| alarm_fieldbus_line | BUS_LINE | 0 | 5 | 0 | 0 | 998 | the whole segment has gone quiet | the trunk, the termination, this unit's transceiver, or a second master - not twenty devices that failed at once |
| alarm_io_drop | IO_DROP | 0 | -1 | 0 | 30*24*3600 | 999 | a field row is never scanned | the planner could not place it: the device model's limits, or more rows than the plan holds. It is a signal that will never be read |

## Status values

| name | type | dgt | unit | accepts | reg | description | notes |
|---|---|---|---|---|---|---|---|
| sys_fieldbus_dead | T_U16 | 0 | - | - | 902 | field devices not answering | 0 when every planned device is talking |
| sys_fieldbus_crc | T_U16 | 0 | - | - | 903 | frames that failed their checksum | rising means the line: termination, stub length, or the wrong baud |
| sys_fieldbus_stray | T_U16 | 0 | - | - | 904 | bytes that arrived out of turn | rising means a second master, a duplicate address, or a device answering after its turn had passed |
| sys_supply_lockout | T_BOOL | 0 | - | - | 500 | supply unhealthy: actuators held off | - |
| cli_temp_in | T_F32 | 1 | degC | - | 501 | interior temperature: the hotter of the two levels | what the cooling ladder controls on; the spec keeps the worst case between levels, and the gap between them is cli_dTStrat_degC |
| cli_temp_hi | T_F32 | 1 | degC | - | 502 | ceiling temperature, voted and filtered | median of the three probes, then EWMA |
| cli_rh_hi | T_F32 | 2 | - | - | 503 | ceiling relative humidity, voted and filtered | fraction 0..1; median of three, then EWMA |
| cli_temp_lo | T_F32 | 1 | degC | - | 504 | floor temperature, voted and filtered | median of the three probes, then EWMA |
| cli_rh_lo | T_F32 | 2 | - | - | 505 | floor relative humidity, voted and filtered | fraction 0..1; median of three, then EWMA |
| cli_temp_cs | T_F32 | 1 | degC | - | 506 | crawlspace temperature, voted and filtered | median of the three probes, then EWMA |
| cli_rh_cs | T_F32 | 2 | - | - | 507 | crawlspace relative humidity, voted and filtered | fraction 0..1; median of three, then EWMA |
| cli_temp_ext | T_F32 | 1 | degC | - | 508 | exterior temperature, voted and filtered | median of the three probes, then EWMA |
| cli_rh_ext | T_F32 | 2 | - | - | 509 | exterior relative humidity, voted and filtered | fraction 0..1; median of three, then EWMA |
| cli_dTStrat_degC | T_F32 | 1 | degC | - | 510 | stratification: ceiling above floor | what the destratification ladder reads |
| cli_dewPoint_degC | T_F32 | 1 | degC | - | 511 | interior dew point (Magnus) | the worse of the two interior levels |
| cli_dpMargin_degC | T_F32 | 1 | degC | - | 512 | interior dew-point margin: how far from condensing | larger is drier; what the rot ladder is set in |
| cli_satPress_kPa | T_F32 | 3 | kPa | - | 513 | interior saturation vapour pressure (Tetens) | - |
| cli_vpd_kPa | T_F32 | 3 | kPa | - | 514 | interior vapour-pressure deficit | larger is thirstier; what irrigation is modulated on |
| cli_targetTemp_degC | T_F32 | 1 | degC | - | 515 | cooling target in force for the current phase | the bank phases offset it, then it is clamped 14..27 |
| cli_prevDayMax_degC | T_F32 | 1 | degC | - | 516 | highest interior temperature of the previous day | gates COLD_BANK: pre-cool only after a hot day |
| cli_prevNightMin_degC | T_F32 | 1 | degC | - | 517 | lowest interior temperature of the previous night | gates HEAT_BANK: bank heat only after a cold night |
| cli_phase | enum | - | - | `DayPhase` | 518 | phase of the day in force | NIGHT COLD_BANK DAWN_BREEZE DAY HEAT_BANK DUSK_BREEZE |
| cli_coolDem | T_F32 | 2 | - | - | 519 | cooling demand, -1 warming to +1 cooling | PID on the interior temperature against cli_targetTemp_degC |
| cli_stratDem | T_F32 | 2 | - | - | 520 | destratification demand, 0 to 1 | 3-band hysteresis on cli_dTStrat_degC |
| cli_fanSpeed | T_U8 | 0 | - | - | 569 | destratification fan speed in force, 0 to 3 | the band the fans are actually in; the three coils are one relay each |
| cli_windowDem | T_F32 | 2 | - | - | 529 | how far the cooling ladder wants the windows open | 0..1, before the rain, wind and supply interlocks; cli_windowN_tgt is where each one is actually going |
| cli_humidDem | T_F32 | 2 | - | - | 521 | humidity demand, -1 dehumidify to +1 humidify | 4 bands and a dead zone on cli_dpMargin_degC |
| cli_coldBankArm | T_BOOL | 0 | - | - | 522 | cold banking armed for the coming dawn | yesterday ran hotter than cli_coldBankTrg_degC |
| cli_heatBankArm | T_BOOL | 0 | - | - | 523 | heat banking armed for the coming dusk | last night ran colder than cli_heatBankTrg_degC |
| sol_fromRise_min | T_I16 | 0 | min | - | 524 | minutes since sunrise; negative before it | what the sol_ phase offsets are measured against |
| sol_fromSet_min | T_I16 | 0 | min | - | 525 | minutes since sunset; negative before it | invalid until the tracker has a location and a wall clock |
| sol_elev_deg | T_F32 | 1 | deg | - | 593 | the sun's elevation above the horizon | negative below it; what the day's shape is anticipated from |
| irr_inFlow_Lpm | T_F32 | 1 | Lpm | - | 526 | inlet flow, differentiated from the counter | one pulse is irr_input_qtyPerImp litres, so the rate is a window average |
| irr_inTotal_L | T_F32 | 1 | L | - | 527 | inlet water counter, since it was last zeroed | retained: the value IS the history, and a reboot must not zero the meter |
| irr_outTotal_L | T_F32 | 1 | L | - | 528 | outlet water counter, since it was last zeroed | integrated from irr_outFlow_Lpm; there is no outlet pulse train |
| irr_vpdAvg_kPa | T_F32 | 3 | kPa | - | 530 | dryness tracker: VPD averaged over irr_dryEwmaDays | folded once an HOUR; per scan the increment underflows, per day the result depends on the hour it was taken |
| irr_mod | T_F32 | 2 | - | - | 531 | irrigation modulation, irr_modMin (wet) to 1 (summer dry) | scales volume and period so total flow tracks it |
| irr_period_days | T_F32 | 2 | days | - | 532 | modulated interval, for the tray due next | the tray's summer period divided by mod^(1-volFreqSplit) |
| irr_volume_L | T_F32 | 1 | L | - | 533 | modulated dose, for the tray being served | the tray's summer volume times mod^volFreqSplit |
| irr_activeTray | T_U8 | 0 | - | - | 534 | tray being served, 0 when none | - |
| irr_today | T_U32 | 0 | days | - | 597 | the day the schedule is running against, 0 before the clock is set | read back, so forcing it rehearses a date - or a plant that has never had one |
| irr_servedToday | T_U8 | 0 | - | - | 535 | trays served since the day rolled over | capped by irr_traysPerDay; the surplus waits for tomorrow |
| irr_delivered_L | T_F32 | 1 | L | - | 536 | delivered into the tray being filled | counted off irr_inPulse, one pulse per irr_input_qtyPerImp |
| irr_drained_L | T_F32 | 1 | L | - | 538 | drained back out of the tray being served | integrated off irr_out_Lpm over the drain window |
| irr_outFlow_Lpm | T_F32 | 1 | Lpm | - | 539 | outlet flow, averaged over irr_out_avg_ms | what the drain verdict integrates |
| irr_state | enum | - | - | `IrrState` | 537 | what the irrigation cycle is doing | IDLE FILLING DRAINING FLUSHING |
| cli_window1_tgt | T_F32 | 2 | - | - | 540 | window 1 commanded position, 0 to 1 | what the arbitration asked for |
| cli_window1_pos | T_F32 | 2 | - | - | 541 | window 1 position, 0 to 1 | estimated from travel time; only a seating stroke proves 0 |
| cli_window2_tgt | T_F32 | 2 | - | - | 542 | window 2 commanded position, 0 to 1 | what the arbitration asked for |
| cli_window2_pos | T_F32 | 2 | - | - | 543 | window 2 position, 0 to 1 | estimated from travel time; only a seating stroke proves 0 |
| cli_window3_tgt | T_F32 | 2 | - | - | 544 | window 3 commanded position, 0 to 1 | what the arbitration asked for |
| cli_window3_pos | T_F32 | 2 | - | - | 545 | window 3 position, 0 to 1 | estimated from travel time; only a seating stroke proves 0 |
| cli_window4_tgt | T_F32 | 2 | - | - | 546 | window 4 commanded position, 0 to 1 | what the arbitration asked for |
| cli_window4_pos | T_F32 | 2 | - | - | 547 | window 4 position, 0 to 1 | estimated from travel time; only a seating stroke proves 0 |
| cli_rainLockout | T_BOOL | 0 | - | - | 548 | rain holds the windows shut | and for cli_rainLockout_sec after it stops |
| cli_windLockout | T_BOOL | 0 | - | - | 549 | wind holds the windows shut | gusting over cli_windShutEnd_mps |
| cli_windowLockout | T_BOOL | 0 | - | - | 596 | the roof is held shut, whatever the ladder wants | stage one of the arbitration: rain, wind or the supply. Force it to hold the roof shut by hand |
| cli_windowCap | T_F32 | 2 | - | - | 602 | the most the roof may open | 0 shuts it; the wind band ramps it down to 0 at cli_windShutEnd_mps |
| cli_sinceRain_min | T_U16 | 0 | min | - | 595 | minutes since the gauge last read wet | held until the first shower of this run: a reboot does not know when it last rained |
| cli_screenMoving | T_BOOL | 0 | - | - | 550 | thermal screen travelling | cuts the destratification fans while it moves |
| cli_rotLockout | T_BOOL | 0 | - | - | 551 | rot risk: nothing may humidify | cuts the skirt valve whatever the humidity demand |
| cli_rotVentDem | T_F32 | 2 | - | - | 552 | anti-rot ventilation demand, 0 to 1 | one of the three the intractors take the max of |
| cli_skirtDuty | T_F32 | 2 | - | - | 553 | skirt valve on-time, 0 to 1 | of cli_skirtPeriod_sec; under cli_skirtMinOn_sec it stays off |
| sys_grid_V | T_F32 | 1 | V | - | 554 | grid line-to-neutral volts | deadbanded mirror of the meter |
| sys_grid_A | T_F32 | 1 | A | - | 555 | grid total current | deadbanded mirror of the meter |
| sys_grid_W | T_F32 | 0 | W | - | 556 | grid active power | deadbanded mirror of the meter |
| sys_grid_Hz | T_F32 | 2 | Hz | - | 557 | grid frequency | deadbanded mirror of the meter |
| sys_grid_ok | T_BOOL | 0 | - | - | 558 | grid supply within its bands | volts AND frequency; false is what alarms |
| sys_local_V | T_F32 | 1 | V | - | 559 | local AC line-to-neutral volts | deadbanded mirror of the meter |
| sys_local_A | T_F32 | 1 | A | - | 560 | local AC total current | deadbanded mirror of the meter |
| sys_local_W | T_F32 | 0 | W | - | 561 | local AC active power | deadbanded mirror of the meter |
| sys_local_Hz | T_F32 | 2 | Hz | - | 562 | local AC frequency | deadbanded mirror of the meter |
| sys_local_ok | T_BOOL | 0 | - | - | 563 | local AC supply within its bands | volts AND frequency; false is what alarms |
| sys_dc1_V | T_F32 | 1 | V | - | 564 | DC PS1 volts | deadbanded mirror of the divider |
| sys_dc2_V | T_F32 | 1 | V | - | 565 | DC PS2 volts | deadbanded mirror of the divider |
| sys_upsBat_V | T_F32 | 1 | V | - | 566 | UPS battery volts | deadbanded mirror of the divider |
| sys_genBat_V | T_F32 | 1 | V | - | 567 | generator start battery volts | deadbanded mirror of the divider |
| sys_dc_ok | T_BOOL | 0 | - | - | 568 | every DC rail above its floor, and the UPS off its low-battery contact | - |
| prog_ac_grid_watch | enum | - | - | `Plc::health_label` | 570 | health of the ac_grid_watch program | 3 is INHIBITED: nothing has run yet |
| prog_ac_local_watch | enum | - | - | `Plc::health_label` | 571 | health of the ac_local_watch program | 3 is INHIBITED: nothing has run yet |
| prog_dc_watch | enum | - | - | `Plc::health_label` | 572 | health of the dc_watch program | 3 is INHIBITED: nothing has run yet |
| prog_temp_rh_sensors | enum | - | - | `Plc::health_label` | 603 | health of the temp_rh_sensors program | 3 is INHIBITED: nothing has run yet |
| prog_temp_rh_zone | enum | - | - | `Plc::health_label` | 573 | health of the temp_rh_zone program | 3 is INHIBITED: nothing has run yet |
| prog_strat | enum | - | - | `Plc::health_label` | 574 | health of the strat program | 3 is INHIBITED: nothing has run yet |
| prog_climate_watch | enum | - | - | `Plc::health_label` | 575 | health of the climate_watch program | 3 is INHIBITED: nothing has run yet |
| prog_sun_watch | enum | - | - | `Plc::health_label` | 594 | health of the sun watch program | 3 is INHIBITED: nothing has run yet |
| prog_day_phase | enum | - | - | `Plc::health_label` | 576 | health of the day_phase program | 3 is INHIBITED: nothing has run yet |
| prog_target_temp | enum | - | - | `Plc::health_label` | 577 | health of the target_temp program | 3 is INHIBITED: nothing has run yet |
| prog_cooling | enum | - | - | `Plc::health_label` | 578 | health of the cooling program | 3 is INHIBITED: nothing has run yet |
| prog_destrat | enum | - | - | `Plc::health_label` | 579 | health of the destrat program | 3 is INHIBITED: nothing has run yet |
| prog_humidity | enum | - | - | `Plc::health_label` | 580 | health of the humidity program | 3 is INHIBITED: nothing has run yet |
| prog_irr_modulation | enum | - | - | `Plc::health_label` | 591 | health of the irrigation modulation program | 3 is INHIBITED: nothing has run yet |
| prog_irr_mtr_input | enum | - | - | `Plc::health_label` | 592 | health of the inlet meter program | 3 is INHIBITED: nothing has run yet |
| prog_irr_valves | enum | - | - | `Plc::health_label` | 605 | health of the irrigation valves program | 3 is INHIBITED: nothing has run yet |
| prog_irr_mtr_output | enum | - | - | `Plc::health_label` | 604 | health of the outlet meter program | 3 is INHIBITED: nothing has run yet |
| prog_irrigation | enum | - | - | `Plc::health_label` | 581 | health of the irrigation program | 3 is INHIBITED: nothing has run yet |
| prog_screen | enum | - | - | `Plc::health_label` | 582 | health of the screen program | 3 is INHIBITED: nothing has run yet |
| prog_windows | enum | - | - | `Plc::health_label` | 583 | health of the windows program | 3 is INHIBITED: nothing has run yet |
| prog_weather_watch | enum | - | - | `Plc::health_label` | 584 | health of the weather_watch program | 3 is INHIBITED: nothing has run yet |
| prog_intractors | enum | - | - | `Plc::health_label` | 585 | health of the intractors program | 3 is INHIBITED: nothing has run yet |
| prog_fans | enum | - | - | `Plc::health_label` | 586 | health of the fans program | 3 is INHIBITED: nothing has run yet |
| prog_skirt | enum | - | - | `Plc::health_label` | 587 | health of the skirt program | 3 is INHIBITED: nothing has run yet |
| cli_intakeCoolOk | T_BOOL | 0 | - | - | 588 | intake air is cool enough to be worth drawing | crawlspace is cli_ventTempMgn_degC under the interior |
| cli_groundHeatOk | T_BOOL | 0 | - | - | 589 | intake air is warm enough to be worth drawing | the mirror gate: the ground is that much OVER the interior, and the ladder is asking for heat |
| cli_intakeDryOk | T_BOOL | 0 | - | - | 590 | outside air is dry enough to be worth drawing | exterior dew point is cli_ventDryMgn_degC under the interior's |
| irr_dueTray | T_U8 | 0 | - | - | 598 | tray owed water now, 0 when none is | the schedule's answer: the most overdue tray whose interval has elapsed, or a forced one |
| irr_slotOpen | T_BOOL | 0 | - | - | 599 | the climate allows a cycle to start | end of night once the plants are warm enough, or the morning window |
| prog_irr_schedule | enum | - | - | `Plc::health_label` | 600 | health of the irrigation schedule | - |
| prog_roof_lockout | enum | - | - | `Plc::health_label` | 601 | health of the roof lockout program | - |

## Direct inputs

| name | type | dev | fc | reg | n | min | max | timeout | unit | description | notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| grid_L1_V | T_F32 | 5 | 4 | 0 | 2 | 0.0 | 500.0 | 15000 | V | L1 phase voltage | SDM630 p1 30001 |
| grid_L2_V | T_F32 | 5 | 4 | 2 | 2 | 0.0 | 500.0 | 15000 | V | L2 phase voltage | SDM630 p2 30003 |
| grid_L3_V | T_F32 | 5 | 4 | 4 | 2 | 0.0 | 500.0 | 15000 | V | L3 phase voltage | SDM630 p3 30005 |
| grid_L1_A | T_F32 | 5 | 4 | 6 | 2 | 0.0 | 200.0 | 15000 | A | L1 current | SDM630 p4 30007 |
| grid_L2_A | T_F32 | 5 | 4 | 8 | 2 | 0.0 | 200.0 | 15000 | A | L2 current | SDM630 p5 30009 |
| grid_L3_A | T_F32 | 5 | 4 | 10 | 2 | 0.0 | 200.0 | 15000 | A | L3 current | SDM630 p6 30011 |
| grid_L1_VA | T_F32 | 5 | 4 | 18 | 2 | -1000000.0 | 1000000.0 | 15000 | VA | L1 apparent power | SDM630 p10 30019 |
| grid_L2_VA | T_F32 | 5 | 4 | 20 | 2 | -1000000.0 | 1000000.0 | 15000 | VA | L2 apparent power | SDM630 p11 30021 |
| grid_L3_VA | T_F32 | 5 | 4 | 22 | 2 | -1000000.0 | 1000000.0 | 15000 | VA | L3 apparent power | SDM630 p12 30023 |
| grid_L1_VAr | T_F32 | 5 | 4 | 24 | 2 | -1000000.0 | 1000000.0 | 15000 | VAr | L1 reactive power | SDM630 p13 30025 |
| grid_L2_VAr | T_F32 | 5 | 4 | 26 | 2 | -1000000.0 | 1000000.0 | 15000 | VAr | L2 reactive power | SDM630 p14 30027 |
| grid_L3_VAr | T_F32 | 5 | 4 | 28 | 2 | -1000000.0 | 1000000.0 | 15000 | VAr | L3 reactive power | SDM630 p15 30029 |
| grid_L1_PF | T_F32 | 5 | 4 | 30 | 2 | -1.0 | 1.0 | 15000 | - | L1 power factor | SDM630 p16 30031 |
| grid_L2_PF | T_F32 | 5 | 4 | 32 | 2 | -1.0 | 1.0 | 15000 | - | L2 power factor | SDM630 p17 30033 |
| grid_L3_PF | T_F32 | 5 | 4 | 34 | 2 | -1.0 | 1.0 | 15000 | - | L3 power factor | SDM630 p18 30035 |
| grid_L1_PA | T_F32 | 5 | 4 | 36 | 2 | -180.0 | 180.0 | 15000 | deg | L1 phase angle | SDM630 p19 30037 |
| grid_L2_PA | T_F32 | 5 | 4 | 38 | 2 | -180.0 | 180.0 | 15000 | deg | L2 phase angle | SDM630 p20 30039 |
| grid_L3_PA | T_F32 | 5 | 4 | 40 | 2 | -180.0 | 180.0 | 15000 | deg | L3 phase angle | SDM630 p21 30041 |
| grid_LN_V | T_F32 | 5 | 4 | 42 | 2 | 0.0 | 500.0 | 15000 | V | average line-to-neutral voltage | SDM630 p22 30043 |
| grid_LL_V | T_F32 | 5 | 4 | 206 | 2 | 0.0 | 900.0 | 15000 | V | average line-to-line voltage | SDM630 p104 30207 |
| grid_avg_A | T_F32 | 5 | 4 | 46 | 2 | 0.0 | 200.0 | 15000 | A | average current | SDM630 p24 30047 |
| grid_total_A | T_F32 | 5 | 4 | 48 | 2 | 0.0 | 600.0 | 15000 | A | total current | SDM630 p25 30049 |
| grid_total_W | T_F32 | 5 | 4 | 52 | 2 | -1000000.0 | 1000000.0 | 15000 | W | total active power | SDM630 p27 30053 |
| grid_total_VA | T_F32 | 5 | 4 | 56 | 2 | -1000000.0 | 1000000.0 | 15000 | VA | total apparent power | SDM630 p29 30057 |
| grid_total_VAr | T_F32 | 5 | 4 | 60 | 2 | -1000000.0 | 1000000.0 | 15000 | VAr | total reactive power | SDM630 p31 30061 |
| grid_total_PF | T_F32 | 5 | 4 | 62 | 2 | -1.0 | 1.0 | 15000 | - | total power factor | SDM630 p32 30063 |
| grid_total_PA | T_F32 | 5 | 4 | 66 | 2 | -180.0 | 180.0 | 15000 | deg | total phase angle | SDM630 p34 30067 |
| grid_Hz | T_F32 | 5 | 4 | 70 | 2 | 0.0 | 100.0 | 15000 | Hz | line frequency | SDM630 p36 30071 |
| grid_L12_V | T_F32 | 5 | 4 | 200 | 2 | 0.0 | 900.0 | 15000 | V | L1-L2 voltage | SDM630 p101 30201 |
| grid_L23_V | T_F32 | 5 | 4 | 202 | 2 | 0.0 | 900.0 | 15000 | V | L2-L3 voltage | SDM630 p102 30203 |
| grid_L31_V | T_F32 | 5 | 4 | 204 | 2 | 0.0 | 900.0 | 15000 | V | L3-L1 voltage | SDM630 p103 30205 |
| grid_LL_avg_THD | T_F32 | 5 | 4 | 340 | 2 | 0.0 | 100.0 | 15000 | % | line-to-line average THD | SDM630 p171 30341 |
| grid_LN_avg_THD | T_F32 | 5 | 4 | 248 | 2 | 0.0 | 100.0 | 15000 | % | line-to-neutral average THD | SDM630 p125 30249 |
| grid_N_A | T_F32 | 5 | 4 | 224 | 2 | 0.0 | 200.0 | 15000 | A | neutral current | SDM630 p113 30225 |
| grid_L1_W | T_F32 | 5 | 4 | 12 | 2 | -1000000.0 | 1000000.0 | 15000 | W | L1 active power | SDM630 p7 30013 |
| grid_L2_W | T_F32 | 5 | 4 | 14 | 2 | -1000000.0 | 1000000.0 | 15000 | W | L2 active power | SDM630 p8 30015 |
| grid_L3_W | T_F32 | 5 | 4 | 16 | 2 | -1000000.0 | 1000000.0 | 15000 | W | L3 active power | SDM630 p9 30017 |
| ats_V | T_F32 | 6 | 4 | 0 | 2 | 0.0 | 500.0 | 15000 | V | ATS voltage | MCT1-140 0x0000 |
| ats_A | T_F32 | 6 | 4 | 2 | 2 | 0.0 | 200.0 | 15000 | A | ATS current | MCT1-140 0x0002 |
| ats_W | T_F32 | 6 | 4 | 4 | 2 | -1000000.0 | 1000000.0 | 15000 | W | ATS active power | MCT1-140 0x0004 |
| ats_VAr | T_F32 | 6 | 4 | 6 | 2 | -1000000.0 | 1000000.0 | 15000 | VAr | ATS reactive power | MCT1-140 0x0006 |
| ats_VA | T_F32 | 6 | 4 | 8 | 2 | -1000000.0 | 1000000.0 | 15000 | VA | ATS apparent power | MCT1-140 0x0008 |
| ats_Hz | T_F32 | 6 | 4 | 12 | 2 | 0.0 | 100.0 | 15000 | Hz | ATS frequency | MCT1-140 0x000C |
| cli_temp_ext1 | T_F32 | 41 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | exterior temperature 1 | temp/RH sensor 0x0000 |
| cli_rh_ext1 | T_F32 | 41 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | exterior humidity 1 | temp/RH sensor 0x0002 |
| cli_temp_ext2 | T_F32 | 42 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | exterior temperature 2 | temp/RH sensor 0x0000 |
| cli_rh_ext2 | T_F32 | 42 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | exterior humidity 2 | temp/RH sensor 0x0002 |
| cli_temp_ext3 | T_F32 | 43 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | exterior temperature 3 | temp/RH sensor 0x0000 |
| cli_rh_ext3 | T_F32 | 43 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | exterior humidity 3 | temp/RH sensor 0x0002 |
| cli_temp_cs1 | T_F32 | 44 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | crawlspace temperature 1 | temp/RH sensor 0x0000 |
| cli_rh_cs1 | T_F32 | 44 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | crawlspace humidity 1 | temp/RH sensor 0x0002 |
| cli_temp_cs2 | T_F32 | 45 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | crawlspace temperature 2 | temp/RH sensor 0x0000 |
| cli_rh_cs2 | T_F32 | 45 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | crawlspace humidity 2 | temp/RH sensor 0x0002 |
| cli_temp_cs3 | T_F32 | 46 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | crawlspace temperature 3 | temp/RH sensor 0x0000 |
| cli_rh_cs3 | T_F32 | 46 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | crawlspace humidity 3 | temp/RH sensor 0x0002 |
| cli_temp_lo1 | T_F32 | 47 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | low level temperature 1 | temp/RH sensor 0x0000 |
| cli_rh_lo1 | T_F32 | 47 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | low level humidity 1 | temp/RH sensor 0x0002 |
| cli_temp_lo2 | T_F32 | 48 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | low level temperature 2 | temp/RH sensor 0x0000 |
| cli_rh_lo2 | T_F32 | 48 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | low level humidity 2 | temp/RH sensor 0x0002 |
| cli_temp_lo3 | T_F32 | 49 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | low level temperature 3 | temp/RH sensor 0x0000 |
| cli_rh_lo3 | T_F32 | 49 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | low level humidity 3 | temp/RH sensor 0x0002 |
| cli_temp_hi1 | T_F32 | 50 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | high level temperature 1 | temp/RH sensor 0x0000 |
| cli_rh_hi1 | T_F32 | 50 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | high level humidity 1 | temp/RH sensor 0x0002 |
| cli_temp_hi2 | T_F32 | 51 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | high level temperature 2 | temp/RH sensor 0x0000 |
| cli_rh_hi2 | T_F32 | 51 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | high level humidity 2 | temp/RH sensor 0x0002 |
| cli_temp_hi3 | T_F32 | 52 | 4 | 0 | 2 | -40.0 | 80.0 | 5000 | C | high level temperature 3 | temp/RH sensor 0x0000 |
| cli_rh_hi3 | T_F32 | 52 | 4 | 2 | 2 | 0.0 | 100.0 | 5000 | % | high level humidity 3 | temp/RH sensor 0x0002 |
| cli_wind_mps | T_F32 | 21 | 4 | 0 | 2 | 0.0 | 80.0 | 30000 | mps | wind speed at the station | TODO confirm against the device datasheet: address, register and word order |
| cli_windGust_mps | T_F32 | 21 | 4 | 2 | 2 | 0.0 | 80.0 | 30000 | mps | peak gust over the station's own window | TODO confirm against the device datasheet: address, register and word order |
| cli_rain_mmph | T_F32 | 21 | 4 | 4 | 2 | 0.0 | 200.0 | 30000 | mmph | rainfall rate | non-zero shuts the windows; TODO confirm against the device datasheet: address, register and word order |
| cli_irrad_Wpm2 | T_F32 | 21 | 4 | 6 | 2 | 0.0 | 1500.0 | 30000 | Wpm2 | solar irradiance | recorded for tuning only, no law reads it; TODO confirm against the device datasheet: address, register and word order |

## Analog inputs

| name | dev | fc | reg | wire | timeout | unit | scaling | description | notes |
|---|---|---|---|---|---|---|---|---|---|
| pwr_genBat_V | 11 | 4 | 0 | T_U16 | 5000 | V | pwr_genBat | generator start battery | - |
| pwr_upsBat_V | 11 | 4 | 1 | T_U16 | 5000 | V | pwr_upsBat | UPS battery voltage | - |
| pwr_dc1_V | 11 | 4 | 2 | T_U16 | 5000 | V | pwr_dc1 | DC PS1 bus voltage | - |
| pwr_dc2_V | 11 | 4 | 3 | T_U16 | 5000 | V | pwr_dc2 | DC PS2 bus voltage | - |
| irr_out_Lpm | 11 | 4 | 4 | T_U16 | 5000 | Lpm | irr_out | irrigation output flow | Hall turbine through a 4-20 mA transmitter: the loop, not the pulse train, is what is wired. 4 mA = no flow |

## Digital inputs

| name | dev | reg | isr | invert | description | notes |
|---|---|---|---|---|---|---|
| irr_inPulse | 0 | 1 | rising | false | inlet water meter pulse (interrupt-counted) | inlet volume meter, one pulse per irr_input_qtyPerImp litres. Channel 1 = terminal I2 = A1 |
| pwr_upsLowBat | 0 | 0 | - | false | UPS low battery contact | closed = the UPS is about to drop; holds every actuator. Fails ASSERTED: an unreadable terminal is not a healthy supply |

## Analog outputs

| name | dev | fc | reg | wire | safe | scaling | description | notes |
|---|---|---|---|---|---|---|---|---|
| cli_intr1 | 16 | 6 | 0 | T_U16 | 0.15f | cli_intr | intractor 1 speed | safe is 15%, not stopped: some air is always wanted |
| cli_intr2 | 16 | 6 | 1 | T_U16 | 0.15f | cli_intr | intractor 2 speed | safe is 15%, not stopped: some air is always wanted |

## Relay outputs

| name | dev | coil | safe | invert | description | notes |
|---|---|---|---|---|---|---|
| cli_window1_opn | 31 | 0 | false | false | window 1 open | - |
| cli_window1_clo | 31 | 1 | false | false | window 1 close | - |
| irr_input_ro1 | 31 | 2 | false | false | irrigation input valve 1 | - |
| irr_output_ro1 | 31 | 3 | true | true | irrigation output valve 1 | - |
| irr_tray1 | 31 | 4 | true | true | tray 1 valve | - |
| irr_tray2 | 31 | 5 | true | true | tray 2 valve | - |
| cli_window2_opn | 32 | 0 | false | false | window 2 open | - |
| cli_window2_clo | 32 | 1 | false | false | window 2 close | - |
| irr_input_ro2 | 32 | 2 | false | false | irrigation input valve 2 | - |
| irr_output_ro2 | 32 | 3 | true | true | irrigation output valve 2 | - |
| irr_tray3 | 32 | 4 | true | true | tray 3 valve | - |
| irr_tray4 | 32 | 5 | true | true | tray 4 valve | - |
| cli_window3_opn | 33 | 0 | false | false | window 3 open | - |
| cli_window3_clo | 33 | 1 | false | false | window 3 close | - |
| cli_destrat1_speed1 | 33 | 2 | true | false | destratification fan 1 speed 1 | SAFE IS RUNNING, not off: still air is itself a rot risk, and low speed costs little. The only actuator here whose safe state is energised |
| cli_destrat1_speed2 | 33 | 3 | false | false | destratification fan 1 speed 2 | off at the safe state; speed 1 carries it |
| cli_destrat1_speed3 | 33 | 4 | false | false | destratification fan 1 speed 3 | off at the safe state; speed 1 carries it |
| cli_screen | 33 | 5 | true | true | thermal screen | deploys on a de-energised coil (safe+invert) so a dead PLC leaves it out |
| irr_tray5 | 33 | 6 | true | true | tray 5 valve | - |
| irr_tray6 | 33 | 7 | true | true | tray 6 valve | - |
| cli_window4_opn | 34 | 0 | false | false | window 4 open | - |
| cli_window4_clo | 34 | 1 | false | false | window 4 close | - |
| cli_destrat2_speed1 | 34 | 2 | true | false | destratification fan 2 speed 1 | SAFE IS RUNNING, not off: still air is itself a rot risk, and low speed costs little. The only actuator here whose safe state is energised |
| cli_destrat2_speed2 | 34 | 3 | false | false | destratification fan 2 speed 2 | off at the safe state; speed 1 carries it |
| cli_destrat2_speed3 | 34 | 4 | false | false | destratification fan 2 speed 3 | off at the safe state; speed 1 carries it |
| cli_skirtValve | 34 | 5 | false | false | wet skirt valve | pulsed: humidify effort, or cooling |
| irr_tray7 | 34 | 6 | true | true | tray 7 valve | - |
| irr_tray8 | 34 | 7 | true | true | tray 8 valve | - |

## Analog scaling

6 calibrations over the site's analog rows. Each maps a
raw count onto a reading and says what range of that reading is
believable - a dead 4-20 mA loop reads full scale, and no timeout
catches that.

**`scale` and `offset` are never stored.** They are derived from the
four corners every time they are used, so a recalibration lands on
the next reading rather than at the next boot.

A calibration is SHARED by being named, not by being pointed at:
two rows naming the same six parameters are scaled by the same
numbers, and there is no third object to keep in step with them.

### pwr_genBat

Scales `pwr_genBat_V`.

| role | parameter | default | unit | description |
|---|---|---|---|---|
| raw min | pwr_genBat_rawMin_mV | 5000 | mV | gen. battery raw value at the low end |
| raw max | pwr_genBat_rawMax_mV | 7500 | mV | gen. battery raw value at the high end |
| engineering min | pwr_genBat_engMin_V | 20.0 | V | gen. battery volts at the low end |
| engineering max | pwr_genBat_engMax_V | 30.0 | V | gen. battery volts at the high end |
| plausible min | pwr_genBat_min | 20.0 | V | lowest believable gen. battery volts |
| plausible max | pwr_genBat_max | 30.0 | V | highest believable gen. battery volts |

### pwr_upsBat

Scales `pwr_upsBat_V`.

| role | parameter | default | unit | description |
|---|---|---|---|---|
| raw min | pwr_upsBat_rawMin_mV | 4000 | mV | UPS battery raw value at the low end |
| raw max | pwr_upsBat_rawMax_mV | 6000 | mV | UPS battery raw value at the high end |
| engineering min | pwr_upsBat_engMin_V | 20.0 | V | UPS battery volts at the low end |
| engineering max | pwr_upsBat_engMax_V | 30.0 | V | UPS battery volts at the high end |
| plausible min | pwr_upsBat_min | 20.0 | V | lowest believable ups battery volts |
| plausible max | pwr_upsBat_max | 30.0 | V | highest believable ups battery volts |

### pwr_dc1

Scales `pwr_dc1_V`.

| role | parameter | default | unit | description |
|---|---|---|---|---|
| raw min | pwr_dc1_rawMin_mV | 4000 | mV | DC PS1 raw value at the low end |
| raw max | pwr_dc1_rawMax_mV | 6000 | mV | DC PS1 raw value at the high end |
| engineering min | pwr_dc1_engMin_V | 20.0 | V | DC PS1 volts at the low end |
| engineering max | pwr_dc1_engMax_V | 30.0 | V | DC PS1 volts at the high end |
| plausible min | pwr_dc1_min | 20.0 | V | lowest believable dc ps1 volts |
| plausible max | pwr_dc1_max | 30.0 | V | highest believable dc ps1 volts |

### pwr_dc2

Scales `pwr_dc2_V`.

| role | parameter | default | unit | description |
|---|---|---|---|---|
| raw min | pwr_dc2_rawMin_mV | 4000 | mV | DC PS2 raw value at the low end |
| raw max | pwr_dc2_rawMax_mV | 6000 | mV | DC PS2 raw value at the high end |
| engineering min | pwr_dc2_engMin_V | 20.0 | V | DC PS2 volts at the low end |
| engineering max | pwr_dc2_engMax_V | 30.0 | V | DC PS2 volts at the high end |
| plausible min | pwr_dc2_min | 20.0 | V | lowest believable dc ps2 volts |
| plausible max | pwr_dc2_max | 30.0 | V | highest believable dc ps2 volts |

### irr_out

Scales `irr_out_Lpm`.

| role | parameter | default | unit | description |
|---|---|---|---|---|
| raw min | irr_out_rawMin_uA | 4000.0 | uA | output meter uA at zero flow |
| raw max | irr_out_rawMax_uA | 20000.0 | uA | output meter uA at full scale |
| engineering min | irr_out_engMin_Lpm | 0.0 | Lpm | output litres/min at zero signal |
| engineering max | irr_out_engMax_Lpm | 50.0 | Lpm | output litres/min at full scale |
| plausible min | irr_out_min | 0.0 | Lpm | lowest believable output flow |
| plausible max | irr_out_max | 50.0 | Lpm | highest believable output flow |

### cli_intr

Scales `cli_intr1`, `cli_intr2`.

| role | parameter | default | unit | description |
|---|---|---|---|---|
| raw min | cli_intr_rawMin_mV | 0.0 | mV | intractor raw value at zero speed |
| raw max | cli_intr_rawMax_mV | 10000.0 | mV | intractor raw value at full speed |
| engineering min | cli_intr_engMin | 0.0 | - | intractor speed at the low end |
| engineering max | cli_intr_engMax | 1.0 | - | intractor speed at the high end |
| plausible min | cli_intr_min | 0.0 | - | lowest believable intractor speed |
| plausible max | cli_intr_max | 1.0 | - | highest believable intractor speed |

## Programs

26 programs, 428 connections and 41 function blocks. Each is declared in its own
`programs/<stage>/<class>/<class>.toml`, which is the IEC 61131-3
CONFIGURATION: it joins the POU's interface to the global table.
The order they run in is `programs/order.toml`.

`in` may not be written; `out` has exactly one producer, checked
before the firmware is built; `inout` is read-modify-write, shared
by declaration.

| # | stage | program | class | task | what it does |
|---|---|---|---|---|---|
| 1 | sense | ac_grid_watch | `AcGridWatch` | SLOW | The utility feed, watched. |
| 2 | sense | ac_local_watch | `AcLocalWatch` | SLOW | The generator-backed side of the transfer switch, watched. |
| 3 | sense | dc_watch | `DcWatch` | SLOW | The DC rails and the two batteries, watched - and the plant-wide lockout they raise. |
| 4 | sense | sun_watch | `SunWatch` | SLOW | Where the sun is, published as rows. |
| 5 | sense | temp_rh_sensors | `TempRHSensors` | FAST | The four measurement locations, each voted down to one reading and filtered: what the greenhouse IS, before any law is set in it. |
| 6 | sense | temp_rh_zone | `TempRHZone` | FAST | The psychrometrics every other law is set in, off the two interior levels: one temperature for the house, and the worst case per quantity. |
| 7 | sense | strat | `StratSensor` | FAST | The stratification gap: how much warmer the ceiling is than the floor, unsigned. |
| 8 | sense | climate_watch | `ClimateWatch` | SLOW | The interior conditions, watched: the alarms that are a READING crossing a line rather than a law failing to hold one. |
| 9 | climate | day_phase | `DayPhaseController` | SLOW | The day's phase, and the temperature target it puts in force. |
| 10 | climate | target_temp | `TargetTempController` | SLOW | The temperature the greenhouse is being held to: the base setting, shifted by whichever bank phase is both IN FORCE and ARMED. |
| 11 | climate | cooling | `CoolingController` | FAST | The cooling ladder: a PID on the interior temperature against the phase's target, published as one signed demand. |
| 12 | climate | destrat | `DestratController` | SLOW | The destratification ladder: three bands with hysteresis on the ceiling-to- floor gap, published as a demand. |
| 13 | climate | humidity | `HumidityController` | SLOW | The humidity ladder: four bands and a dead zone on the dew-point margin, published as one signed demand - and the rot guard under it. |
| 14 | irrigation | irr_modulation | `IrrigationModulation` | SLOW | The irrigation ladder: how thirsty the air has been, as one factor. |
| 15 | irrigation | irr_mtr_input | `IrrMtrInput` | SLOW | The inlet meter, and the two supply faults it alone can see. |
| 16 | irrigation | irr_mtr_output | `IrrMtrOutput` | SLOW | The outlet meter: what came back off the trays. |
| 17 | irrigation | irr_schedule | `IrrigationSchedule` | SLOW | Which tray is owed water, and whether the climate will allow a cycle to start. |
| 18 | irrigation | irrigation | `IrrigationCycle` | SLOW | Irrigation: one tray at a time, filled to a dose, steeped, and drained. |
| 19 | irrigation | irr_valves | `IrrValves` | SLOW | The irrigation manifold: which valves each state of the cycle opens. |
| 20 | actuators | screen | `ScreenController` | SLOW | The thermal screen: one coil, deployed when the greenhouse is losing heat rather than gaining it. |
| 21 | actuators | roof_lockout | `RoofLockout` | SLOW | What forbids the roof being open at all, whatever the ladder asks. |
| 22 | actuators | windows | `WindowsController` | SLOW | The four roof windows: one target from the cooling ladder, and ONE MOVING AT A TIME. |
| 23 | sense | weather_watch | `WeatherWatch` | SLOW | The weather station, watched: whether it is answering, and whether what it says is a storm. |
| 24 | actuators | intractors | `IntractorController` | SLOW | The two intractors: variable-speed fans drawing air from under the floor, driven by whichever of three demands wants the most. |
| 25 | actuators | fans | `FanController` | SLOW | The two destratification fans: three relayed speeds, driven by whichever of three demands wants the most. |
| 26 | actuators | skirt | `SkirtController` | SLOW | The wet skirt: one valve pulsed against two demands. |


The function blocks this site is built from:

| block | instances | held by |
|---|---|---|
| `ValveController` | 13 | irr_valves, skirt |
| `Plc::Ton` | 6 | ac_local_watch, dc_watch, irr_modulation, irr_mtr_input, irrigation |
| `Plc::Ewma` | 6 | dc_watch, target_temp, irr_mtr_output |
| `Plc::Deadband` | 5 | dc_watch, irr_mtr_output |
| `Plc::Actuator` | 5 | screen, windows |
| `ZoneProbes` | 4 | temp_rh_sensors |
| `Plc::Hysteresis` | 4 | cooling, humidity |
| `AcSupply` | 2 | ac_grid_watch, ac_local_watch |
| `Plc::Staircase3` | 2 | destrat, fans |
| `Plc::Tof` | 2 | humidity, roof_lockout |
| `Plc::Pid` | 1 | cooling |
| `Utils::ExpMovingAverage` | 1 | irr_modulation |
| `Plc::Derivative` | 1 | irr_mtr_input |
| `Plc::Integral` | 1 | irr_mtr_output |

<!-- page -->

### ac_grid_watch

`AcGridWatch`, dispatched at SLOW, in the sense stage. It publishes 5 readings and raises 1 alarm. It defines `observe()`, so it keeps reporting while the plant is parked.

The utility feed, watched.

Empty run(), everything in observe(): an observation is exactly what has to survive the plant being parked.

| member | section | row | kind | description |
|---|---|---|---|---|
| volts | VAR_INPUT | grid_LN_V | MI | average line-to-neutral voltage |
| amps | VAR_INPUT | grid_total_A | MI | total current |
| watts | VAR_INPUT | grid_total_W | MI | total active power |
| hz | VAR_INPUT | grid_Hz | MI | line frequency |
| lo_V | VAR_INPUT | pwr_gridLo_V | P | grid volts: low alarm |
| hi_V | VAR_INPUT | pwr_gridHi_V | P | grid volts: high alarm |
| lo_Hz | VAR_INPUT | pwr_gridLo_Hz | P | grid frequency: low alarm |
| hi_Hz | VAR_INPUT | pwr_gridHi_Hz | P | grid frequency: high alarm |
| out_V | VAR_OUTPUT | sys_grid_V | SV | grid line-to-neutral volts |
| out_A | VAR_OUTPUT | sys_grid_A | SV | grid total current |
| out_W | VAR_OUTPUT | sys_grid_W | SV | grid active power |
| out_Hz | VAR_OUTPUT | sys_grid_Hz | SV | grid frequency |
| ok | VAR_OUTPUT | sys_grid_ok | SV | grid supply within its bands |
| fault | VAR_OUTPUT | alarm_ac_grid | A | grid AC out-of-spec/offline |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| supply | `AcSupply` | - |

<!-- page -->

### ac_local_watch

`AcLocalWatch`, dispatched at SLOW, in the sense stage. It publishes 5 readings and raises 2 alarms. It defines `observe()`, so it keeps reporting while the plant is parked.

The generator-backed side of the transfer switch, watched.

The same law as the grid over different rows and wider frequency bands: a generator wanders where a utility does not.

| member | section | row | kind | description |
|---|---|---|---|---|
| volts | VAR_INPUT | ats_V | MI | ATS voltage |
| amps | VAR_INPUT | ats_A | MI | ATS current |
| watts | VAR_INPUT | ats_W | MI | ATS active power |
| hz | VAR_INPUT | ats_Hz | MI | ATS frequency |
| lo_V | VAR_INPUT | pwr_localLo_V | P | local AC volts: low alarm |
| hi_V | VAR_INPUT | pwr_localHi_V | P | local AC volts: high alarm |
| lo_Hz | VAR_INPUT | pwr_localLo_Hz | P | local AC frequency: low alarm |
| hi_Hz | VAR_INPUT | pwr_localHi_Hz | P | local AC frequency: high alarm |
| grid_ok | VAR_INPUT | sys_grid_ok | SV | grid supply within its bands |
| start_s | VAR_INPUT | pwr_genStart_sec | P | the generator has this long to take over |
| out_V | VAR_OUTPUT | sys_local_V | SV | local AC line-to-neutral volts |
| out_A | VAR_OUTPUT | sys_local_A | SV | local AC total current |
| out_W | VAR_OUTPUT | sys_local_W | SV | local AC active power |
| out_Hz | VAR_OUTPUT | sys_local_Hz | SV | local AC frequency |
| ok | VAR_OUTPUT | sys_local_ok | SV | local AC supply within its bands |
| fault | VAR_OUTPUT | alarm_ac_local | A | local AC out of spec or offline |
| gen_fault | VAR_OUTPUT | alarm_gen | A | generator failed to start |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| supply | `AcSupply` | - |
| cranking | `Plc::Ton` | the grid gone with nothing taking over |

<!-- page -->

### dc_watch

`DcWatch`, dispatched at SLOW, in the sense stage. It publishes 6 readings and raises 4 alarms. It defines `observe()`, so it keeps reporting while the plant is parked.

The DC rails and the two batteries, watched - and the plant-wide lockout they raise.

`sys_supply_lockout` follows the UPS LOW-BATTERY CONTACT, not a voltage: the UPS knows what its own battery has left and a divider on the rail does not. The voltage floors are warning, not interlock.

| member | section | row | kind | description |
|---|---|---|---|---|
| dc1 | VAR_INPUT | pwr_dc1_V | AI | DC PSn bus voltage |
| dc2 | VAR_INPUT | pwr_dc2_V | AI | DC PSn bus voltage |
| ups_bat | VAR_INPUT | pwr_upsBat_V | AI | UPS battery voltage |
| gen_bat | VAR_INPUT | pwr_genBat_V | AI | generator start battery |
| dc1_avg | VAR_INPUT | pwr_dc1_avg_ms | P | rolling average for DC PSn |
| dc2_avg | VAR_INPUT | pwr_dc2_avg_ms | P | rolling average for DC PSn |
| ups_avg | VAR_INPUT | pwr_upsBat_avg_ms | P | rolling average for the UPS battery |
| gen_avg | VAR_INPUT | pwr_genBat_avg_ms | P | rolling average for the Gen. battery |
| dc1_lo | VAR_INPUT | pwr_dc1Lo_V | P | DC PSn volts: low alarm |
| dc2_lo | VAR_INPUT | pwr_dc2Lo_V | P | DC PSn volts: low alarm |
| ups_lo | VAR_INPUT | pwr_upsBatLo_V | P | UPS battery volts: low alarm |
| gen_lo | VAR_INPUT | pwr_genBatLo_V | P | gen. battery volts: low alarm |
| low_bat | VAR_INPUT | pwr_upsLowBat | DI | UPS low battery contact |
| out_dc1 | VAR_OUTPUT | sys_dc1_V | SV | DC PSn volts |
| out_dc2 | VAR_OUTPUT | sys_dc2_V | SV | DC PSn volts |
| out_ups | VAR_OUTPUT | sys_upsBat_V | SV | UPS battery volts |
| out_gen | VAR_OUTPUT | sys_genBat_V | SV | generator start battery volts |
| ok | VAR_OUTPUT | sys_dc_ok | SV | every DC rail above its floor, and the UPS off its low-battery contact |
| lockout | VAR_OUTPUT | sys_supply_lockout | SV | supply unhealthy: actuators held off |
| ps1_flt | VAR_OUTPUT | alarm_dc_ps1 | A | DC PSn volts too low |
| ps2_flt | VAR_OUTPUT | alarm_dc_ps2 | A | DC PSn volts too low |
| ups_flt | VAR_OUTPUT | alarm_dc_ups | A | UPS low battery: plant held |
| gen_flt | VAR_OUTPUT | alarm_generator_battery | A | gen. start battery low |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| ps1_mean | `Plc::Ewma` | The rail is judged on the AVERAGE, so one noisy sample cannot raise a supply
fault. One per divider, each with its own time constant.
 |
| ps1_band | `Plc::Deadband` | ...and how far the average must move to be worth a status write |
| ps2_mean | `Plc::Ewma` | - |
| ps2_band | `Plc::Deadband` | - |
| ups_mean | `Plc::Ewma` | - |
| ups_band | `Plc::Deadband` | - |
| gen_mean | `Plc::Ewma` | - |
| gen_band | `Plc::Deadband` | - |
| low_for | `Plc::Ton` | debounce: a contact bounces, a UPS does not |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _seen[4] | `bool` | `{ false, false, false, false }` | Per rail, whether its divider has EVER answered. @see AcSupply |
| _safe_held | `bool` | `false` | What this law last told the registry, so the plant-wide hold is pushed on the EDGE and not walked over every task every beat. |

<!-- page -->

### sun_watch

`SunWatch`, dispatched at SLOW, in the sense stage. It publishes 3 readings. It defines `observe()`, so it keeps reporting while the plant is parked.

Where the sun is, published as rows.

The tracker is the firmware's; this turns it into rows, as the field-bus client does for the probes. Everything downstream reads the rows, never the instrument.

| member | section | row | kind | description |
|---|---|---|---|---|
| from_rise | VAR_OUTPUT | sol_fromRise_min | SV | minutes since sunrise; negative before it |
| from_set | VAR_OUTPUT | sol_fromSet_min | SV | minutes since sunset; negative before it |
| elevation | VAR_OUTPUT | sol_elev_deg | SV | the sun's elevation above the horizon |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _sun | `Sun::SunPositionTracker* const` | `ctx.sun` | The instrument, borrowed: the firmware owns it, registers it and binds its geo parameters. Null in a build without SUN_TRACKER, which is a build this program is not in either. |

<!-- page -->

### temp_rh_sensors

`TempRHSensors`, dispatched at FAST, in the sense stage. It publishes 8 readings and raises 4 alarms.

The four measurement locations, each voted down to one reading and filtered: what the greenhouse IS, before any law is set in it.

Runs FIRST, and nothing downstream reads a probe - they read the four conditioned rows this publishes. Split from temp_rh_zone, which turns them into the psychrometrics: this is the instrument, that is the derivation.

| member | section | row | kind | description |
|---|---|---|---|---|
| tau_ms | VAR_INPUT | cli_sensorTau_ms | P | probe filter time constant |
| t_hi[3] | VAR_INPUT | cli_temp_hi1..3 | MI | high level temperature n |
| h_hi[3] | VAR_INPUT | cli_rh_hi1..3 | MI | high level humidity n |
| t_lo[3] | VAR_INPUT | cli_temp_lo1..3 | MI | low level temperature n |
| h_lo[3] | VAR_INPUT | cli_rh_lo1..3 | MI | low level humidity n |
| t_cs[3] | VAR_INPUT | cli_temp_cs1..3 | MI | crawlspace temperature n |
| h_cs[3] | VAR_INPUT | cli_rh_cs1..3 | MI | crawlspace humidity n |
| t_ext[3] | VAR_INPUT | cli_temp_ext1..3 | MI | exterior temperature n |
| h_ext[3] | VAR_INPUT | cli_rh_ext1..3 | MI | exterior humidity n |
| temp_hi | VAR_OUTPUT | cli_temp_hi | SV | ceiling temperature, voted and filtered |
| rh_hi | VAR_OUTPUT | cli_rh_hi | SV | ceiling relative humidity, voted and filtered |
| temp_lo | VAR_OUTPUT | cli_temp_lo | SV | floor temperature, voted and filtered |
| rh_lo | VAR_OUTPUT | cli_rh_lo | SV | floor relative humidity, voted and filtered |
| temp_cs | VAR_OUTPUT | cli_temp_cs | SV | crawlspace temperature, voted and filtered |
| rh_cs | VAR_OUTPUT | cli_rh_cs | SV | crawlspace relative humidity, voted and filtered |
| temp_ext | VAR_OUTPUT | cli_temp_ext | SV | exterior temperature, voted and filtered |
| rh_ext | VAR_OUTPUT | cli_rh_ext | SV | exterior relative humidity, voted and filtered |
| fault_hi | VAR_OUTPUT | alarm_sns_ceiling | A | ceiling temp/RH probe quorum lost |
| fault_lo | VAR_OUTPUT | alarm_sns_floor | A | floor temp/RH probe quorum lost |
| fault_cs | VAR_OUTPUT | alarm_sns_crawlspace | A | crawlspace temp/RH probe quorum lost |
| fault_ext | VAR_OUTPUT | alarm_sns_exterior | A | exterior temp/RH probe quorum lost |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| ceiling | `ZoneProbes` | - |
| floor | `ZoneProbes` | - |
| crawlspace | `ZoneProbes` | - |
| exterior | `ZoneProbes` | - |

<!-- page -->

### temp_rh_zone

`TempRHZone`, dispatched at FAST, in the sense stage. It publishes 5 readings and raises 1 alarm.

The psychrometrics every other law is set in, off the two interior levels: one temperature for the house, and the worst case per quantity.

Reads the conditioned rows temp_rh_sensors publishes, never a probe. The worst case is taken among the levels that are MEASURED, so one holding a reading from before its probes died does not speak for the house.

| member | section | row | kind | description |
|---|---|---|---|---|
| temp_hi | VAR_INPUT | cli_temp_hi | SV | ceiling temperature, voted and filtered |
| rh_hi | VAR_INPUT | cli_rh_hi | SV | ceiling relative humidity, voted and filtered |
| temp_lo | VAR_INPUT | cli_temp_lo | SV | floor temperature, voted and filtered |
| rh_lo | VAR_INPUT | cli_rh_lo | SV | floor relative humidity, voted and filtered |
| rot_limit | VAR_INPUT | cli_rotMargin_degC | P | dryness margin: rot risk |
| temp_in | VAR_OUTPUT | cli_temp_in | SV | interior temperature: the hotter of the two levels |
| dew_point | VAR_OUTPUT | cli_dewPoint_degC | SV | interior dew point (Magnus) |
| dp_margin | VAR_OUTPUT | cli_dpMargin_degC | SV | interior dew-point margin: how far from condensing |
| sat_press | VAR_OUTPUT | cli_satPress_kPa | SV | interior saturation vapour pressure (Tetens) |
| vpd | VAR_OUTPUT | cli_vpd_kPa | SV | interior vapour-pressure deficit |
| rot | VAR_OUTPUT | alarm_rot | A | dew-point margin low: rot risk |

<!-- page -->

### strat

`StratSensor`, dispatched at FAST, in the sense stage. It publishes 1 reading.

The stratification gap: how much warmer the ceiling is than the floor, unsigned.

Its own program because it is a reading ABOUT two zones rather than part of either, and the destratification ladder wants exactly one number from it.

| member | section | row | kind | description |
|---|---|---|---|---|
| ceiling | VAR_INPUT | cli_temp_hi | SV | ceiling temperature, voted and filtered |
| floor | VAR_INPUT | cli_temp_lo | SV | floor temperature, voted and filtered |
| strat | VAR_OUTPUT | cli_dTStrat_degC | SV | stratification: ceiling above floor |

<!-- page -->

### climate_watch

`ClimateWatch`, dispatched at SLOW, in the sense stage. It raises 5 alarms. It defines `observe()`, so it keeps reporting while the plant is parked.

The interior conditions, watched: the alarms that are a READING crossing a line rather than a law failing to hold one.

Empty run(), everything in observe(): the ALARMS page is read exactly when the plant is parked. An unmeasured reading raises nothing here - the zone's own quorum alarm covers it, and alarming twice for one dead probe would blunt both.

| member | section | row | kind | description |
|---|---|---|---|---|
| temp | VAR_INPUT | cli_temp_hi | SV | ceiling temperature, voted and filtered |
| margin | VAR_INPUT | cli_dpMargin_degC | SV | interior dew-point margin: how far from condensing |
| strat | VAR_INPUT | cli_dTStrat_degC | SV | stratification: ceiling above floor |
| too_hot | VAR_INPUT | cli_tooHot_degC | P | alarm: too HOT |
| too_cold | VAR_INPUT | cli_tooCold_degC | P | alarm: too COLD |
| humid_at | VAR_INPUT | cli_dehumFull_degC | P | dryness margin: dehumidify fully |
| strat_hi | VAR_INPUT | cli_dTStratHigh_degC | P | stratification gap for fan speed n |
| sns[4] | VAR_INPUT | alarm_sns_ceiling, alarm_sns_floor, alarm_sns_crawlspace, alarm_sns_exterior | A | ceiling temp/RH probe quorum lost |
| hot | VAR_OUTPUT | alarm_too_hot | A | inside temperature too high |
| cold | VAR_OUTPUT | alarm_too_cold | A | inside temperature too low |
| humid | VAR_OUTPUT | alarm_too_humid | A | too humid: the ladder is at full |
| stratified | VAR_OUTPUT | alarm_strat | A | temp. stratification too high |
| sensor | VAR_OUTPUT | alarm_sensor_fault | A | sensor fault |

<!-- page -->

### day_phase

`DayPhaseController`, dispatched at SLOW, in the climate stage. It publishes 5 readings.

The day's phase, and the temperature target it puts in force.

It commands nothing: it sets the target and the permissions, and the ladders below decide what moves. The six phases are five boundaries, each an offset in minutes from sunrise or sunset, so the table slides with the season.

| member | section | row | kind | description |
|---|---|---|---|---|
| inside | VAR_INPUT | cli_temp_in | SV | interior temperature: the hotter of the two levels |
| from_rise | VAR_INPUT | sol_fromRise_min | SV | minutes since sunrise; negative before it |
| from_set | VAR_INPUT | sol_fromSet_min | SV | minutes since sunset; negative before it |
| cold_bank_trg | VAR_INPUT | cli_coldBankTrg_degC | P | yesterday's high above this: bank cold |
| heat_bank_trg | VAR_INPUT | cli_heatBankTrg_degC | P | yesterday's low below this: bank heat |
| cold_bank_rise | VAR_INPUT | sol_coldBankRise_min | P | cold banking starts, from sunrise |
| breeze_rise | VAR_INPUT | sol_breezeRise_min | P | dawn breeze starts, from sunrise |
| day_rise | VAR_INPUT | sol_dayRise_min | P | day operation starts, from sunrise |
| heat_bank_set | VAR_INPUT | sol_heatBankSet_min | P | heat banking starts, from sunset |
| dusk_set | VAR_INPUT | sol_duskSet_min | P | dusk breeze starts, from sunset |
| night_set | VAR_INPUT | sol_nightSet_min | P | night operation starts, from sunset |
| phase | VAR_OUTPUT | cli_phase | SVE | phase of the day in force |
| cold_arm | VAR_OUTPUT | cli_coldBankArm | SV | cold banking armed for the coming dawn |
| heat_arm | VAR_OUTPUT | cli_heatBankArm | SV | heat banking armed for the coming dusk |
| prev_day_max | VAR_IN_OUT | cli_prevDayMax_degC | SV | highest interior temperature of the previous day |
| prev_night_min | VAR_IN_OUT | cli_prevNightMin_degC | SV | lowest interior temperature of the previous night |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _phase | `DayPhase` | `PHASE_NIGHT` | - |
| _day_max | `float` | `0.0f` | The interior extremes since the current day and night began. What COLD_BANK and HEAT_BANK are gated on: pre-cool only after a hot day, bank heat only after a cold night. |
| _night_min | `float` | `0.0f` | - |
| _seeded | `bool` | `false` | - |

<!-- page -->

### target_temp

`TargetTempController`, dispatched at SLOW, in the climate stage. It publishes 1 reading.

The temperature the greenhouse is being held to: the base setting, shifted by whichever bank phase is both IN FORCE and ARMED.

Its own program because the target is a DECISION, not a property of the phase: a bank moves it only when yesterday justified it, and a reader wants the number rather than the rule.

The published number is FILTERED, so a phase boundary eases the target over minutes instead of stepping it: everything downstream is a proportional or staged response to the gap, and a step in the target is a step in all of them at once. The clamp stays last, so the guard rails still bite immediately.

| member | section | row | kind | description |
|---|---|---|---|---|
| t_min | VAR_INPUT | cli_targetMin_degC | P | coldest the target may be clamped to |
| t_max | VAR_INPUT | cli_targetMax_degC | P | warmest the target may be clamped to |
| phase | VAR_INPUT | cli_phase | SVE | phase of the day in force |
| cold_arm | VAR_INPUT | cli_coldBankArm | SV | cold banking armed for the coming dawn |
| heat_arm | VAR_INPUT | cli_heatBankArm | SV | heat banking armed for the coming dusk |
| cool_set | VAR_INPUT | cli_coolSet_degC | P | base cooling target temperature |
| cold_bank_ofs | VAR_INPUT | cli_coldBankOfs_degC | P | how much cooler before a hot day |
| heat_bank_ofs | VAR_INPUT | cli_heatBankOfs_degC | P | how much warmer before a cold night |
| tau | VAR_INPUT | cli_targetTau_s | P | target temperature filter time constant |
| target | VAR_OUTPUT | cli_targetTemp_degC | SV | cooling target in force for the current phase |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| smooth | `Plc::Ewma` | eases a phase boundary over cli_targetTau_s |

<!-- page -->

### cooling

`CoolingController`, dispatched at FAST, in the climate stage. It publishes 4 readings.

The cooling ladder: a PID on the interior temperature against the phase's target, published as one signed demand.

Negative is heating: the intractors drawing warm air from under the floor, and below that the thermal screen. The actuators spread themselves along the demand; this program does not know which they are.

| member | section | row | kind | description |
|---|---|---|---|---|
| dem_hyst | VAR_INPUT | cli_groundDemHyst | P | demand given back before the rung lets go |
| inside | VAR_INPUT | cli_temp_in | SV | interior temperature: the hotter of the two levels |
| ceiling | VAR_INPUT | cli_temp_hi | SV | ceiling temperature, voted and filtered |
| floor | VAR_INPUT | cli_temp_lo | SV | floor temperature, voted and filtered |
| target | VAR_INPUT | cli_targetTemp_degC | SV | cooling target in force for the current phase |
| intake | VAR_INPUT | cli_temp_cs | SV | crawlspace temperature, voted and filtered |
| temp_margin | VAR_INPUT | cli_ventTempMgn_degC | P | intake air must be this much cooler |
| vent_hyst | VAR_INPUT | cli_ventHyst_degC | P | how far a gate has to close before it opens |
| ground_dem | VAR_INPUT | cli_groundHeatDem | P | cooling effort that draws ground air |
| win_start | VAR_INPUT | cli_windowStartDem | P | cooling effort that opens windows |
| win_full | VAR_INPUT | cli_windowFullDem | P | cooling effort at fully open windows |
| kp | VAR_INPUT | cli_coolKp | P | cooling PID proportional gain |
| ti | VAR_INPUT | cli_coolTi_sec | P | cooling PID integral time |
| td | VAR_INPUT | cli_coolTd_sec | P | cooling PID derivative time |
| demand | VAR_OUTPUT | cli_coolDem | SV | cooling demand, -n warming to +n cooling |
| intake_ok | VAR_OUTPUT | cli_intakeCoolOk | SV | intake air is cool enough to be worth drawing |
| ground_ok | VAR_OUTPUT | cli_groundHeatOk | SV | intake air is warm enough to be worth drawing |
| window | VAR_OUTPUT | cli_windowDem | SV | how far the cooling ladder wants the windows open |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| pid | `Plc::Pid` | - |
| cool_gate | `Plc::Hysteresis` | the intake is cooler by the margin |
| ground_gate | `Plc::Hysteresis` | ...or warmer by it |
| ground_band | `Plc::Hysteresis` | and the ladder is asking for heat |

<!-- page -->

### destrat

`DestratController`, dispatched at SLOW, in the climate stage. It publishes 1 reading.

The destratification ladder: three bands with hysteresis on the ceiling-to- floor gap, published as a demand.

A band change needs the gap to move by the declared hysteresis, so the fans do not step between speeds on noise.

| member | section | row | kind | description |
|---|---|---|---|---|
| strat | VAR_INPUT | cli_dTStrat_degC | SV | stratification: ceiling above floor |
| low | VAR_INPUT | cli_dTStratLow_degC | P | stratification gap for fan speed n |
| med | VAR_INPUT | cli_dTStratMed_degC | P | stratification gap for fan speed n |
| high | VAR_INPUT | cli_dTStratHigh_degC | P | stratification gap for fan speed n |
| hyst | VAR_INPUT | cli_dTStratHyst_degC | P | gap change needed to switch fan speed |
| fan_low | VAR_INPUT | cli_fanLowDem | P | cooling effort for fan speed n |
| fan_med | VAR_INPUT | cli_fanMedDem | P | cooling effort for fan speed n |
| fan_high | VAR_INPUT | cli_fanHighDem | P | cooling effort for fan speed n |
| demand | VAR_OUTPUT | cli_stratDem | SV | destratification demand, n to n |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| bands | `Plc::Staircase3` | - |

<!-- page -->

### humidity

`HumidityController`, dispatched at SLOW, in the climate stage. It publishes 4 readings. It defines `observe()`, so it keeps reporting while the plant is parked.

The humidity ladder: four bands and a dead zone on the dew-point margin, published as one signed demand - and the rot guard under it.

Negative dehumidifies, positive humidifies. Set in degC rather than kPa because CONDENSATION is what is guarded against, and that happens at the temperature the surfaces are at. The guard is the same signal against a different question - `cli_rotMargin_degC` is what `alarm_rot` means - and it is one program because two laws reading one margin can disagree on the beat it moves.

| member | section | row | kind | description |
|---|---|---|---|---|
| rot_release_s | VAR_INPUT | cli_rotRelease_sec | P | rot lockout overrun past the condition |
| dp_margin | VAR_INPUT | cli_dpMargin_degC | SV | interior dew-point margin: how far from condensing |
| dehum_full | VAR_INPUT | cli_dehumFull_degC | P | dryness margin: dehumidify fully |
| dehum_start | VAR_INPUT | cli_dehumStart_degC | P | dryness margin: dehumidify starts |
| humid_start | VAR_INPUT | cli_humidStart_degC | P | dryness margin: humidify starts |
| humid_full | VAR_INPUT | cli_humidFull_degC | P | dryness margin: humidify fully |
| rot_limit | VAR_INPUT | cli_rotMargin_degC | P | dryness margin: rot risk |
| rot | VAR_INPUT | alarm_rot | A | dew-point margin low: rot risk |
| in_t | VAR_INPUT | cli_temp_hi | SV | ceiling temperature, voted and filtered |
| in_rh | VAR_INPUT | cli_rh_hi | SV | ceiling relative humidity, voted and filtered |
| ext_t | VAR_INPUT | cli_temp_ext | SV | exterior temperature, voted and filtered |
| ext_rh | VAR_INPUT | cli_rh_ext | SV | exterior relative humidity, voted and filtered |
| dry_margin | VAR_INPUT | cli_ventDryMgn_degC | P | outside air must be this much drier |
| vent_hyst | VAR_INPUT | cli_ventHyst_degC | P | how far a gate has to close before it opens |
| demand | VAR_OUTPUT | cli_humidDem | SV | humidity demand, -n dehumidify to +n humidify |
| lockout | VAR_OUTPUT | cli_rotLockout | SV | rot risk: nothing may humidify |
| vent_dem | VAR_OUTPUT | cli_rotVentDem | SV | anti-rot ventilation demand, n to n |
| dry_ok | VAR_OUTPUT | cli_intakeDryOk | SV | outside air is dry enough to be worth drawing |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| lockout_for | `Plc::Tof` | the lockout's overrun past the condition |
| dry_gate_band | `Plc::Hysteresis` | two dew points that converge, so a dead band |

<!-- page -->

### irr_modulation

`IrrigationModulation`, dispatched at SLOW, in the irrigation stage. It publishes 2 readings. It defines `observe()`, so it keeps reporting while the plant is parked.

The irrigation ladder: how thirsty the air has been, as one factor.

Evaporation in, a modulation factor out. It commands nothing, so all of it is in observe(), and it keeps folding while the plant is parked: an outage is not a dry spell.

| member | section | row | kind | description |
|---|---|---|---|---|
| vpd | VAR_INPUT | cli_vpd_kPa | SV | interior vapour-pressure deficit |
| dry_days | VAR_INPUT | irr_dryEwmaDays | P | days of dryness history set the dose |
| dry_lo | VAR_INPUT | irr_dryLo_kPa | P | air dryness giving the least water |
| dry_hi | VAR_INPUT | irr_dryHi_kPa | P | air dryness giving the most water |
| mod_min | VAR_INPUT | irr_modMin | P | least water given, in humid weather |
| mod_gamma | VAR_INPUT | irr_modGamma | P | how fast watering ramps up with dryness |
| vpd_avg | VAR_OUTPUT | irr_vpdAvg_kPa | SV | dryness tracker: VPD averaged over irr_dryEwmaDays |
| mod | VAR_OUTPUT | irr_mod | SV | irrigation modulation, irr_modMin (wet) to n (summer dry) |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| fold | `Plc::Ton` | one hour, self-reset: when the dryness average takes a sample |
| dryness | `Utils::ExpMovingAverage` | the dryness tracker |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _seeded | `bool` | `false` | - |

<!-- page -->

### irr_mtr_input

`IrrMtrInput`, dispatched at SLOW, in the irrigation stage. It publishes 2 readings and raises 2 alarms. It defines `observe()`, so it keeps reporting while the plant is parked.

The inlet meter, and the two supply faults it alone can see.

A PULSE TRAIN, so the COUNTER is the measurement and the rate is derived - the opposite of the outlet, which is why the two are separate programs. An instrument, not a law: it drives no valve and it reads while parked.

| member | section | row | kind | description |
|---|---|---|---|---|
| rate_win | VAR_INPUT | irr_rateWin_ms | P | window the inlet rate is differenced over |
| qty_per_pulse | VAR_INPUT | irr_input_qtyPerImp | P | litres per pulse from the water meter |
| leak_Lpm | VAR_INPUT | irr_leak_Lpm | P | inlet flow with the cycle idle is a leak |
| min_fill_Lpm | VAR_INPUT | irr_minFill_Lpm | P | a fill under this rate is a pressure fault |
| state | VAR_INPUT | irr_state | SVE | what the irrigation cycle is doing |
| in_flow | VAR_OUTPUT | irr_inFlow_Lpm | SV | inlet flow, differentiated from the counter |
| in_total | VAR_OUTPUT | irr_inTotal_L | SV | inlet water counter, since it was last zeroed |
| leak | VAR_OUTPUT | alarm_water_leak | A | leak in irrigation circuit |
| pressure | VAR_OUTPUT | alarm_water_pressure | A | water pressure too low |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| rate | `Plc::Derivative` | litres counted, differenced back into a rate |
| idle_for | `Plc::Ton` | two rate windows of IDLE before a flow means a leak |
| fill_for | `Plc::Ton` | two rate windows of FILLING before no flow means no pressure |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _base_L | `float` | `0.0f` | Where the counter stood when this run adopted it: the pulse counter restarts at boot and the published total does not, so the total is the retained value plus what has been counted since. |
| _base_pulses | `uint32_t` | `0` | - |

<!-- page -->

### irr_mtr_output

`IrrMtrOutput`, dispatched at SLOW, in the irrigation stage. It publishes 2 readings. It defines `observe()`, so it keeps reporting while the plant is parked.

The outlet meter: what came back off the trays.

A 4-20 mA loop, so the RATE is the measurement and the total is integrated - the opposite of the inlet, which is why the two are separate programs.

| member | section | row | kind | description |
|---|---|---|---|---|
| band | VAR_INPUT | irr_flowBand_Lpm | P | outlet flow change worth a status write |
| cutoff | VAR_INPUT | irr_flowCutoff_Lpm | P | below this the outlet counts no litres |
| out_flow | VAR_INPUT | irr_out_Lpm | AI | irrigation output flow |
| out_avg_ms | VAR_INPUT | irr_out_avg_ms | P | rolling average for this flow meter |
| out_flow_pub | VAR_OUTPUT | irr_outFlow_Lpm | SV | outlet flow, averaged over irr_out_avg_ms |
| out_total | VAR_OUTPUT | irr_outTotal_L | SV | outlet water counter, since it was last zeroed |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| flow_avg | `Plc::Ewma` | the loop's own last digit, averaged out |
| flow_band | `Plc::Deadband` | how far a flow has to move to be worth a write |
| meter | `Plc::Integral` | the totalizer, counting litres per second |

<!-- page -->

### irr_schedule

`IrrigationSchedule`, dispatched at SLOW, in the irrigation stage. It publishes 30 readings.

Which tray is owed water, and whether the climate will allow a cycle to start.

The decision, published: which tray goes next is what the whole plan turns on, and a decision nobody can see is one nobody can rehearse. It commands nothing - the cycle downstream reads `irr_dueTray` and serves it.

A day's allowance is counted here and spent by the cycle, through `irr_servedToday`: before NTP there is no date to stamp against, and the tally is then the only thing bounding the day.

| member | section | row | kind | description |
|---|---|---|---|---|
| mod | VAR_INPUT | irr_mod | SV | irrigation modulation, irr_modMin (wet) to n (summer dry) |
| split | VAR_INPUT | irr_volFreqSplit | P | cut the volume, or stretch the period |
| trays_per_day | VAR_INPUT | irr_traysPerDay | P | most trays watered in one day |
| days[8] | VAR_INPUT | irr_days_tray1..8 | P | days between waterings in summer |
| outside_temp | VAR_INPUT | cli_temp_ext | SV | exterior temperature, voted and filtered |
| dawn_gate | VAR_INPUT | irr_dawnGate_degC | P | skip pre-dawn watering below this |
| phase | VAR_INPUT | cli_phase | SVE | phase of the day in force |
| from_rise | VAR_INPUT | sol_fromRise_min | SV | minutes since sunrise; negative before it |
| day_rise | VAR_INPUT | sol_dayRise_min | P | day operation starts, from sunrise |
| morning_win | VAR_INPUT | irr_morningWin_min | P | how long into the day the morning slot runs |
| due_tray | VAR_OUTPUT | irr_dueTray | SV | tray owed water now, n when none is |
| slot_open | VAR_OUTPUT | irr_slotOpen | SV | the climate allows a cycle to start |
| period | VAR_OUTPUT | irr_period_days | SV | modulated interval, for the tray due next |
| last[8] | VAR_IN_OUT | irr_last_tray1..8 | P | last watering (days since n Jan n) |
| force_on[8] | VAR_IN_OUT | irr_forceOn_tray1..8 | P | force watering this tray next cycle |
| force_off[8] | VAR_IN_OUT | irr_forceOff_tray1..8 | P | force skipping this tray next cycle |
| force_now | VAR_IN_OUT | irr_forceNow | P | water now, then clear this flag |
| today | VAR_IN_OUT | irr_today | SV | the day the schedule is running against, n before the clock is set |
| served | VAR_IN_OUT | irr_servedToday | SV | trays served since the day rolled over |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _served_day | `uint32_t` | `0` | The UNIX day the tally is counted against; it rolls when the date does, so a cycle spanning midnight is not counted twice. |

<!-- page -->

### irrigation

`IrrigationCycle`, dispatched at SLOW, in the irrigation stage. It publishes 24 readings and raises 18 alarms.

Irrigation: one tray at a time, filled to a dose, steeped, and drained.

FILLING -> SOAKING -> DRAINING -> FLUSHING. A cycle is long, so only `irr_traysPerDay` are served in a day and the surplus waits. A tray's settings are its DRY-WEATHER ones; `irr_mod` scales them, and the split decides whether that shows up as smaller doses or rarer ones.

| member | section | row | kind | description |
|---|---|---|---|---|
| due_tray | VAR_INPUT | irr_dueTray | SV | tray owed water now, n when none is |
| mod | VAR_INPUT | irr_mod | SV | irrigation modulation, irr_modMin (wet) to n (summer dry) |
| in_total | VAR_INPUT | irr_inTotal_L | SV | inlet water counter, since it was last zeroed |
| out_total | VAR_INPUT | irr_outTotal_L | SV | outlet water counter, since it was last zeroed |
| litres[8] | VAR_INPUT | irr_liters_tray1..8 | P | litres to flood this tray in summer |
| split | VAR_INPUT | irr_volFreqSplit | P | cut the volume, or stretch the period |
| max_fill_s | VAR_INPUT | irr_maxFill_sec | P | alarm if a tray takes longer to fill |
| soak_s | VAR_INPUT | irr_soak_sec | P | how long a filled tray sits before draining |
| drain_window_s | VAR_INPUT | irr_drainWindow_sec | P | time allowed for a tray to drain |
| flush_s | VAR_INPUT | irr_flush_sec | P | how long the manifold is rinsed |
| exp_drain | VAR_INPUT | irr_expDrainFrac | P | fraction expected to drain back |
| drain_margin | VAR_INPUT | irr_drainMargin | P | clog if drained is this far under |
| out_flow | VAR_INPUT | irr_outFlow_Lpm | SV | outlet flow, averaged over irr_out_avg_ms |
| volume | VAR_OUTPUT | irr_volume_L | SV | modulated dose, for the tray being served |
| state | VAR_OUTPUT | irr_state | SVE | what the irrigation cycle is doing |
| active | VAR_OUTPUT | irr_activeTray | SV | tray being served, n when none |
| fill_fault | VAR_OUTPUT | alarm_irr_fill | A | a tray would not fill in time |
| drain_fault | VAR_OUTPUT | alarm_irr_drain | A | a tray would not drain in time |
| fill_tray[8] | VAR_OUTPUT | alarm_irr_fill_tray1..8 | A | tray n would not fill in time |
| drain_tray[8] | VAR_OUTPUT | alarm_irr_drain_tray1..8 | A | tray n would not drain in time |
| served | VAR_IN_OUT | irr_servedToday | SV | trays served since the day rolled over |
| last[8] | VAR_IN_OUT | irr_last_tray1..8 | P | last watering (days since n Jan n) |
| force_on[8] | VAR_IN_OUT | irr_forceOn_tray1..8 | P | force watering this tray next cycle |
| force_now | VAR_IN_OUT | irr_forceNow | P | water now, then clear this flag |
| today | VAR_IN_OUT | irr_today | SV | the day the schedule is running against, n before the clock is set |
| delivered | VAR_IN_OUT | irr_delivered_L | SV | delivered into the tray being filled |
| drained | VAR_IN_OUT | irr_drained_L | SV | drained back out of the tray being served |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| dwell | `Plc::Ton` | how long this state has run; _enter() resets it |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _state | `IrrState` | `IRR_IDLE` | - |
| _tray | `uint8_t` | `0` | 1..N_TRAY_VALVE while serving, else 0 |
| _fill_start_L | `float` | `0.0f` | The two meters' totals when the window under judgement opened. Litres are the DIFFERENCE, so a counter that has run since boot - or since last year, retained - needs no reset. |
| _drain_start_L | `float` | `0.0f` | - |
| _dose_L | `float` | `0.0f` | What the tray actually got, kept because the modulation may move under a cycle that is already draining. |
| _forced | `bool` | `false` | this cycle came from irr_forceNow |

<!-- page -->

### irr_valves

`IrrValves`, dispatched at SLOW, in the irrigation stage. It drives 12 actuator rows.

The irrigation manifold: which valves each state of the cycle opens.

Split from irrigation_cycle, which decides the STATE - this only opens what that state means. It reads the two rows the cycle publishes, so an operator forcing either drives the manifold directly.

| member | section | row | kind | description |
|---|---|---|---|---|
| state | VAR_INPUT | irr_state | SVE | what the irrigation cycle is doing |
| active | VAR_INPUT | irr_activeTray | SV | tray being served, n when none |
| inlet[2] | VAR_OUTPUT | irr_input_ro1..2 | RO | irrigation input valve n |
| outlet[2] | VAR_OUTPUT | irr_output_ro1..2 | RO | irrigation output valve n |
| tray_valve[8] | VAR_OUTPUT | irr_tray1..8 | RO | tray n valve |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| inlet_v[2] | `ValveController` | two contactors on one manifold |
| outlet_v[2] | `ValveController` | - |
| tray_v[8] | `ValveController` | - |

<!-- page -->

### screen

`ScreenController`, dispatched at SLOW, in the actuators stage. It drives 1 actuator row and publishes 1 reading. It defines `observe()`, so it keeps reporting while the plant is parked.

The thermal screen: one coil, deployed when the greenhouse is losing heat rather than gaining it.

Two independent causes, either sufficient: the cooling ladder far enough negative, or a cold night. No feedback, so travel is TIMED: the fabric block carries the estimate and says UNKNOWN until a stroke to an end has datumed it. The destratification fans are cut while it moves through the air they blow into.

| member | section | row | kind | description |
|---|---|---|---|---|
| cool_dem | VAR_INPUT | cli_coolDem | SV | cooling demand, -n warming to +n cooling |
| deploy_dem | VAR_INPUT | cli_thermalScreenDem | P | cooling effort that deploys the screen |
| phase | VAR_INPUT | cli_phase | SVE | phase of the day in force |
| travel_s | VAR_INPUT | cli_screenTravel_sec | P | time to deploy/remove the screen |
| travel_tmo_s | VAR_INPUT | cli_screenTmo_sec | P | safe time to deploy/remove the screen |
| screen | VAR_OUTPUT | cli_screen | RO | thermal screen |
| moving | VAR_OUTPUT | cli_screenMoving | SV | thermal screen travelling |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| fabric | `Plc::Actuator` | where the fabric is, and whether it is travelling |

<!-- page -->

### roof_lockout

`RoofLockout`, dispatched at SLOW, in the actuators stage. It publishes 4 readings.

What forbids the roof being open at all, whatever the ladder asks.

Stage 1 of the roof's arbitration, and absolute: rain, wind and a supply lockout shut the windows whatever else is true, including their own safe state. Its own program because the interlocks are a judgement about the WEATHER, while the windows are a judgement about four rams sharing one bus.

Published and then obeyed: the windows read `cli_windowLockout` back rather than a verdict of their own, so an operator can force one and hold the roof shut without inventing weather. It is also the only way to rehearse stage 1.

| member | section | row | kind | description |
|---|---|---|---|---|
| rain_mmph | VAR_INPUT | cli_rain_mmph | MI | rainfall rate |
| wind_mps | VAR_INPUT | cli_wind_mps | MI | wind speed at the station |
| gust_mps | VAR_INPUT | cli_windGust_mps | MI | peak gust over the station's own window |
| shut_end | VAR_INPUT | cli_windShutEnd_mps | P | wind that shuts the roof and is a storm |
| shut_begin | VAR_INPUT | cli_windShutBegin_mps | P | wind that starts capping the opening |
| shut_max | VAR_INPUT | cli_windShutMax | P | how far the roof may open in that band |
| rain_hold_s | VAR_INPUT | cli_rainLockout_sec | P | keep windows shut this long after rain |
| supply_lock | VAR_INPUT | sys_supply_lockout | SV | supply unhealthy: actuators held off |
| cap | VAR_OUTPUT | cli_windowCap | SV | the most the roof may open |
| rain_lock | VAR_IN_OUT | cli_rainLockout | SV | rain holds the windows shut |
| wind_lock | VAR_IN_OUT | cli_windLockout | SV | wind holds the windows shut |
| window_lock | VAR_IN_OUT | cli_windowLockout | SV | the roof is held shut, whatever the ladder wants |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| rain_for | `Plc::Tof` | the rain lockout's overrun past the last drop |

<!-- page -->

### windows

`WindowsController`, dispatched at SLOW, in the actuators stage. It drives 8 actuator rows and publishes 8 readings. It defines `observe()`, so it keeps reporting while the plant is parked.

The four roof windows: one target from the cooling ladder, and ONE MOVING AT A TIME.

One at a time is ELECTRICAL, so a safety cannot break it either: a rain interlock shuts them in sequence. A window keeps the bus until its stroke ends, so a target moving mid-travel cannot leave a coil energised. The humidity demand is deliberately not read: cooling wins over anti-dryness, and with no cooling demand they are shut anyway.

| member | section | row | kind | description |
|---|---|---|---|---|
| window_dem | VAR_INPUT | cli_windowDem | SV | how far the cooling ladder wants the windows open |
| travel_s | VAR_INPUT | cli_windowTravel_sec | P | time to fully open/close a window |
| tmo_s | VAR_INPUT | cli_windowTmo_sec | P | safe time to fully open/close a window |
| window_lock | VAR_INPUT | cli_windowLockout | SV | the roof is held shut, whatever the ladder wants |
| cap | VAR_INPUT | cli_windowCap | SV | the most the roof may open |
| opn[4] | VAR_OUTPUT | cli_window1_opn, cli_window2_opn, cli_window3_opn, cli_window4_opn | RO | window n open |
| clo[4] | VAR_OUTPUT | cli_window1_clo, cli_window2_clo, cli_window3_clo, cli_window4_clo | RO | window n close |
| tgt[4] | VAR_OUTPUT | cli_window1_tgt, cli_window2_tgt, cli_window3_tgt, cli_window4_tgt | SV | window n commanded position, n to n |
| pos[4] | VAR_OUTPUT | cli_window1_pos, cli_window2_pos, cli_window3_pos, cli_window4_pos | SV | window n position, n to n |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| act[4] | `Plc::Actuator` | - |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _target | `float` | `0.0f` | The position the arbitration asked for this scan. One number for all four: they are one roof. |
| _safe_open | `float` | `0.0f` | Where the roof goes when this law is not acting: PART OPEN, not shut, read off the target row's declared default like every other safe state here. It should never be reached, but a greenhouse with some ventilation survives a summer afternoon and a sealed one does not. @note NOT the interlocks: rain and wind shut the roof outright. |

<!-- page -->

### weather_watch

`WeatherWatch`, dispatched at SLOW, in the sense stage. It publishes 1 reading and raises 2 alarms. It defines `observe()`, so it keeps reporting while the plant is parked.

The weather station, watched: whether it is answering, and whether what it says is a storm.

It reads the roof's two lockouts rather than declaring a second pair of thresholds beside them: a storm alarm that disagreed with the actuator it is about would be worse than no alarm.

| member | section | row | kind | description |
|---|---|---|---|---|
| wind | VAR_INPUT | cli_wind_mps | MI | wind speed at the station |
| gust | VAR_INPUT | cli_windGust_mps | MI | peak gust over the station's own window |
| rain | VAR_INPUT | cli_rain_mmph | MI | rainfall rate |
| irrad | VAR_INPUT | cli_irrad_Wpm2 | MI | solar irradiance |
| storm_wind | VAR_INPUT | cli_windShutEnd_mps | P | wind that shuts the roof and is a storm |
| station | VAR_OUTPUT | alarm_wx_station | A | weather station not answering |
| storm | VAR_OUTPUT | alarm_weather_storm | A | current storm conditions |
| since_rain | VAR_OUTPUT | cli_sinceRain_min | SV | minutes since the gauge last read wet |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _seen | `bool` | `false` | Whether the station has EVER answered. Before it has, silence is bring-up; after, it is a fault. @see AcSupply |
| _dry_ms | `uint64_t` | `0` | Ms since the gauge last read wet, and whether it ever has in this run. A boot does not know when it last rained, so before the first shower the row is held rather than reading "just now". |
| _wet_seen | `bool` | `false` | - |

<!-- page -->

### intractors

`IntractorController`, dispatched at SLOW, in the actuators stage. It drives 2 actuator rows.

The two intractors: variable-speed fans drawing air from under the floor, driven by whichever of three demands wants the most.

Both run at one speed - they serve one volume - and the fusion is a MAXIMUM, so each input is a floor rather than a share. The ramp spans the two staging thresholds, so the windows and the intractors hand over rather than overlap.

| member | section | row | kind | description |
|---|---|---|---|---|
| cool_dem | VAR_INPUT | cli_coolDem | SV | cooling demand, -n warming to +n cooling |
| rot_vent | VAR_INPUT | cli_rotVentDem | SV | anti-rot ventilation demand, n to n |
| humid_dem | VAR_INPUT | cli_humidDem | SV | humidity demand, -n dehumidify to +n humidify |
| ground_speed | VAR_INPUT | cli_groundSpeed | P | intractor speed while pulling ground heat |
| start_dem | VAR_INPUT | cli_intakeStartDem | P | cooling effort that starts intake fans |
| full_dem | VAR_INPUT | cli_intakeFullDem | P | cooling effort at full intake speed |
| cool_ok | VAR_INPUT | cli_intakeCoolOk | SV | intake air is cool enough to be worth drawing |
| ground_ok | VAR_INPUT | cli_groundHeatOk | SV | intake air is warm enough to be worth drawing |
| dry_ok | VAR_INPUT | cli_intakeDryOk | SV | outside air is dry enough to be worth drawing |
| intr[2] | VAR_OUTPUT | cli_intr1..2 | AO | intractor n speed |

What it remembers between activations:

| variable | type | starts at | what it is |
|---|---|---|---|
| _safe_speed | `float` | `0.0f` | Where these go when this law is not acting, off the row's own declared safe value. NOT stopped: some air is always wanted. |

<!-- page -->

### fans

`FanController`, dispatched at SLOW, in the actuators stage. It drives 6 actuator rows and publishes 1 reading.

The two destratification fans: three relayed speeds, driven by whichever of three demands wants the most.

The three speed relays are exclusive, and the band the fused demand falls in picks one. That band is hysteretic: the inputs are FLOORS, so the winner moves as the plant does. A travelling screen cuts them outright.

| member | section | row | kind | description |
|---|---|---|---|---|
| dem_hyst | VAR_INPUT | cli_fanDemHyst | P | demand change needed to switch fan speed |
| irr_dem | VAR_INPUT | cli_fanIrrDem | P | fan demand while a tray is being watered |
| strat_dem | VAR_INPUT | cli_stratDem | SV | destratification demand, n to n |
| cool_dem | VAR_INPUT | cli_coolDem | SV | cooling demand, -n warming to +n cooling |
| irr_state | VAR_INPUT | irr_state | SVE | what the irrigation cycle is doing |
| screen_moving | VAR_INPUT | cli_screenMoving | SV | thermal screen travelling |
| low_dem | VAR_INPUT | cli_fanLowDem | P | cooling effort for fan speed n |
| med_dem | VAR_INPUT | cli_fanMedDem | P | cooling effort for fan speed n |
| high_dem | VAR_INPUT | cli_fanHighDem | P | cooling effort for fan speed n |
| phase | VAR_INPUT | cli_phase | SVE | phase of the day in force |
| breeze_dem | VAR_INPUT | cli_breezeDem | P | brassage floor during the two breezes |
| speed1[2] | VAR_OUTPUT | cli_destrat1_speed1, cli_destrat2_speed1 | RO | destratification fan n speed n |
| speed2[2] | VAR_OUTPUT | cli_destrat1_speed2, cli_destrat2_speed2 | RO | destratification fan n speed n |
| speed3[2] | VAR_OUTPUT | cli_destrat1_speed3, cli_destrat2_speed3 | RO | destratification fan n speed n |
| band | VAR_OUTPUT | cli_fanSpeed | SV | destratification fan speed in force, n to n |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| steps | `Plc::Staircase3` | - |

<!-- page -->

### skirt

`SkirtController`, dispatched at SLOW, in the actuators stage. It drives 1 actuator row and publishes 1 reading.

The wet skirt: one valve pulsed against two demands.

PROPORTIONAL on the humidify arm - it IS the humidifier - and STAGED on the cooling axis, only while the air is dry enough for a wet pad to cool anything. The rot lockout cuts it outright: wetting condensing air is the one thing this valve must never do.

| member | section | row | kind | description |
|---|---|---|---|---|
| cool_dem | VAR_INPUT | cli_coolDem | SV | cooling demand, -n warming to +n cooling |
| humid_dem | VAR_INPUT | cli_humidDem | SV | humidity demand, -n dehumidify to +n humidify |
| cool_start | VAR_INPUT | cli_skirtCoolDem | P | cooling effort that starts the skirt |
| dry_min | VAR_INPUT | cli_skirtDryMin_degC | P | air this dry lets the pad cool |
| dp_margin | VAR_INPUT | cli_dpMargin_degC | SV | interior dew-point margin: how far from condensing |
| rot_lock | VAR_INPUT | cli_rotLockout | SV | rot risk: nothing may humidify |
| period_s | VAR_INPUT | cli_skirtPeriod_sec | P | skirt valve modulation period |
| min_on_s | VAR_INPUT | cli_skirtMinOn_sec | P | shortest skirt valve pulse |
| valve | VAR_OUTPUT | cli_skirtValve | RO | wet skirt valve |
| duty | VAR_OUTPUT | cli_skirtDuty | SV | skirt valve on-time, n to n |

Function blocks it holds:

| block | type | what for |
|---|---|---|
| pulse | `ValveController` | the on-time within a period |

## Signal networks

3 networks over 26 programs and 104
connections. A network is a set of programs that can reach each
other through the table; programs in different networks cannot
affect one another, whatever order they run in.

### Network 1: climate_watch, cooling, day_phase, dc_watch, destrat, fans, humidity, intractors, irr_modulation, irr_mtr_input, irr_mtr_output, irr_schedule, irr_valves, irrigation, roof_lockout, screen, skirt, strat, sun_watch, target_temp, temp_rh_sensors, temp_rh_zone, windows

| produces | row | is read by |
|---|---|---|
| cooling | cli_coolDem | fans |
| cooling | cli_coolDem | intractors |
| cooling | cli_groundHeatOk | intractors |
| cooling | cli_intakeCoolOk | intractors |
| cooling | cli_coolDem | screen |
| cooling | cli_coolDem | skirt |
| cooling | cli_windowDem | windows |
| day_phase | cli_phase | fans |
| day_phase | cli_phase | irr_schedule |
| day_phase | cli_phase | screen |
| day_phase | cli_coldBankArm | target_temp |
| day_phase | cli_heatBankArm | target_temp |
| day_phase | cli_phase | target_temp |
| dc_watch | sys_supply_lockout | roof_lockout |
| destrat | cli_stratDem | fans |
| humidity | cli_humidDem | intractors |
| humidity | cli_intakeDryOk | intractors |
| humidity | cli_rotVentDem | intractors |
| humidity | cli_humidDem | skirt |
| humidity | cli_rotLockout | skirt |
| irr_modulation | irr_mod | irr_schedule |
| irr_modulation | irr_mod | irrigation |
| irr_mtr_input | irr_inTotal_L | irrigation |
| irr_mtr_output | irr_outFlow_Lpm | irrigation |
| irr_mtr_output | irr_outTotal_L | irrigation |
| irr_schedule | irr_dueTray | irrigation |
| irr_schedule | irr_forceNow | irrigation |
| irr_schedule | irr_forceOn_tray1 | irrigation |
| irr_schedule | irr_forceOn_tray2 | irrigation |
| irr_schedule | irr_forceOn_tray3 | irrigation |
| irr_schedule | irr_forceOn_tray4 | irrigation |
| irr_schedule | irr_forceOn_tray5 | irrigation |
| irr_schedule | irr_forceOn_tray6 | irrigation |
| irr_schedule | irr_forceOn_tray7 | irrigation |
| irr_schedule | irr_forceOn_tray8 | irrigation |
| irr_schedule | irr_last_tray1 | irrigation |
| irr_schedule | irr_last_tray2 | irrigation |
| irr_schedule | irr_last_tray3 | irrigation |
| irr_schedule | irr_last_tray4 | irrigation |
| irr_schedule | irr_last_tray5 | irrigation |
| irr_schedule | irr_last_tray6 | irrigation |
| irr_schedule | irr_last_tray7 | irrigation |
| irr_schedule | irr_last_tray8 | irrigation |
| irr_schedule | irr_servedToday | irrigation |
| irr_schedule | irr_today | irrigation |
| irrigation | irr_state | fans |
| irrigation | irr_state | irr_mtr_input |
| irrigation | irr_forceNow | irr_schedule |
| irrigation | irr_forceOn_tray1 | irr_schedule |
| irrigation | irr_forceOn_tray2 | irr_schedule |
| irrigation | irr_forceOn_tray3 | irr_schedule |
| irrigation | irr_forceOn_tray4 | irr_schedule |
| irrigation | irr_forceOn_tray5 | irr_schedule |
| irrigation | irr_forceOn_tray6 | irr_schedule |
| irrigation | irr_forceOn_tray7 | irr_schedule |
| irrigation | irr_forceOn_tray8 | irr_schedule |
| irrigation | irr_last_tray1 | irr_schedule |
| irrigation | irr_last_tray2 | irr_schedule |
| irrigation | irr_last_tray3 | irr_schedule |
| irrigation | irr_last_tray4 | irr_schedule |
| irrigation | irr_last_tray5 | irr_schedule |
| irrigation | irr_last_tray6 | irr_schedule |
| irrigation | irr_last_tray7 | irr_schedule |
| irrigation | irr_last_tray8 | irr_schedule |
| irrigation | irr_servedToday | irr_schedule |
| irrigation | irr_today | irr_schedule |
| irrigation | irr_activeTray | irr_valves |
| irrigation | irr_state | irr_valves |
| roof_lockout | cli_windowCap | windows |
| roof_lockout | cli_windowLockout | windows |
| screen | cli_screenMoving | fans |
| strat | cli_dTStrat_degC | climate_watch |
| strat | cli_dTStrat_degC | destrat |
| sun_watch | sol_fromRise_min | day_phase |
| sun_watch | sol_fromSet_min | day_phase |
| sun_watch | sol_fromRise_min | irr_schedule |
| target_temp | cli_targetTemp_degC | cooling |
| temp_rh_sensors | alarm_sns_ceiling | climate_watch |
| temp_rh_sensors | alarm_sns_crawlspace | climate_watch |
| temp_rh_sensors | alarm_sns_exterior | climate_watch |
| temp_rh_sensors | alarm_sns_floor | climate_watch |
| temp_rh_sensors | cli_temp_hi | climate_watch |
| temp_rh_sensors | cli_temp_cs | cooling |
| temp_rh_sensors | cli_temp_hi | cooling |
| temp_rh_sensors | cli_temp_lo | cooling |
| temp_rh_sensors | cli_rh_ext | humidity |
| temp_rh_sensors | cli_rh_hi | humidity |
| temp_rh_sensors | cli_temp_ext | humidity |
| temp_rh_sensors | cli_temp_hi | humidity |
| temp_rh_sensors | cli_temp_ext | irr_schedule |
| temp_rh_sensors | cli_temp_hi | strat |
| temp_rh_sensors | cli_temp_lo | strat |
| temp_rh_sensors | cli_rh_hi | temp_rh_zone |
| temp_rh_sensors | cli_rh_lo | temp_rh_zone |
| temp_rh_sensors | cli_temp_hi | temp_rh_zone |
| temp_rh_sensors | cli_temp_lo | temp_rh_zone |
| temp_rh_zone | cli_dpMargin_degC | climate_watch |
| temp_rh_zone | cli_temp_in | cooling |
| temp_rh_zone | cli_temp_in | day_phase |
| temp_rh_zone | alarm_rot | humidity |
| temp_rh_zone | cli_dpMargin_degC | humidity |
| temp_rh_zone | cli_vpd_kPa | irr_modulation |
| temp_rh_zone | cli_dpMargin_degC | skirt |

### Network 2: ac_grid_watch, ac_local_watch

| produces | row | is read by |
|---|---|---|
| ac_grid_watch | sys_grid_ok | ac_local_watch |

### Network 3: weather_watch

Stands alone: it produces nothing another program reads, and
reads nothing another produces. What it does read and write:

| produces | row | is read by |
|---|---|---|
| - | cli_wind_mps | weather_watch |
| - | cli_windGust_mps | weather_watch |
| - | cli_rain_mmph | weather_watch |
| - | cli_irrad_Wpm2 | weather_watch |
| - | cli_windShutEnd_mps | weather_watch |
| weather_watch | alarm_wx_station | - |
| weather_watch | alarm_weather_storm | - |
| weather_watch | cli_sinceRain_min | - |

## Orphans

0 rows with one end missing. A row nothing writes is an
ERROR and stops the build; a parameter nothing reads is a NOTE,
because a threshold declared ahead of the law that will read it is
how this site gets built. The other kind - a parameter whose law
was rewritten around it - looks identical from inside the table,
which is why the list is printed rather than kept.

### Computed, and no law reads it

60 rows. Most are TERMINAL by design - an instrument's mirror, an actuator's position, a totalizer - and exist for the page and the field bus. What to look for here is an INTERMEDIATE: a quantity the control system derives, publishes, and then bypasses by keeping its own copy. That row cannot be forced and does not steer anything, so it lies the moment an operator leans on it. Whatever a law decides on, it should decide on the ROW.

| row | kind | what it is |
|---|---|---|
| cli_dewPoint_degC | SV | interior dew point (Magnus) |
| cli_fanSpeed | SV | destratification fan speed in force, 0 to 3 |
| cli_rh_cs | SV | crawlspace relative humidity, voted and filtered |
| cli_satPress_kPa | SV | interior saturation vapour pressure (Tetens) |
| cli_sinceRain_min | SV | minutes since the gauge last read wet |
| cli_skirtDuty | SV | skirt valve on-time, 0 to 1 |
| cli_window1_pos | SV | window 1 position, 0 to 1 |
| cli_window1_tgt | SV | window 1 commanded position, 0 to 1 |
| cli_window2_pos | SV | window 2 position, 0 to 1 |
| cli_window2_tgt | SV | window 2 commanded position, 0 to 1 |
| cli_window3_pos | SV | window 3 position, 0 to 1 |
| cli_window3_tgt | SV | window 3 commanded position, 0 to 1 |
| cli_window4_pos | SV | window 4 position, 0 to 1 |
| cli_window4_tgt | SV | window 4 commanded position, 0 to 1 |
| irr_inFlow_Lpm | SV | inlet flow, differentiated from the counter |
| irr_period_days | SV | modulated interval, for the tray due next |
| irr_slotOpen | SV | the climate allows a cycle to start |
| irr_volume_L | SV | modulated dose, for the tray being served |
| irr_vpdAvg_kPa | SV | dryness tracker: VPD averaged over irr_dryEwmaDays |
| prog_ac_grid_watch | SVE | health of the ac_grid_watch program |
| prog_ac_local_watch | SVE | health of the ac_local_watch program |
| prog_climate_watch | SVE | health of the climate_watch program |
| prog_cooling | SVE | health of the cooling program |
| prog_day_phase | SVE | health of the day_phase program |
| prog_dc_watch | SVE | health of the dc_watch program |
| prog_destrat | SVE | health of the destrat program |
| prog_fans | SVE | health of the fans program |
| prog_humidity | SVE | health of the humidity program |
| prog_intractors | SVE | health of the intractors program |
| prog_irr_modulation | SVE | health of the irrigation modulation program |
| prog_irr_mtr_input | SVE | health of the inlet meter program |
| prog_irr_mtr_output | SVE | health of the outlet meter program |
| prog_irr_schedule | SVE | health of the irrigation schedule |
| prog_irr_valves | SVE | health of the irrigation valves program |
| prog_irrigation | SVE | health of the irrigation program |
| prog_roof_lockout | SVE | health of the roof lockout program |
| prog_screen | SVE | health of the screen program |
| prog_skirt | SVE | health of the skirt program |
| prog_strat | SVE | health of the strat program |
| prog_sun_watch | SVE | health of the sun watch program |
| prog_target_temp | SVE | health of the target_temp program |
| prog_temp_rh_sensors | SVE | health of the temp_rh_sensors program |
| prog_temp_rh_zone | SVE | health of the temp_rh_zone program |
| prog_weather_watch | SVE | health of the weather_watch program |
| prog_windows | SVE | health of the windows program |
| sol_elev_deg | SV | the sun's elevation above the horizon |
| sys_dc1_V | SV | DC PS1 volts |
| sys_dc2_V | SV | DC PS2 volts |
| sys_dc_ok | SV | every DC rail above its floor, and the UPS off its low-battery contact |
| sys_genBat_V | SV | generator start battery volts |
| sys_grid_A | SV | grid total current |
| sys_grid_Hz | SV | grid frequency |
| sys_grid_V | SV | grid line-to-neutral volts |
| sys_grid_W | SV | grid active power |
| sys_local_A | SV | local AC total current |
| sys_local_Hz | SV | local AC frequency |
| sys_local_V | SV | local AC line-to-neutral volts |
| sys_local_W | SV | local AC active power |
| sys_local_ok | SV | local AC supply within its bands |
| sys_upsBat_V | SV | UPS battery volts |

## The field bus

The RS-485 segment, by unit: what answers there and which rows it
carries. Same rows as the tables above, addressed the way an
engineer at the cabinet has them - by the device they are wired to.

Unit 0 is the controller's own terminals, on no bus at all.

| unit | tag | model | shape | rows |
|---|---|---|---|---|
| 5 | SRR/TAC/MTR-1 | Eastron SDM630 | meter | 37 |
| 6 | SRR/TAC/MTR-2 | Lettel MCT1-140 | meter | 6 |
| 11 | SRR/TDC/AI-1 | Waveshare AI 8CH | io_board | 5 |
| 16 | SRR/TDC/AO-1 | Waveshare AO 8CH | io_board | 2 |
| 21 | SRR/CLI/WX-1 | Weather station | meter | 4 |
| 31 | SRR/TDC/RO-1 | Waveshare Relay (D) | relay_board | 6 |
| 32 | SRR/TDC/RO-2 | Waveshare Relay (D) | relay_board | 6 |
| 33 | SRR/TDC/RO-3 | Waveshare Relay (D) | relay_board | 8 |
| 34 | SRR/TDC/RO-4 | Waveshare Relay (D) | relay_board | 8 |
| 41 | SRR/CLI/TRH-EXT-1 | Temp/RH sensor | meter | 2 |
| 42 | SRR/CLI/TRH-EXT-2 | Temp/RH sensor | meter | 2 |
| 43 | SRR/CLI/TRH-EXT-3 | Temp/RH sensor | meter | 2 |
| 44 | SRR/CLI/TRH-CS-1 | Temp/RH sensor | meter | 2 |
| 45 | SRR/CLI/TRH-CS-2 | Temp/RH sensor | meter | 2 |
| 46 | SRR/CLI/TRH-CS-3 | Temp/RH sensor | meter | 2 |
| 47 | SRR/CLI/TRH-LO-1 | Temp/RH sensor | meter | 2 |
| 48 | SRR/CLI/TRH-LO-2 | Temp/RH sensor | meter | 2 |
| 49 | SRR/CLI/TRH-LO-3 | Temp/RH sensor | meter | 2 |
| 50 | SRR/CLI/TRH-HI-1 | Temp/RH sensor | meter | 2 |
| 51 | SRR/CLI/TRH-HI-2 | Temp/RH sensor | meter | 2 |
| 52 | SRR/CLI/TRH-HI-3 | Temp/RH sensor | meter | 2 |

### Unit 0: the controller's own terminals

`XP_LOCAL`: no bus, no unit address. The `reg` column is the
OPTA channel, and `LocalInputs` publishes it at PHASE_INPUT.

| name | dir | fc | at | description |
|---|---|---|---|---|
| irr_inPulse | read | 2 | 1 | inlet water meter pulse (interrupt-counted) |
| pwr_upsLowBat | read | 2 | 0 | UPS low battery contact |

### Unit 5: SRR/TAC/MTR-1

Eastron SDM630 (meter), at most 80 registers per request, addresses aligned to 2.

grid supply meter.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| grid_L1_V | read | 4 | 0 | 2 | V | L1 phase voltage |
| grid_L2_V | read | 4 | 2 | 2 | V | L2 phase voltage |
| grid_L3_V | read | 4 | 4 | 2 | V | L3 phase voltage |
| grid_L1_A | read | 4 | 6 | 2 | A | L1 current |
| grid_L2_A | read | 4 | 8 | 2 | A | L2 current |
| grid_L3_A | read | 4 | 10 | 2 | A | L3 current |
| grid_L1_VA | read | 4 | 18 | 2 | VA | L1 apparent power |
| grid_L2_VA | read | 4 | 20 | 2 | VA | L2 apparent power |
| grid_L3_VA | read | 4 | 22 | 2 | VA | L3 apparent power |
| grid_L1_VAr | read | 4 | 24 | 2 | VAr | L1 reactive power |
| grid_L2_VAr | read | 4 | 26 | 2 | VAr | L2 reactive power |
| grid_L3_VAr | read | 4 | 28 | 2 | VAr | L3 reactive power |
| grid_L1_PF | read | 4 | 30 | 2 | - | L1 power factor |
| grid_L2_PF | read | 4 | 32 | 2 | - | L2 power factor |
| grid_L3_PF | read | 4 | 34 | 2 | - | L3 power factor |
| grid_L1_PA | read | 4 | 36 | 2 | deg | L1 phase angle |
| grid_L2_PA | read | 4 | 38 | 2 | deg | L2 phase angle |
| grid_L3_PA | read | 4 | 40 | 2 | deg | L3 phase angle |
| grid_LN_V | read | 4 | 42 | 2 | V | average line-to-neutral voltage |
| grid_LL_V | read | 4 | 206 | 2 | V | average line-to-line voltage |
| grid_avg_A | read | 4 | 46 | 2 | A | average current |
| grid_total_A | read | 4 | 48 | 2 | A | total current |
| grid_total_W | read | 4 | 52 | 2 | W | total active power |
| grid_total_VA | read | 4 | 56 | 2 | VA | total apparent power |
| grid_total_VAr | read | 4 | 60 | 2 | VAr | total reactive power |
| grid_total_PF | read | 4 | 62 | 2 | - | total power factor |
| grid_total_PA | read | 4 | 66 | 2 | deg | total phase angle |
| grid_Hz | read | 4 | 70 | 2 | Hz | line frequency |
| grid_L12_V | read | 4 | 200 | 2 | V | L1-L2 voltage |
| grid_L23_V | read | 4 | 202 | 2 | V | L2-L3 voltage |
| grid_L31_V | read | 4 | 204 | 2 | V | L3-L1 voltage |
| grid_LL_avg_THD | read | 4 | 340 | 2 | % | line-to-line average THD |
| grid_LN_avg_THD | read | 4 | 248 | 2 | % | line-to-neutral average THD |
| grid_N_A | read | 4 | 224 | 2 | A | neutral current |
| grid_L1_W | read | 4 | 12 | 2 | W | L1 active power |
| grid_L2_W | read | 4 | 14 | 2 | W | L2 active power |
| grid_L3_W | read | 4 | 16 | 2 | W | L3 active power |

### Unit 6: SRR/TAC/MTR-2

Lettel MCT1-140 (meter), at most 100 registers per request, addresses aligned to 2.

generator-backed local supply meter.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| ats_V | read | 4 | 0 | 2 | V | ATS voltage |
| ats_A | read | 4 | 2 | 2 | A | ATS current |
| ats_W | read | 4 | 4 | 2 | W | ATS active power |
| ats_VAr | read | 4 | 6 | 2 | VAr | ATS reactive power |
| ats_VA | read | 4 | 8 | 2 | VA | ATS apparent power |
| ats_Hz | read | 4 | 12 | 2 | Hz | ATS frequency |

### Unit 11: SRR/TDC/AI-1

Waveshare AI 8CH (io_board), at most 8 registers per request.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| pwr_genBat_V | read | 4 | 0 | 1 | V | generator start battery |
| pwr_upsBat_V | read | 4 | 1 | 1 | V | UPS battery voltage |
| pwr_dc1_V | read | 4 | 2 | 1 | V | DC PS1 bus voltage |
| pwr_dc2_V | read | 4 | 3 | 1 | V | DC PS2 bus voltage |
| irr_out_Lpm | read | 4 | 4 | 1 | Lpm | irrigation output flow |

### Unit 16: SRR/TDC/AO-1

Waveshare AO 8CH (io_board), at most 8 registers per request.

| name | dir | fc | at | n | description |
|---|---|---|---|---|---|
| cli_intr1 | write | 6 | 0 | 1 | intractor 1 speed |
| cli_intr2 | write | 6 | 1 | 1 | intractor 2 speed |

### Unit 21: SRR/CLI/WX-1

Weather station (meter), at most 8 registers per request, addresses aligned to 2.

TODO: address assumed until the unit is chosen.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_wind_mps | read | 4 | 0 | 2 | mps | wind speed at the station |
| cli_windGust_mps | read | 4 | 2 | 2 | mps | peak gust over the station's own window |
| cli_rain_mmph | read | 4 | 4 | 2 | mmph | rainfall rate |
| cli_irrad_Wpm2 | read | 4 | 6 | 2 | Wpm2 | solar irradiance |

### Unit 31: SRR/TDC/RO-1

Waveshare Relay (D) (relay_board), at most 8 coils per request.

| name | dir | at | description |
|---|---|---|---|
| cli_window1_opn | write | coil 0 | window 1 open |
| cli_window1_clo | write | coil 1 | window 1 close |
| irr_input_ro1 | write | coil 2 | irrigation input valve 1 |
| irr_output_ro1 | write | coil 3 | irrigation output valve 1 |
| irr_tray1 | write | coil 4 | tray 1 valve |
| irr_tray2 | write | coil 5 | tray 2 valve |

### Unit 32: SRR/TDC/RO-2

Waveshare Relay (D) (relay_board), at most 8 coils per request.

| name | dir | at | description |
|---|---|---|---|
| cli_window2_opn | write | coil 0 | window 2 open |
| cli_window2_clo | write | coil 1 | window 2 close |
| irr_input_ro2 | write | coil 2 | irrigation input valve 2 |
| irr_output_ro2 | write | coil 3 | irrigation output valve 2 |
| irr_tray3 | write | coil 4 | tray 3 valve |
| irr_tray4 | write | coil 5 | tray 4 valve |

### Unit 33: SRR/TDC/RO-3

Waveshare Relay (D) (relay_board), at most 8 coils per request.

| name | dir | at | description |
|---|---|---|---|
| cli_window3_opn | write | coil 0 | window 3 open |
| cli_window3_clo | write | coil 1 | window 3 close |
| cli_destrat1_speed1 | write | coil 2 | destratification fan 1 speed 1 |
| cli_destrat1_speed2 | write | coil 3 | destratification fan 1 speed 2 |
| cli_destrat1_speed3 | write | coil 4 | destratification fan 1 speed 3 |
| cli_screen | write | coil 5 | thermal screen |
| irr_tray5 | write | coil 6 | tray 5 valve |
| irr_tray6 | write | coil 7 | tray 6 valve |

### Unit 34: SRR/TDC/RO-4

Waveshare Relay (D) (relay_board), at most 8 coils per request.

| name | dir | at | description |
|---|---|---|---|
| cli_window4_opn | write | coil 0 | window 4 open |
| cli_window4_clo | write | coil 1 | window 4 close |
| cli_destrat2_speed1 | write | coil 2 | destratification fan 2 speed 1 |
| cli_destrat2_speed2 | write | coil 3 | destratification fan 2 speed 2 |
| cli_destrat2_speed3 | write | coil 4 | destratification fan 2 speed 3 |
| cli_skirtValve | write | coil 5 | wet skirt valve |
| irr_tray7 | write | coil 6 | tray 7 valve |
| irr_tray8 | write | coil 7 | tray 8 valve |

### Unit 41: SRR/CLI/TRH-EXT-1

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_ext1 | read | 4 | 0 | 2 | C | exterior temperature 1 |
| cli_rh_ext1 | read | 4 | 2 | 2 | % | exterior humidity 1 |

### Unit 42: SRR/CLI/TRH-EXT-2

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_ext2 | read | 4 | 0 | 2 | C | exterior temperature 2 |
| cli_rh_ext2 | read | 4 | 2 | 2 | % | exterior humidity 2 |

### Unit 43: SRR/CLI/TRH-EXT-3

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_ext3 | read | 4 | 0 | 2 | C | exterior temperature 3 |
| cli_rh_ext3 | read | 4 | 2 | 2 | % | exterior humidity 3 |

### Unit 44: SRR/CLI/TRH-CS-1

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_cs1 | read | 4 | 0 | 2 | C | crawlspace temperature 1 |
| cli_rh_cs1 | read | 4 | 2 | 2 | % | crawlspace humidity 1 |

### Unit 45: SRR/CLI/TRH-CS-2

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_cs2 | read | 4 | 0 | 2 | C | crawlspace temperature 2 |
| cli_rh_cs2 | read | 4 | 2 | 2 | % | crawlspace humidity 2 |

### Unit 46: SRR/CLI/TRH-CS-3

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_cs3 | read | 4 | 0 | 2 | C | crawlspace temperature 3 |
| cli_rh_cs3 | read | 4 | 2 | 2 | % | crawlspace humidity 3 |

### Unit 47: SRR/CLI/TRH-LO-1

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_lo1 | read | 4 | 0 | 2 | C | low level temperature 1 |
| cli_rh_lo1 | read | 4 | 2 | 2 | % | low level humidity 1 |

### Unit 48: SRR/CLI/TRH-LO-2

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_lo2 | read | 4 | 0 | 2 | C | low level temperature 2 |
| cli_rh_lo2 | read | 4 | 2 | 2 | % | low level humidity 2 |

### Unit 49: SRR/CLI/TRH-LO-3

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_lo3 | read | 4 | 0 | 2 | C | low level temperature 3 |
| cli_rh_lo3 | read | 4 | 2 | 2 | % | low level humidity 3 |

### Unit 50: SRR/CLI/TRH-HI-1

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_hi1 | read | 4 | 0 | 2 | C | high level temperature 1 |
| cli_rh_hi1 | read | 4 | 2 | 2 | % | high level humidity 1 |

### Unit 51: SRR/CLI/TRH-HI-2

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_hi2 | read | 4 | 0 | 2 | C | high level temperature 2 |
| cli_rh_hi2 | read | 4 | 2 | 2 | % | high level humidity 2 |

### Unit 52: SRR/CLI/TRH-HI-3

Temp/RH sensor (meter), at most 4 registers per request, addresses aligned to 2.

| name | dir | fc | at | n | unit | description |
|---|---|---|---|---|---|---|
| cli_temp_hi3 | read | 4 | 0 | 2 | C | high level temperature 3 |
| cli_rh_hi3 | read | 4 | 2 | 2 | % | high level humidity 3 |

## Modbus TCP register map

What an external master reads. Hand-assigned, and a contract: adding a
row never renumbers the others. Split by KIND rather than sorted into
one run, because an integrator arrives looking for one of the three.

- an **enum** is ONE register carrying its numeric CODE, never its
  label. The codes are the contract: a converter may gain labels, it
  may never renumber them.
- a **text** row is RESERVED, NOT SERVED. A 16-bit register cannot
  hold a hostname, so the slave answers `EX_ILLEGAL_ADDRESS` rather
  than a zero that would read as a value. Set these over the console
  or the shell. The register is kept so the numbering stays stable if
  strings are ever packed onto the wire.

### Parameters

**RW** is served and written; **RO** is served and never written.
`mb_write_enable` is the slave-wide switch above that: off, nothing
is written whatever a row says.

| reg | name | access | unit | description |
|---|---|---|---|---|
| 100 | cli_coldBankOfs_degC | RW | degC | how much cooler before a hot day |
| 101 | cli_heatBankOfs_degC | RW | degC | how much warmer before a cold night |
| 102 | cli_heatBankTrg_degC | RW | degC | yesterday's low below this: bank heat |
| 103 | cli_coldBankTrg_degC | RW | degC | yesterday's high above this: bank cold |
| 104 | cli_coolSet_degC | RW | degC | base cooling target temperature |
| 105 | cli_coolKp | RW | - | cooling PID proportional gain |
| 106 | cli_coolTi_sec | RW | sec | cooling PID integral time |
| 107 | cli_coolTd_sec | RW | sec | cooling PID derivative time |
| 108 | cli_ventTempMgn_degC | RW | degC | intake air must be this much cooler |
| 109 | cli_skirtDryMin_degC | RW | degC | air this dry lets the pad cool |
| 110 | cli_thermalScreenDem | RW | - | cooling effort that deploys the screen |
| 111 | cli_groundHeatDem | RW | - | cooling effort that draws ground air |
| 112 | cli_windowStartDem | RW | - | cooling effort that opens windows |
| 113 | cli_intakeStartDem | RW | - | cooling effort that starts intake fans |
| 114 | cli_windowFullDem | RW | - | cooling effort at fully open windows |
| 115 | cli_skirtCoolDem | RW | - | cooling effort that starts the skirt |
| 116 | cli_fanLowDem | RW | - | cooling effort for fan speed 1 |
| 117 | cli_fanMedDem | RW | - | cooling effort for fan speed 2 |
| 118 | cli_fanHighDem | RW | - | cooling effort for fan speed 3 |
| 119 | cli_intakeFullDem | RW | - | cooling effort at full intake speed |
| 120 | cli_dTStratLow_degC | RW | degC | stratification gap for fan speed 1 |
| 121 | cli_dTStratMed_degC | RW | degC | stratification gap for fan speed 2 |
| 122 | cli_dTStratHigh_degC | RW | degC | stratification gap for fan speed 3 |
| 123 | cli_dTStratHyst_degC | RW | degC | gap change needed to switch fan speed |
| 124 | cli_dehumFull_degC | RW | degC | dryness margin: dehumidify fully |
| 125 | cli_dehumStart_degC | RW | degC | dryness margin: dehumidify starts |
| 126 | cli_humidStart_degC | RW | degC | dryness margin: humidify starts |
| 127 | cli_humidFull_degC | RW | degC | dryness margin: humidify fully |
| 128 | cli_groundSpeed | RW | - | intractor speed while pulling ground heat |
| 130 | cli_ventDryMgn_degC | RW | degC | outside air must be this much drier |
| 131 | cli_tooCold_degC | RW | degC | alarm: too COLD |
| 132 | cli_tooHot_degC | RW | degC | alarm: too HOT |
| 133 | cli_rotMargin_degC | RW | degC | dryness margin: rot risk |
| 134 | cli_intr_rawMin_mV | RW | mV | intractor raw value at zero speed |
| 135 | cli_intr_rawMax_mV | RW | mV | intractor raw value at full speed |
| 136 | cli_intr_engMin | RW | - | intractor speed at the low end |
| 137 | cli_intr_engMax | RW | - | intractor speed at the high end |
| 138 | cli_intr_min | RW | - | lowest believable intractor speed |
| 139 | cli_intr_max | RW | - | highest believable intractor speed |
| 140 | cli_windShutEnd_mps | RW | mps | wind that shuts the roof and is a storm |
| 141 | cli_windowTravel_sec | RW | sec | time to fully open/close a window |
| 142 | cli_windowTmo_sec | RW | sec | safe time to fully open/close a window |
| 143 | cli_screenTravel_sec | RW | sec | time to deploy/remove the screen |
| 144 | cli_screenTmo_sec | RW | sec | safe time to deploy/remove the screen |
| 145 | cli_rainLockout_sec | RW | sec | keep windows shut this long after rain |
| 146 | cli_skirtPeriod_sec | RW | sec | skirt valve modulation period |
| 147 | cli_skirtMinOn_sec | RW | sec | shortest skirt valve pulse |
| 148 | cli_ventHyst_degC | RW | degC | how far a gate has to close before it opens |
| 149 | cli_breezeDem | RW | - | brassage floor during the two breezes |
| 150 | cli_groundDemHyst | RW | - | demand given back before the rung lets go |
| 151 | cli_fanDemHyst | RW | - | demand change needed to switch fan speed |
| 152 | cli_fanIrrDem | RW | - | fan demand while a tray is being watered |
| 153 | cli_rotRelease_sec | RW | s | rot lockout overrun past the condition |
| 154 | cli_targetMin_degC | RW | degC | coldest the target may be clamped to |
| 155 | cli_targetMax_degC | RW | degC | warmest the target may be clamped to |
| 156 | cli_sensorTau_ms | RW | ms | probe filter time constant |
| 157 | irr_flowBand_Lpm | RW | Lpm | outlet flow change worth a status write |
| 158 | irr_flowCutoff_Lpm | RW | Lpm | below this the outlet counts no litres |
| 159 | irr_rateWin_ms | RW | ms | window the inlet rate is differenced over |
| 160 | cli_targetTau_s | RW | s | target temperature filter time constant |
| 161 | cli_windShutBegin_mps | RW | mps | wind that starts capping the opening |
| 162 | cli_windShutMax | RW | - | how far the roof may open in that band |
| 200 | sol_coldBankRise_min | RW | min | cold banking starts, from sunrise |
| 201 | sol_breezeRise_min | RW | min | dawn breeze starts, from sunrise |
| 202 | sol_dayRise_min | RW | min | day operation starts, from sunrise |
| 203 | sol_heatBankSet_min | RW | min | heat banking starts, from sunset |
| 204 | sol_nightSet_min | RW | min | night operation starts, from sunset |
| 205 | sol_duskSet_min | RW | min | dusk breeze starts, from sunset |
| 250 | irr_forceNow | RW | - | water now, then clear this flag |
| 251 | irr_liters_tray1 | RW | L | litres to flood this tray in summer |
| 252 | irr_days_tray1 | RW | days | days between waterings in summer |
| 253 | irr_last_tray1 | RW | days | last watering (days since 1 Jan 1970) |
| 254 | irr_forceOn_tray1 | RW | - | force watering this tray next cycle |
| 255 | irr_forceOff_tray1 | RW | - | force skipping this tray next cycle |
| 256 | irr_liters_tray2 | RW | L | litres to flood this tray in summer |
| 257 | irr_days_tray2 | RW | days | days between waterings in summer |
| 258 | irr_last_tray2 | RW | days | last watering (days since 1 Jan 1970) |
| 259 | irr_forceOn_tray2 | RW | - | force watering this tray next cycle |
| 260 | irr_forceOff_tray2 | RW | - | force skipping this tray next cycle |
| 261 | irr_liters_tray3 | RW | L | litres to flood this tray in summer |
| 262 | irr_days_tray3 | RW | days | days between waterings in summer |
| 263 | irr_last_tray3 | RW | days | last watering (days since 1 Jan 1970) |
| 264 | irr_forceOn_tray3 | RW | - | force watering this tray next cycle |
| 265 | irr_forceOff_tray3 | RW | - | force skipping this tray next cycle |
| 266 | irr_liters_tray4 | RW | L | litres to flood this tray in summer |
| 267 | irr_days_tray4 | RW | days | days between waterings in summer |
| 268 | irr_last_tray4 | RW | days | last watering (days since 1 Jan 1970) |
| 269 | irr_forceOn_tray4 | RW | - | force watering this tray next cycle |
| 270 | irr_forceOff_tray4 | RW | - | force skipping this tray next cycle |
| 271 | irr_liters_tray5 | RW | L | litres to flood this tray in summer |
| 272 | irr_days_tray5 | RW | days | days between waterings in summer |
| 273 | irr_last_tray5 | RW | days | last watering (days since 1 Jan 1970) |
| 274 | irr_forceOn_tray5 | RW | - | force watering this tray next cycle |
| 275 | irr_forceOff_tray5 | RW | - | force skipping this tray next cycle |
| 276 | irr_liters_tray6 | RW | L | litres to flood this tray in summer |
| 277 | irr_days_tray6 | RW | days | days between waterings in summer |
| 278 | irr_last_tray6 | RW | days | last watering (days since 1 Jan 1970) |
| 279 | irr_forceOn_tray6 | RW | - | force watering this tray next cycle |
| 280 | irr_forceOff_tray6 | RW | - | force skipping this tray next cycle |
| 281 | irr_liters_tray7 | RW | L | litres to flood this tray in summer |
| 282 | irr_days_tray7 | RW | days | days between waterings in summer |
| 283 | irr_last_tray7 | RW | days | last watering (days since 1 Jan 1970) |
| 284 | irr_forceOn_tray7 | RW | - | force watering this tray next cycle |
| 285 | irr_forceOff_tray7 | RW | - | force skipping this tray next cycle |
| 286 | irr_liters_tray8 | RW | L | litres to flood this tray in summer |
| 287 | irr_days_tray8 | RW | days | days between waterings in summer |
| 288 | irr_last_tray8 | RW | days | last watering (days since 1 Jan 1970) |
| 289 | irr_forceOn_tray8 | RW | - | force watering this tray next cycle |
| 290 | irr_forceOff_tray8 | RW | - | force skipping this tray next cycle |
| 291 | irr_dryEwmaDays | RW | days | days of dryness history set the dose |
| 292 | irr_dryLo_kPa | RW | kPa | air dryness giving the least water |
| 293 | irr_dryHi_kPa | RW | kPa | air dryness giving the most water |
| 294 | irr_modMin | RW | - | least water given, in humid weather |
| 295 | irr_modGamma | RW | - | how fast watering ramps up with dryness |
| 296 | irr_volFreqSplit | RW | - | cut the volume, or stretch the period |
| 297 | irr_traysPerDay | RW | - | most trays watered in one day |
| 298 | irr_dawnGate_degC | RW | degC | skip pre-dawn watering below this |
| 299 | irr_maxFill_sec | RW | sec | alarm if a tray takes longer to fill |
| 300 | irr_drainWindow_sec | RW | sec | time allowed for a tray to drain |
| 301 | irr_expDrainFrac | RW | - | fraction expected to drain back |
| 302 | irr_drainMargin | RW | - | clog if drained is this far under |
| 303 | irr_flush_sec | RW | sec | how long the manifold is rinsed |
| 304 | irr_input_qtyPerImp | RW | L | litres per pulse from the water meter |
| 305 | irr_out_rawMin_uA | RW | uA | output meter uA at zero flow |
| 306 | irr_out_rawMax_uA | RW | uA | output meter uA at full scale |
| 307 | irr_out_engMin_Lpm | RW | Lpm | output litres/min at zero signal |
| 308 | irr_out_engMax_Lpm | RW | Lpm | output litres/min at full scale |
| 309 | irr_out_min | RW | Lpm | lowest believable output flow |
| 310 | irr_out_max | RW | Lpm | highest believable output flow |
| 311 | irr_out_avg_ms | RW | ms | rolling average for this flow meter |
| 312 | irr_leak_Lpm | RW | Lpm | inlet flow with the cycle idle is a leak |
| 313 | irr_minFill_Lpm | RW | Lpm | a fill under this rate is a pressure fault |
| 314 | irr_morningWin_min | RW | min | how long into the day the morning slot runs |
| 315 | irr_soak_sec | RW | sec | how long a filled tray sits before draining |
| 400 | pwr_dc1_rawMin_mV | RW | mV | DC PS1 raw value at the low end |
| 401 | pwr_dc1_rawMax_mV | RW | mV | DC PS1 raw value at the high end |
| 402 | pwr_dc1_engMin_V | RW | V | DC PS1 volts at the low end |
| 403 | pwr_dc1_engMax_V | RW | V | DC PS1 volts at the high end |
| 404 | pwr_dc1_min | RW | V | lowest believable dc ps1 volts |
| 405 | pwr_dc1_max | RW | V | highest believable dc ps1 volts |
| 406 | pwr_dc1_avg_ms | RW | ms | rolling average for DC PS1 |
| 407 | pwr_dc2_rawMin_mV | RW | mV | DC PS2 raw value at the low end |
| 408 | pwr_dc2_rawMax_mV | RW | mV | DC PS2 raw value at the high end |
| 409 | pwr_dc2_engMin_V | RW | V | DC PS2 volts at the low end |
| 410 | pwr_dc2_engMax_V | RW | V | DC PS2 volts at the high end |
| 411 | pwr_dc2_min | RW | V | lowest believable dc ps2 volts |
| 412 | pwr_dc2_max | RW | V | highest believable dc ps2 volts |
| 413 | pwr_dc2_avg_ms | RW | ms | rolling average for DC PS2 |
| 414 | pwr_upsBat_rawMin_mV | RW | mV | UPS battery raw value at the low end |
| 415 | pwr_upsBat_rawMax_mV | RW | mV | UPS battery raw value at the high end |
| 416 | pwr_upsBat_engMin_V | RW | V | UPS battery volts at the low end |
| 417 | pwr_upsBat_engMax_V | RW | V | UPS battery volts at the high end |
| 418 | pwr_upsBat_min | RW | V | lowest believable ups battery volts |
| 419 | pwr_upsBat_max | RW | V | highest believable ups battery volts |
| 420 | pwr_upsBat_avg_ms | RW | ms | rolling average for the UPS battery |
| 421 | pwr_genBat_rawMin_mV | RW | mV | gen. battery raw value at the low end |
| 422 | pwr_genBat_rawMax_mV | RW | mV | gen. battery raw value at the high end |
| 423 | pwr_genBat_engMin_V | RW | V | gen. battery volts at the low end |
| 424 | pwr_genBat_engMax_V | RW | V | gen. battery volts at the high end |
| 425 | pwr_genBat_min | RW | V | lowest believable gen. battery volts |
| 426 | pwr_genBat_max | RW | V | highest believable gen. battery volts |
| 427 | pwr_genBat_avg_ms | RW | ms | rolling average for the Gen. battery |
| 428 | pwr_gridLo_V | RW | V | grid volts: low alarm |
| 429 | pwr_gridHi_V | RW | V | grid volts: high alarm |
| 430 | pwr_gridLo_Hz | RW | Hz | grid frequency: low alarm |
| 431 | pwr_gridHi_Hz | RW | Hz | grid frequency: high alarm |
| 432 | pwr_localLo_V | RW | V | local AC volts: low alarm |
| 433 | pwr_localHi_V | RW | V | local AC volts: high alarm |
| 434 | pwr_localLo_Hz | RW | Hz | local AC frequency: low alarm |
| 435 | pwr_localHi_Hz | RW | Hz | local AC frequency: high alarm |
| 436 | pwr_dc1Lo_V | RW | V | DC PS1 volts: low alarm |
| 437 | pwr_dc2Lo_V | RW | V | DC PS2 volts: low alarm |
| 438 | pwr_upsBatLo_V | RW | V | UPS battery volts: low alarm |
| 439 | pwr_genBatLo_V | RW | V | gen. battery volts: low alarm |
| 440 | pwr_genStart_sec | RW | sec | the generator has this long to take over |
| 900 | console_baud | RW | Bd | console UART rate |
| 901 | monitor_baud | RW | Bd | monitor UART rate |
| 905 | monitor_level | RW | - | lowest level the monitor port carries |
| 910 | net_serial | RW | - | bump to apply staged net_ edits |
| 911 | net_dhcp | RW | - | enable DHCP IP configuration |
| 912 | net_ip | reserved | - | static IP address |
| 913 | net_mask | reserved | - | static IP netmask |
| 914 | net_gw | reserved | - | static IP gateway |
| 915 | net_dns1 | reserved | - | static DNS resolver 1 |
| 916 | net_dns2 | reserved | - | static DNS resolver 2 |
| 917 | net_enable | RW | - | bring the network up |
| 920 | ntp_serial | RW | - | bump to apply staged ntp_ edits |
| 921 | ntp_enable | RW | - | enable NTP clock sync |
| 922 | ntp_poll_sec | RW | sec | NTP poll interval, in seconds |
| 923 | ntp_port | RW | - | NTP server port |
| 924 | ntp_srv1 | reserved | - | NTP server 1 |
| 925 | ntp_srv2 | reserved | - | NTP server 2 |
| 930 | mqtt_serial | RW | - | bump to apply staged mqtt_ edits |
| 931 | mqtt_enable | RW | - | enable MQTT |
| 932 | mqtt_host | reserved | - | MQTT broker host |
| 933 | mqtt_port | RW | - | MQTT broker port |
| 934 | mqtt_ka_sec | RW | sec | MQTT keep-alive interval |
| 935 | mqtt_tlm_sec | RW | sec | telemetry publish cadence |
| 936 | mqtt_retain | RW | - | publish telemetry retained |
| 937 | mqtt_level | RW | - | lowest level published to the broker |
| 940 | geo_serial | RW | - | bump to apply staged geo_ edits |
| 941 | geo_lat_deg | RW | deg | geographical latitude, in degrees |
| 942 | geo_lon_deg | RW | deg | geographical longitude, in degrees |
| 950 | mb_serial | RW | - | bump to apply staged mb_ edits |
| 951 | mb_enable | RW | - | serve the Modbus TCP slave |
| 952 | mb_port | RW | - | Modbus TCP slave port |
| 953 | mb_write_enable | RW | - | let a master write parameters |
| 960 | console_serial | RW | - | bump to apply staged console_ edits |
| 961 | console_type | RW | - | which terminal is on the console |
| 962 | console_cols | RW | col | console width, in columns |
| 963 | console_rows | RW | row | console height, in rows |
| 964 | console_home_level | RW | - | lowest level HOME's log tail shows |
| 965 | console_shell_level | RW | - | lowest level the SHELL page interleaves |
| 966 | console_logs_level | RW | - | lowest level the LOGS page keeps |
| 970 | sys_cpu_load_pct | RW | % | CPU_LOAD alarm threshold |
| 971 | sys_jitter_pct | RW | % | CPU_JITT alarm threshold |
| 972 | sys_stack_pct | RW | % | STACK alarm threshold |
| 973 | sys_log_level | RW | - | lowest level the logger forwards |
| 974 | sys_retain_sec | RW | sec | how often retained values reach flash |
| 980 | rtu_timeout_ms | RW | ms | how long a slave has to answer |
| 981 | rtu_retries | RW | - | immediate retries before a device is backed off |
| 982 | rtu_backoff_ms | RW | ms | first backoff after a device fails its retries |
| 983 | rtu_backoff_max_ms | RW | ms | longest a backed-off device waits between probes |
| 984 | rtu_write_min_ms | RW | ms | shortest interval between writes to one block |
| 985 | rtu_rewrite_ms | RW | ms | how often an output block is written anyway |

### Status values

Read-only: a master may not write a reading. An `SVE` is one
register carrying its numeric code.

| reg | name | unit | description |
|---|---|---|---|
| 500 | sys_supply_lockout | - | supply unhealthy: actuators held off |
| 501 | cli_temp_in | degC | interior temperature: the hotter of the two levels |
| 502 | cli_temp_hi | degC | ceiling temperature, voted and filtered |
| 503 | cli_rh_hi | - | ceiling relative humidity, voted and filtered |
| 504 | cli_temp_lo | degC | floor temperature, voted and filtered |
| 505 | cli_rh_lo | - | floor relative humidity, voted and filtered |
| 506 | cli_temp_cs | degC | crawlspace temperature, voted and filtered |
| 507 | cli_rh_cs | - | crawlspace relative humidity, voted and filtered |
| 508 | cli_temp_ext | degC | exterior temperature, voted and filtered |
| 509 | cli_rh_ext | - | exterior relative humidity, voted and filtered |
| 510 | cli_dTStrat_degC | degC | stratification: ceiling above floor |
| 511 | cli_dewPoint_degC | degC | interior dew point (Magnus) |
| 512 | cli_dpMargin_degC | degC | interior dew-point margin: how far from condensing |
| 513 | cli_satPress_kPa | kPa | interior saturation vapour pressure (Tetens) |
| 514 | cli_vpd_kPa | kPa | interior vapour-pressure deficit |
| 515 | cli_targetTemp_degC | degC | cooling target in force for the current phase |
| 516 | cli_prevDayMax_degC | degC | highest interior temperature of the previous day |
| 517 | cli_prevNightMin_degC | degC | lowest interior temperature of the previous night |
| 518 | cli_phase | - | phase of the day in force |
| 519 | cli_coolDem | - | cooling demand, -1 warming to +1 cooling |
| 520 | cli_stratDem | - | destratification demand, 0 to 1 |
| 521 | cli_humidDem | - | humidity demand, -1 dehumidify to +1 humidify |
| 522 | cli_coldBankArm | - | cold banking armed for the coming dawn |
| 523 | cli_heatBankArm | - | heat banking armed for the coming dusk |
| 524 | sol_fromRise_min | min | minutes since sunrise; negative before it |
| 525 | sol_fromSet_min | min | minutes since sunset; negative before it |
| 526 | irr_inFlow_Lpm | Lpm | inlet flow, differentiated from the counter |
| 527 | irr_inTotal_L | L | inlet water counter, since it was last zeroed |
| 528 | irr_outTotal_L | L | outlet water counter, since it was last zeroed |
| 529 | cli_windowDem | - | how far the cooling ladder wants the windows open |
| 530 | irr_vpdAvg_kPa | kPa | dryness tracker: VPD averaged over irr_dryEwmaDays |
| 531 | irr_mod | - | irrigation modulation, irr_modMin (wet) to 1 (summer dry) |
| 532 | irr_period_days | days | modulated interval, for the tray due next |
| 533 | irr_volume_L | L | modulated dose, for the tray being served |
| 534 | irr_activeTray | - | tray being served, 0 when none |
| 535 | irr_servedToday | - | trays served since the day rolled over |
| 536 | irr_delivered_L | L | delivered into the tray being filled |
| 537 | irr_state | - | what the irrigation cycle is doing |
| 538 | irr_drained_L | L | drained back out of the tray being served |
| 539 | irr_outFlow_Lpm | Lpm | outlet flow, averaged over irr_out_avg_ms |
| 540 | cli_window1_tgt | - | window 1 commanded position, 0 to 1 |
| 541 | cli_window1_pos | - | window 1 position, 0 to 1 |
| 542 | cli_window2_tgt | - | window 2 commanded position, 0 to 1 |
| 543 | cli_window2_pos | - | window 2 position, 0 to 1 |
| 544 | cli_window3_tgt | - | window 3 commanded position, 0 to 1 |
| 545 | cli_window3_pos | - | window 3 position, 0 to 1 |
| 546 | cli_window4_tgt | - | window 4 commanded position, 0 to 1 |
| 547 | cli_window4_pos | - | window 4 position, 0 to 1 |
| 548 | cli_rainLockout | - | rain holds the windows shut |
| 549 | cli_windLockout | - | wind holds the windows shut |
| 550 | cli_screenMoving | - | thermal screen travelling |
| 551 | cli_rotLockout | - | rot risk: nothing may humidify |
| 552 | cli_rotVentDem | - | anti-rot ventilation demand, 0 to 1 |
| 553 | cli_skirtDuty | - | skirt valve on-time, 0 to 1 |
| 554 | sys_grid_V | V | grid line-to-neutral volts |
| 555 | sys_grid_A | A | grid total current |
| 556 | sys_grid_W | W | grid active power |
| 557 | sys_grid_Hz | Hz | grid frequency |
| 558 | sys_grid_ok | - | grid supply within its bands |
| 559 | sys_local_V | V | local AC line-to-neutral volts |
| 560 | sys_local_A | A | local AC total current |
| 561 | sys_local_W | W | local AC active power |
| 562 | sys_local_Hz | Hz | local AC frequency |
| 563 | sys_local_ok | - | local AC supply within its bands |
| 564 | sys_dc1_V | V | DC PS1 volts |
| 565 | sys_dc2_V | V | DC PS2 volts |
| 566 | sys_upsBat_V | V | UPS battery volts |
| 567 | sys_genBat_V | V | generator start battery volts |
| 568 | sys_dc_ok | - | every DC rail above its floor, and the UPS off its low-battery contact |
| 569 | cli_fanSpeed | - | destratification fan speed in force, 0 to 3 |
| 570 | prog_ac_grid_watch | - | health of the ac_grid_watch program |
| 571 | prog_ac_local_watch | - | health of the ac_local_watch program |
| 572 | prog_dc_watch | - | health of the dc_watch program |
| 573 | prog_temp_rh_zone | - | health of the temp_rh_zone program |
| 574 | prog_strat | - | health of the strat program |
| 575 | prog_climate_watch | - | health of the climate_watch program |
| 576 | prog_day_phase | - | health of the day_phase program |
| 577 | prog_target_temp | - | health of the target_temp program |
| 578 | prog_cooling | - | health of the cooling program |
| 579 | prog_destrat | - | health of the destrat program |
| 580 | prog_humidity | - | health of the humidity program |
| 581 | prog_irrigation | - | health of the irrigation program |
| 582 | prog_screen | - | health of the screen program |
| 583 | prog_windows | - | health of the windows program |
| 584 | prog_weather_watch | - | health of the weather_watch program |
| 585 | prog_intractors | - | health of the intractors program |
| 586 | prog_fans | - | health of the fans program |
| 587 | prog_skirt | - | health of the skirt program |
| 588 | cli_intakeCoolOk | - | intake air is cool enough to be worth drawing |
| 589 | cli_groundHeatOk | - | intake air is warm enough to be worth drawing |
| 590 | cli_intakeDryOk | - | outside air is dry enough to be worth drawing |
| 591 | prog_irr_modulation | - | health of the irrigation modulation program |
| 592 | prog_irr_mtr_input | - | health of the inlet meter program |
| 593 | sol_elev_deg | deg | the sun's elevation above the horizon |
| 594 | prog_sun_watch | - | health of the sun watch program |
| 595 | cli_sinceRain_min | min | minutes since the gauge last read wet |
| 596 | cli_windowLockout | - | the roof is held shut, whatever the ladder wants |
| 597 | irr_today | days | the day the schedule is running against, 0 before the clock is set |
| 598 | irr_dueTray | - | tray owed water now, 0 when none is |
| 599 | irr_slotOpen | - | the climate allows a cycle to start |
| 600 | prog_irr_schedule | - | health of the irrigation schedule |
| 601 | prog_roof_lockout | - | health of the roof lockout program |
| 602 | cli_windowCap | - | the most the roof may open |
| 603 | prog_temp_rh_sensors | - | health of the temp_rh_sensors program |
| 604 | prog_irr_mtr_output | - | health of the outlet meter program |
| 605 | prog_irr_valves | - | health of the irrigation valves program |
| 902 | sys_fieldbus_dead | - | field devices not answering |
| 903 | sys_fieldbus_crc | - | frames that failed their checksum |
| 904 | sys_fieldbus_stray | - | bytes that arrived out of turn |

### Alarms

Read-only as registers, and each has an ACK coil beside it - the one
thing a master may write on this side of the map.

| reg | name | code | description |
|---|---|---|---|
| 800 | alarm_ac_grid | AC_GRID | grid AC out-of-spec/offline |
| 801 | alarm_dc_ps1 | DC_PS1 | DC PS1 volts too low |
| 802 | alarm_gen | GEN_FLT | generator failed to start |
| 803 | alarm_ac_local | AC_LOCAL | local AC out of spec or offline |
| 804 | alarm_dc_ps2 | DC_PS2 | DC PS2 volts too low |
| 805 | alarm_dc_ups | DC_UPS | UPS low battery: plant held |
| 807 | alarm_generator_battery | GEN_BATT | gen. start battery low |
| 812 | alarm_sensor_fault | SNS_FLT | sensor fault |
| 813 | alarm_too_hot | TOO_HOT | inside temperature too high |
| 814 | alarm_too_cold | TOO_COLD | inside temperature too low |
| 815 | alarm_too_humid | TOO_HUM | too humid: the ladder is at full |
| 816 | alarm_strat | STRAT | temp. stratification too high |
| 817 | alarm_water_leak | WTR_LK | leak in irrigation circuit |
| 818 | alarm_water_pressure | WTR_BAR | water pressure too low |
| 819 | alarm_irr_drain | DRAIN_FLT | a tray would not drain in time |
| 822 | alarm_weather_storm | WH_STORM | current storm conditions |
| 826 | alarm_sns_ceiling | SNS_CEIL | ceiling temp/RH probe quorum lost |
| 827 | alarm_sns_floor | SNS_FLR | floor temp/RH probe quorum lost |
| 828 | alarm_sns_crawlspace | SNS_CS | crawlspace temp/RH probe quorum lost |
| 829 | alarm_sns_exterior | SNS_EXT | exterior temp/RH probe quorum lost |
| 830 | alarm_rot | ROT | dew-point margin low: rot risk |
| 831 | alarm_wx_station | WX_FLT | weather station not answering |
| 832 | alarm_irr_fill | FILL_FLT | a tray would not fill in time |
| 833 | alarm_irr_fill_tray1 | FILL1 | tray 1 would not fill in time |
| 834 | alarm_irr_fill_tray2 | FILL2 | tray 2 would not fill in time |
| 835 | alarm_irr_fill_tray3 | FILL3 | tray 3 would not fill in time |
| 836 | alarm_irr_fill_tray4 | FILL4 | tray 4 would not fill in time |
| 837 | alarm_irr_fill_tray5 | FILL5 | tray 5 would not fill in time |
| 838 | alarm_irr_fill_tray6 | FILL6 | tray 6 would not fill in time |
| 839 | alarm_irr_fill_tray7 | FILL7 | tray 7 would not fill in time |
| 840 | alarm_irr_fill_tray8 | FILL8 | tray 8 would not fill in time |
| 841 | alarm_irr_drain_tray1 | DRAIN1 | tray 1 would not drain in time |
| 842 | alarm_irr_drain_tray2 | DRAIN2 | tray 2 would not drain in time |
| 843 | alarm_irr_drain_tray3 | DRAIN3 | tray 3 would not drain in time |
| 844 | alarm_irr_drain_tray4 | DRAIN4 | tray 4 would not drain in time |
| 845 | alarm_irr_drain_tray5 | DRAIN5 | tray 5 would not drain in time |
| 846 | alarm_irr_drain_tray6 | DRAIN6 | tray 6 would not drain in time |
| 847 | alarm_irr_drain_tray7 | DRAIN7 | tray 7 would not drain in time |
| 848 | alarm_irr_drain_tray8 | DRAIN8 | tray 8 would not drain in time |
| 986 | alarm_net_fault | NET_FLT | the network link is down |
| 987 | alarm_ntp_timeout | NTP_TIM | no NTP sync |
| 988 | alarm_log_flood | LOG_FLD | log messages are being dropped |
| 990 | alarm_system_reset | SYS_RST | unit reset recently |
| 991 | alarm_cpu_xrun | CPU_XRUN | scheduler overrun (skipped beat) |
| 992 | alarm_cpu_load | CPU_LOAD | CPU load over threshold |
| 993 | alarm_stack | STACK | stack usage over threshold |
| 994 | alarm_cpu_jitter | CPU_JITT | CPU jitter over threshold |
| 995 | alarm_flash_error | FLASH_FLT | persistent storage write failed |
| 996 | alarm_fieldbus | BUS_DEAD | a field device is not answering |
| 997 | alarm_scan_late | SCAN_LATE | a control program is missing its period |
| 998 | alarm_fieldbus_line | BUS_LINE | the whole segment has gone quiet |
| 999 | alarm_io_drop | IO_DROP | a field row is never scanned |

### Enum rows

Each is one register carrying a numeric CODE, never its label.
The vocabulary each is written over is below; a row whose
vocabulary is the library's names that instead, and its codes
come from the library's own header.

| reg | name | vocabulary | notes |
|---|---|---|---|
| 905 | monitor_level | `Logs::level_label` | open by default: this port exists to be the full record |
| 937 | mqtt_level | `Logs::level_label` | every entry costs a publish, so this is the floor to raise first |
| 964 | console_home_level | `Logs::level_label` | a landing screen: only what is worth looking up for |
| 965 | console_shell_level | `Logs::level_label` | `logs off` silences it entirely without moving this |
| 966 | console_logs_level | `Logs::level_label` | this page is the record; raising it loses the record |
| 973 | sys_log_level | `Logs::level_label` | the floor for EVERY sink; each page filters further |
| 518 | cli_phase | `DayPhase` | NIGHT COLD_BANK DAWN_BREEZE DAY HEAT_BANK DUSK_BREEZE |
| 537 | irr_state | `IrrState` | IDLE FILLING DRAINING FLUSHING |
| 570 | prog_ac_grid_watch | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 571 | prog_ac_local_watch | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 572 | prog_dc_watch | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 573 | prog_temp_rh_zone | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 574 | prog_strat | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 575 | prog_climate_watch | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 576 | prog_day_phase | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 577 | prog_target_temp | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 578 | prog_cooling | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 579 | prog_destrat | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 580 | prog_humidity | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 581 | prog_irrigation | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 582 | prog_screen | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 583 | prog_windows | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 584 | prog_weather_watch | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 585 | prog_intractors | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 586 | prog_fans | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 587 | prog_skirt | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 591 | prog_irr_modulation | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 592 | prog_irr_mtr_input | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 594 | prog_sun_watch | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 600 | prog_irr_schedule | `Plc::health_label` | - |
| 601 | prog_roof_lockout | `Plc::health_label` | - |
| 603 | prog_temp_rh_sensors | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 604 | prog_irr_mtr_output | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |
| 605 | prog_irr_valves | `Plc::health_label` | 3 is INHIBITED: nothing has run yet |

#### DayPhase

The code is the CONTRACT: a vocabulary may gain members and
must never renumber them. The label is what an operator
reads and types, and may change freely.

| code | label | C++ | meaning |
|---|---|---|---|
| 0 | NIGHT | `PHASE_NIGHT` | dark; the target is the night setting |
| 1 | COLD_BANK | `PHASE_COLD_BANK` | pre-dawn pre-cool after a hot day |
| 2 | DAWN_BREEZE | `PHASE_DAWN_BREEZE` | first air of the morning |
| 3 | DAY | `PHASE_DAY` | full daylight operation |
| 4 | HEAT_BANK | `PHASE_HEAT_BANK` | dusk heat store after a cold night |
| 5 | DUSK_BREEZE | `PHASE_DUSK_BREEZE` | last air before the night closes |

#### IrrState

The code is the CONTRACT: a vocabulary may gain members and
must never renumber them. The label is what an operator
reads and types, and may change freely.

| code | label | C++ | meaning |
|---|---|---|---|
| 0 | IDLE | `IRR_IDLE` | no tray is being served |
| 1 | FILLING | `IRR_FILLING` | inlet open; tray open; outlet shut |
| 2 | DRAINING | `IRR_DRAINING` | inlet shut; tray open; outlet open |
| 3 | FLUSHING | `IRR_FLUSHING` | inlet and drain open, no tray: the manifold rinsed |
| 4 | SOAKING | `IRR_SOAKING` | every valve shut: the tray sits full for irr_soak_sec |

#### `Logs::level_label`

A log severity. BIT VALUES, not a run: the gaps are what lets a sink hold a mask of levels, and a code that is not one of these is REFUSED rather than shown.

| code | label | C++ | meaning |
|---|---|---|---|
| 1 | debug | `LOG_DEBUG` | everything, including the per-beat detail |
| 2 | info | `LOG_INFO` | what the plant did |
| 4 | notice | `LOG_NOTICE` | worth looking up for |
| 8 | warning | `LOG_WARNING` | wrong, and still running |
| 16 | error | `LOG_ERROR` | wrong, and something stopped |
| 32 | critical | `LOG_CRITICAL` | undroppable: the rate limiter never sheds it |

#### `Plc::health_label`

What a control program could do on its last activation. The first three are a SEVERITY RUN and are ordered, so a supervisor takes the worst across the programs in one comparison. INHIBITED is off that axis: its control has stopped and its actuators are parked, but it is still reporting.

| code | label | C++ | meaning |
|---|---|---|---|
| 0 | HEALTHY | `Health::HEALTHY` | normal operation; every output valid |
| 1 | DEGRADED | `Health::DEGRADED` | an input is invalid; the outputs are still good |
| 2 | FAULTED | `Health::FAULTED` | at least one output is invalid |
| 3 | INHIBITED | `Health::INHIBITED` | control stopped and actuators parked; still reporting |

## MQTT telemetry

217 rows are published: everything except a parameter
and an alarm, which each have a page of their own.

