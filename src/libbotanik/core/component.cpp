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

#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"
#include "libbotanik/tasks/tasks.h"

namespace Botanik {

    void Component::announce() {
#ifdef DEBUG
        if (_logger != nullptr) _logger->debug(Logs::BOOT, "%s initialized", _name);
#endif
    }

}

namespace Botanik { namespace Tasks {

    /** @brief Declared in tasks.h, defined here: this is the one TU below
     *  `core/` that has Component complete. setup() is idempotent, so a
     *  component registered in several phases still sets up once. */
    void component_setup(Component* c, uint64_t now_ms) {
        c->setup(now_ms);
    }

    void component_poll_config(Component* c, uint64_t now_ms) {
        c->poll_config(now_ms);
    }

}}
