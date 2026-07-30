#ifndef SITE_BOTANIK_H
#define SITE_BOTANIK_H

/**
 * @file botanik.h
 * @brief GENERATED - the one file that knows where libbotanik sits relative to
 *        this site directory.
 *
 * Every other file in this directory includes only `"botanik.h"` and its own
 * siblings, so the site directory is relocatable: moving it means regenerating
 * this one file, nothing else.
 *
 * Why relative and not an include path: the Arduino PLC IDE adds no include
 * directory inside the sketch folder (neither the sketch root nor its src/), so
 * a vendored copy of the library can only be found relative to the including
 * file. The paths below assume the standard layout, where the site sits two
 * levels under the tree that holds libbotanik:
 *
 *     <root>/libbotanik.h
 *     <root>/libbotanik/...
 *     <root>/sites/<name>/     <- this directory
 *
 * That layout holds both in the library tree and in a vendored <sketch>/src/,
 * so the same generated site compiles in either. If you place the site
 * elsewhere, this file is the only thing to change.
 */

#include "../../libbotanik.h"                    // the umbrella public API
#include "../../libbotanik/core/firmware.h"      // BotanikFirmware + SiteDescriptor
#include "../../libbotanik/core/params.h"        // Params::Param / ParamMeta
#include "../../libbotanik/core/scalar.h"        // ScalarField / ScalarGroup / HomeField
#include "../../libbotanik/alarms/alarms.h"      // Alarms::AlarmCondition
#include "../../libbotanik/shell/shell.h"        // Shell::ShellCommand

#endif // SITE_BOTANIK_H
