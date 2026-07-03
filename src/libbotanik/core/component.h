// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_COMPONENT_H
#define LIBBOTANIK_CORE_COMPONENT_H

/**
 * @file component.h
 * @brief Component - announces a component's first step() into the log.
 *
 * In DEBUG builds, a component logs one DEBUG / BOOT line the first time it is
 * stepped, confirming it is actually wired into the main loop (one constructed
 * but never stepped stays silent - the bug this catches). On the first step(),
 * not the ctor - construction says nothing about loop inclusion. Compiles down
 * to a flag set with DEBUG undefined.
 */

#include "libbotanik/plcio/vars.h"     // LogTopic (Logs::BOOT)
#include "libbotanik/logs/logger.h"    // Logs::Logger

namespace Botanik {

    /** @brief Mixin base: one-shot first-step DEBUG / BOOT announce
     *  (loop-inclusion marker). */
    class Component {
        bool _announced = false;

    protected:
        /** @brief Log a one-time DEBUG / BOOT line on the first step(), showing
         *  the component reached the main loop. Call at the top of step();
         *  no-op after the first call, compiled out (bar the flag) when DEBUG
         *  is undefined. @p logger may be null. */
        void announce_step(Logs::Logger* logger, const char* name) {
            if (_announced) return;
            _announced = true;
#ifdef DEBUG
            if (logger != nullptr) logger->debug(Logs::BOOT, "%s initialized", name);
#else
            (void)logger; (void)name;
#endif
        }
    };

}

#endif // LIBBOTANIK_CORE_COMPONENT_H
