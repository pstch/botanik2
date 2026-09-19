/**
 * @file component.cpp
 * @brief The registry's trampolines into Component.
 *
 * Out of line so component.h can forward-declare Logs::Logger, which is what
 * lets Logger itself derive Component, and so `tasks/` never includes `core/`.
 */

#include "libbotanik/core/component.h"

#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Tasks {

    /** @brief Declared in tasks.h, defined here: this is the one TU that has
     *  both Component and the registry complete. setup() is idempotent, so a
     *  component registered in several phases still sets up once. */
    void component_setup(Component* c, uint64_t now_ms) { c->setup(now_ms); }
    void component_step(Component* c, uint64_t now_ms, Phase phase) { c->step(now_ms, phase); }
    void component_sample_verdict(Component* c) { c->sample_verdict(); }
    const Plc::Health* component_verdict_slot(const Component* c) { return c->verdict_slot(); }

}}
