#ifndef SCAN_SCAN_H
#define SCAN_SCAN_H

/**
 * @file scan.h
 * @brief The scan cycle: Task, ScanContext, and the driver that runs due tasks.
 *
 * The PLC idiom, on top of the existing beat. LoopScheduler gates a fixed beat;
 * within it the firmware reads inputs, runs the scan, then writes outputs. Tasks
 * are multi-rate: each declares a period and runs when it comes due.
 *
 * @section scan_coherency Coherency
 *
 * Inputs are refreshed ONCE per beat, before any task runs, so every task in a
 * beat sees the same instant. That is the whole coherency story - there are no
 * per-task snapshots, because tasks run sequentially in one thread and never
 * span beats. A task that did span a beat is an overrun, which the scheduler
 * already detects and reports as CPU_XRUN.
 *
 * @section scan_lateness Lateness
 *
 * A task that comes due late runs ONCE; missed activations are dropped, not
 * caught up. A burst of catch-up runs is worse than a missed one - it lands
 * exactly when the system is already behind.
 *
 * The consequence is that ScanContext::dt_ms is the ACTUAL elapsed time since
 * that task last ran, never its nominal period. A timer or integrator handed
 * the nominal period after a stall silently loses the time it was late by.
 * ScanContext::missed carries the dropped count for logic that wants to know.
 */

#include <cstdint>

#include "../core/component.h"
#include "../vars/model.h"

namespace Botanik { namespace Scan {

    class Driver;   // below

    /** @brief What a control program is handed each time it runs. */
    struct ScanContext {
        /** @brief Beat timestamp; identical for every task run this beat. */
        uint64_t now_ms;
        /** @brief Actual ms since THIS task last ran (its first run reports its
         *  nominal period). Drives every timed function block. */
        uint32_t dt_ms;
        /** @brief Activations dropped since the last run; 0 normally. */
        uint16_t missed;
        /** @brief The variables this program reads and drives. */
        Vars::Bank& io;
    };

    /** @brief A control program. Const and flash-resident like the other site
     *  tables; the program's own state lives in its statics or in the function
     *  blocks it holds. */
    struct Task {
        const char* name;
        /** @brief Run interval in ms; 0 means every beat. */
        uint32_t    period_ms;
        void      (*run)(ScanContext&);
    };

    /** @brief Per-task scheduling state. RAM-resident, so the NSDMIs are fine
     *  here (unlike the flash-resident descriptor tables).
     *  @note Duration/overrun telemetry lived here but nothing ever read it; the
     *  per-run `missed` count the task itself receives is in ScanContext. */
    struct TaskStats {
        uint64_t last_run_ms   = 0;   ///< when it last ran (0 = never)
        uint32_t runs          = 0;
    };

    /** @brief Runs the site's tasks at their declared rates.
     *
     * Stepped once per beat from the firmware loop, between the input refresh
     * and the output write. Holds the borrowed task table and one TaskStats per
     * task; the stats array is the only state, so a site can hand the same
     * const table to as many drivers as it likes. */
    class Driver : public Component {

        /** @name State
         *  @{ */

        const Task* _tasks = nullptr;
        uint8_t     _count = 0;
        TaskStats*  _stats = nullptr;   ///< caller-owned, _count entries
        Vars::Bank*   _io    = nullptr;   ///< borrowed; the firmware owns it

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the (borrowed) task table and its stats storage. Both
         *  must outlive the driver; a site declares them as statics. */
        Driver(const Task* tasks, uint8_t count, TaskStats* stats,
               Vars::Bank* io, Logs::Logger* logger = nullptr)
            : Component(logger, "Scan"), _tasks(tasks), _count(count),
              _stats(stats), _io(io) {}

        /** @} */

    protected:
        /** @brief Run every task that has come due, in table order. */
        void on_step(uint64_t now_ms) override;
    };

}}  // namespace Botanik::Scan

#endif // SCAN_SCAN_H
