#ifndef SCAN_SCAN_H
#define SCAN_SCAN_H

/**
 * @file scan.h
 * @brief The scan cycle: the driver that runs the site's control programs.
 *
 * The PLC idiom, on top of the existing beat. LoopScheduler gates a fixed beat;
 * within it the firmware reads inputs, runs the scan, then writes outputs.
 * Programs are multi-rate: each declares a period and runs when it comes due.
 *
 * The programs themselves are Plc::Program (plc/program.h). This file is the
 * CYCLE, not the vocabulary: which program is due, how late it is, and whether
 * it may run at all. The dependency points one way: the driver knows about
 * programs, a program knows nothing about the driver.
 *
 * @section scan_coherency Coherency
 *
 * Inputs are refreshed ONCE per beat, before any program runs, so every program
 * in a beat sees the same instant. No per-program snapshots: programs run
 * sequentially in one thread and never span beats (one that did is an overrun,
 * reported as CPU_XRUN).
 *
 * @section scan_lateness Lateness
 *
 * A program that comes due late runs ONCE; missed activations are dropped, not
 * caught up. So Plc::Context::dt_ms is the ACTUAL elapsed time since that
 * program last ran, never its nominal period, and Plc::Context::missed carries
 * the dropped count.
 */

#include <cstdint>                      // uint8_t/uint64_t

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/plc/program.h"     // Plc::Program
#include "libbotanik/vars/model.h"      // Vars::Bank

namespace Botanik { namespace Scan {

    /** @brief Runs the site's control programs at their declared rates.
     *
     * Stepped once per beat from the firmware loop, between the input refresh
     * and the output write. Holds a borrowed array of borrowed program
     * pointers; all per-program state lives in the program itself, so there is
     * no parallel array to keep the same length as the site's table.
     *
     * The dispatch arithmetic belongs to Component (a Plc::Program is one).
     * What is this class's own is the order, and the control-authority gate
     * with its two edges. */
    class Driver : public Component {

        /** @name State
         *  @{ */

        Plc::Program* const* _programs = nullptr;
        uint8_t              _count    = 0;
        Vars::Bank*          _io       = nullptr;   ///< borrowed; firmware owns it
        /** @brief Whether control was held on the previous step. Edge tracking
         *  only (reset the programs when it goes, reseed their timers when it
         *  returns); the authority itself lives in the Bank. Starts false
         *  because control starts withheld. */
        bool                 _enabled  = false;
        /** @brief The conflict count already reported, so a defect that is true
         *  on every beat is named once, not every 10 ms.
         *  @see Vars::Bank::claim_writer() */
        uint16_t             _conflicts_said = 0;
        /** @brief The row carrying "a program is missing its period", resolved
         *  once at bring-up like every other name the library reads. */
        Vars::VarRef         _late_alarm = { Vars::VAR_NONE };
        /** @brief The worst lateness this beat, and who owned it. Borrowed from
         *  the program table, so it lives as long as the driver does. */
        uint32_t             _late_ms = 0;
        const Plc::Program*  _worst   = nullptr;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Bind the (borrowed) program table. The array and every
         *  program in it must outlive the driver; a site declares them as
         *  statics. */
        Driver(Plc::Program* const* programs, uint8_t count,
               Vars::Bank* io, Logs::Logger* logger = nullptr)
            : Component(logger, "Scan"), _programs(programs), _count(count),
              _io(io) {}

        /** @} */
        /** @name Accounting
         *  @{ */

        /** @brief How late the worst program was on the last beat, and which one
         *  - null when nothing was late. The alarm says something is missing its
         *  period; these say how badly and by whom. */
        uint32_t            late_ms() const   { return _late_ms; }
        const Plc::Program* worst() const     { return _worst; }
        uint8_t             count() const     { return _count; }
        const Plc::Program* program(uint8_t i) const {
            return (i < _count) ? _programs[i] : nullptr;
        }

        /** @} */

    protected:
        /** @brief Hand each program the bank, stagger it, and run its one-shot
         *  setup. The task registry calls this inside BotanikFirmware::setup(),
         *  so name resolution happens off the beat and before the watchdog is
         *  armed.
         *
         *  The stagger keeps programs of one period off the same beat
         *  (@ref timer_offset). SCAN_STAGGER_MS = 0 turns it off for a site
         *  whose programs must share a beat. */
        void on_setup(uint64_t now_ms) override;
        /** @brief Run every program that has come due, in table order. */
        void on_step(uint64_t now_ms) override;

    private:
        /** @brief Name a row that has acquired a second author, once per new
         *  conflict. The bank counts them; only the driver can turn a program
         *  index back into a program name. */
        void _report_conflicts();
        /** @brief Find this beat's worst-late program and drive the alarm row. */
        void _publish_lateness();
    };

}}  // namespace Botanik::Scan

#endif // SCAN_SCAN_H
