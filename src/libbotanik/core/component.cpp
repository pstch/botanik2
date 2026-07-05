// REVIEW_35: WIP
/**
 * @file component.cpp
 * @brief Component::announce() - the one-shot first-step BOOT line.
 *
 * Out-of-line so component.h can forward-declare Logs::Logger (letting Logger
 * itself derive Component); this TU is the only place the full Logger + BOOT
 * topic are pulled in.
 */

#include "libbotanik/core/component.h"
#include "libbotanik/logs/logger.h"    // Logs::Logger (full definition)
#include "libbotanik/plcio/vars.h"     // Logs::BOOT

namespace Botanik {

    void Component::announce() {
#ifdef DEBUG
        if (_logger != nullptr) _logger->debug(Logs::BOOT, "%s initialized", _name);
#endif
    }

}
