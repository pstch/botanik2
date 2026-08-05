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
 * it may run at all. The dependency only ever points this way - the driver
 * knows about programs, a program knows nothing about the driver.
 *
 * @section scan_coherency Coherency
 *
 * Inputs are refreshed ONCE per beat, before any program runs, so every program
 * in a beat sees the same instant. That is the whole coherency story - there
 * are no per-program snapshots, because programs run sequentially in one thread
 * and never span beats. One that did span a beat is an overrun, which the
 * scheduler already detects and reports as CPU_XRUN.
 *
 * @section scan_lateness Lateness
 *
 * A program that comes due late runs ONCE; missed activations are dropped, not
 * caught up. A burst of catch-up runs is worse than a missed one - it lands
 * exactly when the system is already behind.
 *
 * The consequence is that Plc::Context::dt_ms is the ACTUAL elapsed time since
 * that program last ran, never its nominal period. A timer or integrator handed
 * the nominal period after a stall silently loses the time it was late by.
 * Plc::Context::missed carries the dropped count for logic that wants to know.
 */

#include <cstdint>

#include "../core/component.h"
#include "../plc/program.h"
#include "../vars/model.h"

namespace Botanik { namespace Scan {

    /** @brief Runs the site's control programs at their declared rates.
     *
     * Stepped once per beat from the firmware loop, between the input refresh
     * and the output write. Holds a borrowed array of borrowed program
     * pointers; every scrap of per-program state lives in the program itself,
     * so the driver owns nothing - which is what removed the parallel stats
     * array a site used to declare alongside its task table.
     *
     * The dispatch arithmetic is not here either: a Plc::Program is a Component,
     * so "is it due, how late, how many were dropped" is the one implementation
     * every other component uses. What is left in this class is what is
     * genuinely the scan cycle's own - the order, and the control-authority
     * gate with its two edges. */
    class Driver : public Component {

        /** @name State
         *  @{ */

        Plc::Program* const* _programs = nullptr;
        uint8_t              _count    = 0;
        Vars::Bank*          _io       = nullptr;   ///< borrowed; firmware owns it
        /** @brief Whether control was held on the previous step. Edge tracking
         *  only - so timers can be reseeded when it returns and programs reset
         *  when it goes; the authority itself lives in the Bank. Starts false
         *  because control starts withheld. */
        bool                 _enabled  = false;

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

    protected:
        /** @brief Hand each program the bank, stagger it, and run its one-shot
         *  setup - all of it inside BotanikFirmware::setup(), since that is
         *  where the task registry calls this, not on the first beat. Binding
         *  here rather than lazily on first step keeps the name resolution off
         *  the beat entirely, and ahead of the watchdog being armed.
         *
         *  Without the stagger every program seeds its timer on the same first
         *  beat and they stay phase-locked to boot, so two 100 ms programs
         *  share a beat forever - which leaves the PEAK work per beat exactly
         *  where it would be with no rates at all. SCAN_STAGGER_MS = 0 turns it
         *  off for a site whose programs must share a beat. */
        void on_setup(uint64_t now_ms) override;
        /** @brief Run every program that has come due, in table order. */
        void on_step(uint64_t now_ms) override;
    };

}}  // namespace Botanik::Scan

#endif // SCAN_SCAN_H
