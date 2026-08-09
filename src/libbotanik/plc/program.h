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
 * A program is a Component, so the lateness contract, the multi-rate dispatch
 * and the stagger live in one place for the whole firmware: the driver calls
 * step(), Component does the due/late/dropped arithmetic. It inherits
 * on_param_changed() (re-read tuning when its parameter group commits) and
 * healthy() with it.
 *
 * Three hooks, in lifecycle order:
 *
 * | Hook | When | For |
 * |---|---|---|
 * | `bind(io)` | once, at bring-up | names into VarRefs, so `run()` never searches the table |
 * | `run(c)` | each activation | read, apply blocks, drive |
 * | `reset()` | control authority withdrawn | drop what has gone stale |
 *
 * @section plc_program_bind bind, and reset
 *
 * Every other consumer in the firmware resolves its names once at bring-up;
 * control programs were the last still searching on every activation, which for
 * six names at 10 Hz is sixty table searches a second to learn what the program
 * already knew.
 *
 * reset() is called because while control is withdrawn the process moves
 * without us - a vent creeps shut, a tank drains - so what a program remembered
 * is stale, and reseeding dt on the way back cannot express that. Opt-in, and
 * deliberately not "clear everything": a dwell timer is stale, but a totalizer
 * holding the litres delivered today is a record.
 */

#include <cstdint>                      // uint16_t/uint32_t/uint64_t

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/plc/io.h"           // Plc::Io - the narrowed view a program gets
#include "libbotanik/vars/model.h"      // Vars::Bank / VarRef

namespace Botanik { namespace Plc {

    /** @brief What a program is handed each time it runs. Lives here, not with
     *  the driver: it is the PROGRAM's argument, and the dependency runs
     *  driver -> POU library, never back. */
    struct Context {
        uint64_t now_ms;   ///< beat timestamp; the same for every program this beat
        uint32_t dt_ms;    ///< REAL ms since this program last ran; drives every timed block
        uint16_t missed;   ///< activations dropped since the last run; 0 normally
        /** @brief The variables it reads and drives - the narrowed view, not the
         *  Bank. A program has no business editing a parameter, forcing an
         *  override or withdrawing control authority, and this is what stops it
         *  reaching them by accident (@ref Botanik::Plc::Io). */
        Io       io;
    };

    /**
     * @brief Base class for a site's control programs: name and rate to the
     * constructor, function blocks as members, run() overridden.
     *
     * RAM-resident, so the NSDMIs are fine - unlike the descriptor tables, which
     * are const and flash-resident. A site's table of programs is an array of
     * POINTERS, which stays in flash: the pointer values are link-time constants
     * even though the objects are in RAM.
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
     *         const bool bad = !Plc::in_range(c.io.value(_ln), 200.0f, 260.0f);
     *         c.io.set(_out, _lockout(_bad_for(bad, 5000, c.dt_ms), !bad));
     *     }
     *     void reset() override { _bad_for.reset(); _lockout.reset(); }
     * };
     * @endcode
     */
    class Program : public Component {

        /** @name Wiring
         *  @{ */

        /** @brief Borrowed; the firmware owns it. Held rather than passed in
         *  because Component::step() takes only a timestamp. */
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
         *  The driver does this for the whole site table. */
        void attach(Vars::Bank& io, Logs::Logger* logger = nullptr) {
            _io = &io;
            if (logger != nullptr) _logger = logger;
        }

        /** @brief Drop state a control withdrawal has made stale; default,
         *  nothing is (@ref plc_program_bind). Public because it is the
         *  DRIVER's to call, on an edge the program cannot see. */
        virtual void reset() {}

        /** @} */

    protected:
        /** @name Hooks (implemented by the program)
         *  @{ */

        /** @brief Resolve this program's variable names, once, at bring-up
         *  (@ref plc_program_bind). Default: nothing to resolve. */
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

        /** @brief Component's per-activation hook: build the Context from the
         *  timer's own reading of how late this activation is, then run. */
        void on_step(uint64_t now_ms) override {
            if (_io == nullptr) return;
            Context c = { now_ms, dt_ms(), missed(), Io(*_io) };
            run(c);
        }

        /** @} */
    };

    /**
     * @brief One statically-allocated @p T, safe to name from a header.
     *
     * A program is mutable state declared in a header that several TUs include,
     * so a plain definition would be a duplicate symbol. A class template's
     * static member has vague linkage: emitted per TU, folded by the linker,
     * for one guard byte tested once during static init. (Not a function-local
     * static, which tests its guard on every access and pulls in
     * __cxa_guard_acquire/release.)
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
