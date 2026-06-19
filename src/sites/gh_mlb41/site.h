#ifndef SITES_GH_MLB41_SITE_H
#define SITES_GH_MLB41_SITE_H

/**
 * @file site.h
 * @brief gh_mlb41 site entry - the uniform handle a target harness builds against.
 *
 * A target selects a site with -DBOTANIK_SITE=<name>; CMake then points
 * BOTANIK_SITE_HEADER at this file, so the simulator harness and the OPTA firmware
 * include it without naming the site. Each site exposes the same surface:
 *   - `BotanikSite`      - the firmware type to instantiate (a BotanikFirmware<>).
 *   - `PlcInput/OutputData_t` - the site payload (from greenhouse.h / payload.h).
 *   - `start_site_emulator()` / `plc_dilated_timeout()` - the sim-only mock PLC
 *     (declarations always visible; definitions compile only under TARGET_SIMULATOR).
 */

#include "sites/gh_mlb41/greenhouse.h"
#include "sites/gh_mlb41/sim.h"

/** @brief The firmware type the harness instantiates for this site. */
using BotanikSite = GreenhouseMLB41;

#endif // SITES_GH_MLB41_SITE_H
