#ifndef SITE_DESCRIPTOR_H
#define SITE_DESCRIPTOR_H

/**
 * @file descriptor.h
 * @brief GENERATED - the one SiteDescriptor bundling every gh_mlb41 table.
 *
 * Generated from the Arduino PLC IDE project. Regenerating overwrites this
 * file.
 *
 * This is the whole handoff to the library: the firmware base reads this
 * descriptor to build and wire every page (home / shell / logs / alarms /
 * params / monitor) and every backend generically. There is no per-field code
 * and no per-site page class.
 *
 * Every include below is RELATIVE, so this site's files always find each other
 * whether the directory sits in the library tree or in a consumer's project.
 * Each table is a borrowed pointer to a site static, which outlives the
 * firmware.
 */

#include <cstdint>

#include "botanik.h"

#include "payload.h"      // the PLC payload every table addresses by offset
#include "status.h"       // status variables + home-screen readings
#include "params.h"       // config parameters
#include "alarms.h"       // alarm conditions
#include "commands.h"     // shell commands (hand-written)

namespace Site {

/** @brief The gh_mlb41 site: name + the generated tables. Field order matches
 *  Botanik::SiteDescriptor. Any table may be null / count 0. */
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_params,        g_param_count,          // params page + `param` command
    g_commands,      g_command_count,        // site shell commands
    g_status_groups, g_status_group_count,   // monitor page, telemetry, `status`
    g_home_fields,   g_home_field_count,     // home-screen readings
    g_alarms,        g_alarm_count,          // alarm conditions
};

}  // namespace Site

#endif // SITE_DESCRIPTOR_H
