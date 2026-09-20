#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

/** @file libbotanik_config.h
 *  @brief Global configuration parameters for the Botanik library.
 *
 *  This is the AGGREGATE: it includes every `libbotanik/config/<concern>.gen.h`
 *  and is what a site includes to get the lot. Library code should include only
 *  the concern it reads (`config/net.gen.h`, `config/terminal.gen.h`, ...) -
 *  editing the MQTT keep-alive has no business recompiling the PID block.
 *
 *  @note Every parameter is overridable per site. The site selected by the build
 *        tools may ship `examples/<name>/config.h` and `#define` overrides. The
 *        path of the selected site is passed by CMake in BOTANIK_SITE_CONFIG,
 *        and `config/base.h` pulls it in ahead of every default, so precedence
 *        is: command line > site config > the `#ifndef` default here.
 *
 *  @note What belongs here is what an INSTALLATION varies: a baud rate, a
 *        threshold, the size of a table a site fills. A number the code, a data
 *        type or a protocol already fixes belongs beside its one consumer as a
 *        `static constexpr`, where its constraint reads in the same breath.
 *
 *  @note The concern headers are GENERATED from
 *        `libbotanik/tables/config/<concern>.csv`. Adding a parameter is a row
 *        in the one that owns it; adding a CONCERN is a new table, an include
 *        here and a row in docs/integration/config.md. */

#include "libbotanik/config/base.h"

#include "libbotanik/config/alarms.gen.h"
#include "libbotanik/config/identity.gen.h"
#include "libbotanik/config/logging.gen.h"
#include "libbotanik/config/modbus.gen.h"
#include "libbotanik/config/net.gen.h"
#include "libbotanik/config/notes.gen.h"
#include "libbotanik/config/scheduler.gen.h"
#include "libbotanik/config/serial.gen.h"
#include "libbotanik/config/simulator.gen.h"
#include "libbotanik/config/sun.gen.h"
#include "libbotanik/config/terminal.gen.h"
#include "libbotanik/config/vars.gen.h"
#endif // LIBBOTANIK_CONFIG_H
