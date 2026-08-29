/**
 * @file component.cpp
 * @brief Tasks::component_setup() - the registry's trampoline into Component.
 *
 * Out of line so component.h can forward-declare Logs::Logger, which is what
 * lets Logger itself derive Component.
 */

#include "libbotanik/core/component.h"

#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Tasks {

    /** @brief Declared in tasks.h, defined here: this is the one TU below
     *  `core/` that has Component complete. setup() is idempotent, so a
     *  component registered in several phases still sets up once. */
    void component_setup(Component* c, uint64_t now_ms) { c->setup(now_ms); }

}}
