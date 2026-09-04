#ifndef BOTANIK_FW_FIRMWARE_H
#define BOTANIK_FW_FIRMWARE_H

/**
 * @file firmware.h
 * @brief OPTA firmware: the selected site, built for the hardware.
 *
 * Hardware counterpart of botanik_sim: same site, plain Config timeouts (the
 * simulator overrides them).
 */

// Site selected at configure time (-DBOTANIK_SITE).
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER    // Site::firmware()

namespace Botanik { namespace Firmware {

/** @brief The OPTA firmware, bound to the selected site. The site owns the
 *  function-local static behind it, so construction is deferred until after
 *  the Arduino core's init() has validated the UARTs. */
inline BotanikFirmware& instance() { return Site::firmware(); }

}}  // namespace Botanik::Firmware

#endif // BOTANIK_FW_FIRMWARE_H
