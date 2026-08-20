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

    void Component::announce_task(const char* task, uint8_t group, uint8_t phase) {
#ifdef DEBUG
        if (_logger == nullptr) return;
        _logger->debug(Logs::BOOT, "%s: %s, %s", task,
                       Tasks::group_name((Tasks::GroupId)group),
                       Tasks::phase_name((Tasks::Phase)phase));
#else
        (void)task; (void)group; (void)phase;
#endif
    }

}

namespace Botanik { namespace Tasks {

    /** @brief Declared in tasks.h, defined here: this is the one TU below
     *  `core/` that has Component complete. setup() is idempotent, so a
     *  component registered in several phases still sets up once. */
    void component_setup(Component* c, uint64_t now_ms, const char* task,
                         GroupId group, Phase phase) {
        c->setup(now_ms);
        // OUTSIDE the idempotent setup: the roll-call is one line per task, and
        // a component owning three of them registers three times.
        c->announce_task(task, (uint8_t)group, (uint8_t)phase);
    }

}}
