// REVIEW_35: DONE
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
 *        threshold, the size of a table a site fills. What does not is a number
 *        the code, a data type or a protocol already fixes - that is a
 *        `static constexpr` beside its one consumer, where the thing that
 *        constrains it can be read in the same breath.
 *
 *  @note Adding a parameter is an `#ifndef` default plus a BOTANIK_CFG line in
 *        the one concern header it belongs to. Adding a CONCERN is a new file
 *        here and a row in docs/integration/config.md. */

#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

#include "libbotanik/config/alarms.h"     // Config::ALARM_* / HEALTH_*
#include "libbotanik/config/identity.h"   // Config::SITE_NAME / FIRMWARE_VERSION / MQTT_TOPIC_PREFIX
#include "libbotanik/config/logging.h"    // Config::LOG_*
#include "libbotanik/config/modbus.h"     // Config::MODBUS_* - both transports
#include "libbotanik/config/net.h"        // Config::NET_* / DNS_* / NTP_* / MQTT_*
#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*
#include "libbotanik/config/serial.h"     // Config::SERIAL_* / *_BAUD
#include "libbotanik/config/simulator.h"  // Config::STORE_MAX_BYTES
#include "libbotanik/config/sun.h"        // Config::SUN_*
#include "libbotanik/config/terminal.h"   // Config::SCREEN_* / EDITOR_* / SHELL_* / PAGE_*
#include "libbotanik/config/vars.h"       // Config::VAR_* / STR_*
#endif // LIBBOTANIK_CONFIG_H
