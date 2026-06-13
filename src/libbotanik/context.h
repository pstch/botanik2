#ifndef CONTEXT_H
#define CONTEXT_H

/**
 * @file context.h
 * @brief SystemContext — a single aggregate of the per-subsystem published
 *        status contexts (scheduler metrics, PLC link status). UI code depends
 *        on this one object instead of reaching into each subsystem's concrete
 *        context type, so new status sources can be added here without touching
 *        every consumer's constructor.
 */

// Only pointers are stored, so the pointee definitions are not needed here.
namespace Botanik { namespace Scheduler { struct SchedulerContext; } }
namespace Botanik { namespace PlcIO     { struct PlcSyncContext;   } }

namespace Botanik {

    /** @brief Borrowed-pointer aggregate of the subsystem status contexts read by
     *  the terminal UI (status bar + `stats` command). The owning subsystems
     *  outlive it; any member may be null when its subsystem is absent. */
    struct SystemContext {
        /** @brief Scheduler metrics: tick/overrun counters plus the perf snapshot. */
        Scheduler::SchedulerContext* scheduler = nullptr;
        /** @brief PLC link status and log buffers. */
        PlcIO::PlcSyncContext*       plc       = nullptr;
    };

}

#endif // CONTEXT_H
