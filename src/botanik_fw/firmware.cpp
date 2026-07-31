// REVIEW_35: TODO
/**
 * @file firmware.cpp
 * @brief Constructs the OPTA firmware singleton against the coprocessor's PLC
 *        shared memory (see firmware.h).
 *
 */

#include "botanik_fw/firmware.h"

namespace Botanik { namespace Firmware {

::BotanikSite& instance() {
    // The site's HAL constructor binds to the M4 coprocessor's PLC region at
    // the config-default baud (no region casts here).
    static ::BotanikSite fw;
    return fw;
}

}}  // namespace Botanik::Firmware
