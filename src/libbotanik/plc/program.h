#ifndef PLC_PROGRAM_H
#define PLC_PROGRAM_H

/**
 * @file program.h
 * @brief Program - the third POU: what a site writes its control logic as.
 *
 * A function computes, a block remembers, a program DRIVES: it reads the
 * variable bank, applies its blocks, and writes demands back. It is the only
 * one of the three that knows the field exists.
 *
 * @section plc_program_state Why this is a class
 *
 * A program used to be a bare `void(*)(Context&)` with its state in
 * function-local statics. That works exactly once. It cannot be instantiated
 * twice (two irrigation zones, four window banks), it cannot be tested without
 * poking process-global state, it has nowhere to cache the variable lookups it
 * repeats every scan, and nothing can reach into it to reset it. All four are
 * the same missing thing: an object.
 *
 * @section plc_program_bind Resolving names once
 *
 * bind() runs once at bring-up, and is where a program turns the names it uses
 * into VarRefs. Everything else in the firmware already works this way - the
 * alarm table, the home fields, the status groups all resolve at bring-up - and
 * control programs were the last consumers still walking the table by strcmp on
 * every activation. A program that names six variables at 10 Hz was doing sixty
 * linear searches of a 249-row table per second to learn what it already knew.
 *
 * @section plc_program_reset Resetting on a control withdrawal
 *
 * When control authority is withdrawn the scan stops, and the process carries
 * on without us: a vent creeps shut, a tank drains, a room cools. Anything a
 * program remembered about the field is stale from that moment, and reseeding
 * dt on the way back in does not fix it - dt cannot express "the process moved
 * while you were not looking". The driver calls reset() on the withdrawal edge
 * so a program can drop what has gone stale.
 *
 * It is opt-in per program, and deliberately not "clear everything": a dwell
 * timer or a latched decision is stale, but a totalizer holding the litres
 * delivered today is a RECORD, and zeroing it would be a small act of data
 * loss. Override reset() to drop the former and keep the latter.
 */

#include <cstdint>

#include "../scheduler/task_timer.h"
#include "../vars/model.h"

namespace Botanik {

    namespace Scan { class Driver; }   // runs programs; owns their timers

    namespace Plc {

    /** @brief What a program is handed each time it runs.
     *
     *  Lives here rather than with the scan driver because it is the PROGRAM's
     *  argument: the driver depends on the POU library, never the other way
     *  round. */
    struct Context {
        /** @brief Beat timestamp; identical for every program run this beat. */
        uint64_t now_ms;
        /** @brief Actual ms since THIS program last ran (its first run reports
         *  its nominal period). Drives every timed function block. */
        uint32_t dt_ms;
        /** @brief Activations dropped since the last run; 0 normally. */
        uint16_t missed;
        /** @brief The variables this program reads and drives. */
        Vars::Bank& io;
    };

    /**
     * @brief Base class for a site's control programs.
     *
     * A program declares its name and rate to this constructor, holds its
     * function blocks as members, and overrides run(). The optional hooks are
     * bind() (@ref plc_program_bind) and reset() (@ref plc_program_reset).
     *
     * @code
     * class SupplyWatch : public Plc::Program {
     *     Plc::Ton     _bad_for;
     *     Plc::Sr      _lockout;
     *     Vars::VarRef _ln, _out;
     * public:
     *     SupplyWatch() : Program("supply_watch", 100) {}
     *
     *     void bind(Vars::Bank& io) override {
     *         _ln  = io.find("grid_LN_V");
     *         _out = io.find("sys_supply_lockout");
     *     }
     *     void run(Plc::Context& c) override {
     *         const bool bad = !Plc::in_range(c.io.var(_ln).value(), 200.0f, 260.0f);
     *         c.io.var(_out).set(_lockout(_bad_for(bad, 5000, c.dt_ms), !bad));
     *     }
     *     void reset() override { _bad_for.reset(); _lockout.reset(); }
     * };
     * @endcode
     *
     * RAM-resident, so the NSDMIs are fine (unlike the descriptor tables, which
     * are const and flash-resident). The site's table of these is an array of
     * POINTERS, which stays in flash: the pointer values are link-time
     * constants even though the objects they name are in RAM.
     */
    class Program {

        /** @name Identity and rate
         *  @{ */

        const char* _name;
        uint32_t    _period_ms;

        /** @} */
        /** @name Scheduling state
         *  @{ */

        /** @brief When this program last ran, and how often. Private, and the
         *  driver is the only thing that may move it: a program that could
         *  stamp its own timer could silently skip its next activation. It used
         *  to be a parallel TaskStats[] the site had to declare and keep the
         *  same length as its task table. */
        Scheduler::TaskTimer _timer;

        friend class Scan::Driver;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name for logs and the TASKS page; @p period_ms is the run
         *  interval, 0 meaning every beat. Both are borrowed constants. */
        Program(const char* name, uint32_t period_ms)
            : _name(name), _period_ms(period_ms) {}

        /** @brief Resolve the variable names this program uses, once, at
         *  bring-up. Default: nothing to resolve. */
        virtual void bind(Vars::Bank& io) { (void)io; }

        /** @brief One activation. The only required override. */
        virtual void run(Context& c) = 0;

        /** @brief Drop state that control authority going away has made stale.
         *  Default: nothing is. See @ref plc_program_reset. */
        virtual void reset() {}

        /** @} */
        /** @name Identity
         *  @{ */

        const char* name() const { return _name; }
        uint32_t period_ms() const { return _period_ms; }
        /** @brief Completed activations; 0 until the first. Diagnostics only -
         *  the timer itself is the driver's. */
        uint32_t runs() const { return _timer.runs(); }
        /** @brief When it last ran. Meaningless while runs() is 0. */
        uint64_t last_run_ms() const { return _timer.last_run_ms(); }

        /** @} */

    protected:
        /** @brief Non-virtual and protected: programs are site statics, never
         *  deleted, and least of all through this base. Saves the vtable slot a
         *  virtual destructor would cost every program class. */
        ~Program() {}
    };

    }  // namespace Plc
}  // namespace Botanik

#endif // PLC_PROGRAM_H
