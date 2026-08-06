// REVIEW_35: TODO
#ifndef BOTANIK_FW_FIRMWARE_H
#define BOTANIK_FW_FIRMWARE_H

/**
 * @file firmware.h
 * @brief OPTA firmware: the selected site, built for the hardware.
 *
 * Hardware counterpart of botanik_sim: same site, plain Config timeouts (the
 * simulator dilates them).
 */

// Site selected at configure time (-DBOTANIK_SITE); libbotanik_site.h derives
// BOTANIK_SITE_HEADER -> its site.h, exposing BotanikSite.
#include "libbotanik_site.h"    // resolves BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include BOTANIK_SITE_HEADER    // BotanikSite

namespace Botanik { namespace Firmware {

/** @brief The OPTA firmware singleton. A function-local static, first
 *  constructed on the first call - deferred until after the Arduino core's
 *  init() has validated the UARTs. */
::BotanikSite& instance();

}}  // namespace Botanik::Firmware

#endif // BOTANIK_FW_FIRMWARE_H
