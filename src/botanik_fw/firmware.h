#ifndef BOTANIK_FW_FIRMWARE_H
#define BOTANIK_FW_FIRMWARE_H

/**
 * @file firmware.h
 * @brief OPTA firmware: the site firmware bound to the coprocessor's PLC shared memory.
 *
 * Hardware counterpart of botanik_sim. The simulator owns the PLC region as local
 * statics with time-dilated timeouts; here the region is the M4 coprocessor's (via
 * HAL::get_plc_input()/get_plc_output()) and the timeouts are the plain Config values.
 */

// Site selected at configure time (-DBOTANIK_SITE); CMake points BOTANIK_SITE_HEADER at
// its site.h, exposing BotanikSite + the payload.
#include BOTANIK_SITE_HEADER

namespace Botanik { namespace Firmware {

/** @brief The OPTA firmware singleton: the selected site bound to the PLC region. A
 *  function-local static, first constructed on the first call - deferred until after
 *  the Arduino core's init() has validated the UARTs and shared memory. */
::BotanikSite& instance();

}}  // namespace Botanik::Firmware

#endif // BOTANIK_FW_FIRMWARE_H
