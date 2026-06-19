#ifndef BOTANIK_FW_FIRMWARE_H
#define BOTANIK_FW_FIRMWARE_H

/**
 * @file firmware.h
 * @brief OPTA firmware: the gh_mlb41 site firmware bound to the coprocessor's PLC
 *        shared memory.
 *
 * Hardware counterpart of the botanik_sim harness. The simulator owns the PLC
 * shared region as local statics and applies the time-dilated link timeouts; here
 * the region is the M4 coprocessor's, reached through HAL::get_plc_input()/
 * get_plc_output(), and the timeouts are the plain Config values. Everything else
 * (the components, the per-tick loop, the pages) is the shared GreenhouseMLB41 /
 * BotanikFirmware machinery.
 */

// The site is selected at configure time (-DBOTANIK_SITE); CMake points
// BOTANIK_SITE_HEADER at its site.h, which exposes BotanikSite + the payload.
#include BOTANIK_SITE_HEADER

namespace Botanik { namespace Firmware {

/** @brief The OPTA firmware singleton: the selected site (BotanikSite) bound to the
 *  PLC shared region from HAL::get_plc_input()/get_plc_output(). A function-local
 *  static, first constructed on the first call - deferred until after the Arduino
 *  core's init() has validated the UARTs and the shared-memory region, while staying
 *  in static storage (no heap) for the whole run. */
::BotanikSite& instance();

}}  // namespace Botanik::Firmware

#endif // BOTANIK_FW_FIRMWARE_H
