#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

/** @file libbotanik_config.h
 *  @brief Global configuration parameters for the Botanik library.
 *
 *  This is the AGGREGATE: it includes every `libbotanik/config/<concern>.h` and
 *  is what a site includes to get the lot. Library code should include only the
 *  concern it reads (`config/net.h`, `config/terminal.h`, ...) - editing the
 *  MQTT keep-alive has no business recompiling the PID block.
 *
 *  @note Every parameter is overridable per site. The site selected by the build
 *        tools may ship `src/sites/<name>/config.h` and `#define` overrides. The
 *        path of the selected site is passed by CMake in BOTANIK_SITE_CONFIG,
 *        and `config/base.h` pulls it in ahead of every default, so precedence
 *        is: command line > site config > the `#ifndef` default here.
 *
 *  @note What belongs here is what an INSTALLATION varies: a baud rate, a
 *        threshold, the size of a table a site fills. A number the code, a data
 *        type or a protocol already fixes belongs beside its one consumer as a
 *        `static constexpr`, where its constraint reads in the same breath.
 *
 *  @note Adding a parameter is an `#ifndef` default plus a BOTANIK_CFG line in
 *        the one concern header it belongs to. Adding a CONCERN is a new file
 *        here and a row in docs/integration/config.md. */

#include "libbotanik/config/base.h"

#include "libbotanik/config/alarms.h"
#include "libbotanik/config/identity.h"
#include "libbotanik/config/logging.h"
#include "libbotanik/config/modbus.h"
#include "libbotanik/config/net.h"
#include "libbotanik/config/notes.h"
#include "libbotanik/config/scheduler.h"
#include "libbotanik/config/serial.h"
#include "libbotanik/config/simulator.h"
#include "libbotanik/config/sun.h"
#include "libbotanik/config/terminal.h"
#include "libbotanik/config/vars.h"
#endif // LIBBOTANIK_CONFIG_H
