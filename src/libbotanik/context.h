#ifndef CONTEXT_H
#define CONTEXT_H

/**
 * @file context.h
 * @brief SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads.
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

        /** @brief Bundle the subsystem contexts (either may be null when absent).
         *  Defaulted args also serve as the empty default construction. */
        SystemContext(Scheduler::SchedulerContext* sched = nullptr,
                      PlcIO::PlcSyncContext*        plc_ctx = nullptr)
            : scheduler(sched), plc(plc_ctx) {}
    };

}

#endif // CONTEXT_H
