#ifndef LIBBOTANIK_COMPONENT_H
#define LIBBOTANIK_COMPONENT_H

/**
 * @file component.h
 * @brief Component - base that announces a component's first step() into the log.
 *
 * In DEBUG builds every important loop component logs one DEBUG / BOOT line the first
 * time it is stepped, confirming it is actually wired into the main event loop (a
 * component that is constructed but never stepped stays silent, which is the bug this
 * catches). The announce fires on the first step(), never in the constructor -
 * construction says nothing about loop inclusion. With DEBUG undefined (the OPTA
 * release build) it compiles down to setting a flag.
 *
 * A component derives this and calls announce_step(logger, name) at the top of its
 * step(); ConfigHandler does it for all the config-driven handlers.
 */

#include "libbotanik/plcio/vars.h"     // LogTopic (PlcIO::BOOT)
#include "libbotanik/logs/logger.h"    // Logs::Logger

namespace Botanik {

    /** @brief Mixin base: one-shot first-step DEBUG / BOOT announce (loop-inclusion marker). */
    class Component {
        /** @brief Whether the first-step announce has fired. */
        bool _announced = false;

    protected:
        /** @brief Log a one-time DEBUG / BOOT line the first time the component is
         *  stepped, to show it reached the main loop. Call at the top of step(): a no-op
         *  after the first call, and (bar setting the flag) compiled out when DEBUG is
         *  undefined. @p name is the component's class name, @p logger may be null. */
        void announce_step(Logs::Logger* logger, const char* name) {
            if (_announced) return;
            _announced = true;
#ifdef DEBUG
            if (logger != nullptr) logger->debug(PlcIO::BOOT, "%s initialized", name);
#else
            (void)logger; (void)name;
#endif
        }
    };

}

#endif // LIBBOTANIK_COMPONENT_H
