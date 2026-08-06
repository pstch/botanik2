// REVIEW_35: TODO
/**
 * @file firmware.cpp
 * @brief Constructs the OPTA firmware singleton (see firmware.h).
 *
 */

#include "botanik_fw/firmware.h"      // the setup()/loop() entry points implemented here

namespace Botanik { namespace Firmware {

::BotanikSite& instance() {
    static ::BotanikSite fw;
    return fw;
}

}}  // namespace Botanik::Firmware
