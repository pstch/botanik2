#ifndef SITE_TASKS_H
#define SITE_TASKS_H

/**
 * @file tasks.h
 * @brief HAND-WRITTEN - the gh_mlb41 control programs, in four files.
 *
 * Not generated: this is the one place a site expresses *behaviour* rather than
 * schema. The rows they read and drive are in `tables/`; the maths they are
 * built from is generic (`plc/functions/`, `plc/blocks/`).
 *
 * A program holds its function blocks as members and its variable references in
 * `_v`, the `Refs` struct `generated/programs.gen.h` builds from
 * `tables/programs.toml`, and reads and drives them through those references:
 * `_v.x.get()`, `_v.y.set(v)`. Four rules the scaffolding guarantees, and which
 * every program here relies on:
 *
 *  - every program in a beat sees the same instant (inputs refresh once, before
 *    any of them runs)
 *  - @c dt_ms is the REAL time since this program last ran, so a timer keeps
 *    correct time even when the scan was late
 *  - nothing runs at all while control authority is withheld, and the first
 *    activation after it returns says so (@c c.first_scan), which is where a
 *    program drops whatever the outage made stale
 *  - an input that has stopped being believable is reported DEGRADED by the
 *    base, from the interface `programs.toml` declares, so no law has to
 *    remember to ask
 *
 * ## How they reach each other
 *
 * NEVER directly. No program holds a reference to another, or to an instrument
 * another owns: they meet in the variable table and nowhere else, which is what
 * lets any of them be constructed with `(setup_context(), io())` alone and read
 * on the PARAMS and MONITOR pages while it runs.
 *
 * Order of declaration is order of execution, and the flow is downhill:
 *
 *     INSTRUMENTS (tasks_sense.h)
 *       ac_grid_watch, ac_local_watch, dc_watch ─> sys_*, the supply alarms
 *       sun_watch      ─> sol_fromRise_min, sol_fromSet_min, sol_elev_deg
 *       temp_rh_zone   ─> cli_temp_in/hi/lo/cs/ext, cli_rh_*,
 *                         cli_dpMargin_degC, cli_vpd_kPa, alarm_rot
 *       strat          ─> cli_dTStrat_degC
 *       climate_watch, weather_watch ─> the alarms only
 *
 *     LADDERS (tasks_climate.h, tasks_irrigation.h)
 *       day_phase      ─> cli_phase, the two bank arms
 *       target_temp    ─> cli_targetTemp_degC
 *       cooling        ─> cli_coolDem, cli_windowDem,
 *                         cli_intakeCoolOk, cli_groundHeatOk
 *       destrat        ─> cli_stratDem
 *       humidity       ─> cli_humidDem, cli_rotVentDem, cli_rotLockout,
 *                         cli_intakeDryOk
 *       irr_modulation ─> irr_mod
 *       irr_meters     ─> irr_inFlow/inTotal, irr_outFlow/outTotal
 *
 *     THE PLANT (tasks_irrigation.h, tasks_actuators.h)
 *       irrigation, screen, windows, intractors, fans, skirt
 *
 * One thing does NOT travel through a row: the supply lockout. `dc_watch`
 * publishes `sys_supply_lockout` for the page and the bus, and raises the
 * registry's plant-wide safe hold beside it, so every program parks through the
 * machinery a halt already uses instead of each one testing the row. It is
 * released on the same edge, so the plant comes back when the battery does.
 * The roof is the exception: a supply lockout SHUTS it, which is stage-1
 * interlock and not a safe state, so `windows` still reads the row.
 *
 * A law reading a demand computed in the same beat needs its producer to have
 * run first, which is the whole reason the order is fixed. See docs/dev/scan.md
 * for the cycle, and docs/design/control-system.md for what each ladder means.
 */

#include "programs/actuators/skirt_controller/skirt_controller.h"
#include "programs/irrigation/water_meters/water_meters.h"

#include "tasks_sense.h"
#include "tasks_climate.h"
#include "tasks_irrigation.h"
#include "tasks_actuators.h"

#endif // SITE_TASKS_H
