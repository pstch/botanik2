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
 * @section plc_program_component Why it is a Component
 *
 * "A named thing that runs at its own rate, carries its own timer, and gets a
 * one-shot init" is Component, and the library already had it. Deriving means
 * the lateness contract, the multi-rate dispatch and the stagger live in ONE
 * place for the whole firmware rather than being reimplemented per runner: the
 * driver just calls step(), and Component does the due/late/dropped arithmetic
 * it does for every other component. A program also inherits the two hooks that
 * come with it - on_param_changed() (re-read tuning when its parameter group
 * commits) and healthy() - which a bespoke base would have had to grow.
 *
 * @section plc_program_bind Resolving names once
 *
 * bind() runs once at bring-up, and is where a program turns the names it uses
 * into VarRefs. Everything else in the firmware already works this way - the
 * alarm table, the home fields, the status groups all resolve at bring-up - and
 * control programs were the last consumers still walking the table by strcmp on
 * every activation. A program that names six variables at 10 Hz was doing sixty
 * linear searches of the whole variable table per second to learn what it
 * already knew.
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

#include <cstdint>                    // uint16_t/uint32_t/uint64_t - rate, dt_ms, last activation

#include "libbotanik/core/component.h" // Component - a program carries its own rate machinery
#include "libbotanik/vars/model.h"    // Vars::Bank / VarRef - bind() resolves names once at bring-up

namespace Botanik { namespace Plc {

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
     *     Vars::VarRef _ln = { Vars::VAR_NONE }, _out = { Vars::VAR_NONE };
     * public:
     *     SupplyWatch() : Program("supply_watch", 100) {}
     *
     * protected:
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
    class Program : public Component {

        /** @name Wiring
         *  @{ */

        /** @brief Borrowed; the firmware owns it. Held rather than passed to
         *  every call because Component::step() takes only a timestamp - the
         *  Context is built from it and the timer's own dt. */
        Vars::Bank* _io = nullptr;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name for the boot announce and diagnostics; @p period_ms
         *  is the run interval, 0 meaning every beat. Both are borrowed
         *  constants. */
        Program(const char* name, uint32_t period_ms)
            : Component(nullptr, name) {
            set_period(period_ms);
        }

        /** @brief Hand the program the field it drives, before it is stepped.
         *  The driver does this for every program in the site's table; a test
         *  driving one directly does it itself. */
        void attach(Vars::Bank& io, Logs::Logger* logger = nullptr) {
            _io = &io;
            if (logger != nullptr) _logger = logger;
        }

        /** @brief Drop state that control authority going away has made stale.
         *  Default: nothing is. See @ref plc_program_reset.
         *
         *  Public because it is the DRIVER's to call, on an edge the program
         *  cannot see - unlike bind() and run(), which are the program's own
         *  side of the contract. */
        virtual void reset() {}

        /** @} */

    protected:
        /** @name Hooks (implemented by the program)
         *  @{ */

        /** @brief Resolve the variable names this program uses, once, at
         *  bring-up. Default: nothing to resolve. */
        virtual void bind(Vars::Bank& io) { (void)io; }

        /** @brief One activation. The only required override. */
        virtual void run(Context& c) = 0;

        /** @} */
        /** @name Component plumbing
         *  @{ */

        /** @brief Component's one-shot init: bind the names. */
        void on_setup(uint64_t now_ms) override {
            (void)now_ms;
            if (_io != nullptr) bind(*_io);
        }

        /** @brief Component's per-activation hook: assemble the Context from
         *  the timer's own reading of how late this activation is, and run. */
        void on_step(uint64_t now_ms) override {
            if (_io == nullptr) return;
            Context c = { now_ms, dt_ms(), missed(), *_io };
            run(c);
        }

        /** @} */
    };

    /**
     * @brief One statically-allocated @p T, safe to name from a header.
     *
     * A program instance is mutable state, and a site declares its programs in
     * a header that several translation units include - so a plain definition
     * would be a duplicate symbol at link time. A class template's static
     * member has vague linkage: emitted per TU and folded by the linker.
     *
     * It costs one guard byte, tested once during static initialisation so the
     * folded copies do not each run the constructor. That is not a
     * function-local static, which tests its guard on EVERY access and pulls in
     * __cxa_guard_acquire/release to do it.
     *
     * It lives here so a site never reproduces the trick:
     *
     * @code
     * Plc::Program* const g_programs[] = {
     *     &Plc::Instance<SupplyWatch>::obj,
     *     &Plc::Instance<SupplyReport>::obj,
     * };
     * @endcode
     *
     * @tparam Tag distinguishes two instances of the same program class - two
     *  irrigation zones running the same logic. Default: the only one.
     */
    template <typename T, typename Tag = void>
    struct Instance { static T obj; };
    template <typename T, typename Tag> T Instance<T, Tag>::obj;

}}  // namespace Botanik::Plc

#endif // PLC_PROGRAM_H
