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
 * A program is a Component: the driver calls step(), Component does the
 * due/late/dropped arithmetic, and on_param_changed() and healthy() come with
 * it.
 *
 * Four hooks, in lifecycle order:
 *
 * | Hook | When | For |
 * |---|---|---|
 * | `bind(io)` | once, at bring-up | names into VarRefs, so `run()` never searches the table |
 * | `run(c)` | each activation | read, apply blocks, drive |
 * | `reset()` | control authority withdrawn | drop what has gone stale |
 * | `resume()` | control authority returned | look at the plant before driving it |
 *
 * @section plc_program_bind reset, and resume
 *
 * While control is withdrawn the process moves without us, so reset() drops
 * what has gone stale. Opt-in, and not "clear everything": a dwell timer is
 * stale, a totalizer holding the day's litres is a record.
 *
 * resume() is the counterpart on the way back up: the chance to LOOK at the
 * plant before commanding it, so a program estimating a valve position by
 * timing its strokes re-datums here. It also raises `Context::first_scan`.
 *
 * Bring-up runs resume() too (control starts withheld and is granted before the
 * first beat). reset() is NOT called there: nothing was ever driving.
 */

#include <cstdint>                      // uint16_t/uint32_t/uint64_t

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/hal/platform.h"    // HAL::Clock::uptime_us - per-program cost
#include "libbotanik/tasks/tasks.h"     // Tasks::TaskCost - the same accounting a task gets
#include "libbotanik/plc/io.h"           // Plc::Io - the narrowed view a program gets
#include "libbotanik/vars/model.h"      // Vars::Bank / VarRef

namespace Botanik { namespace Plc {

    /** @brief What a program is handed each time it runs. Lives here, not with
     *  the driver: the dependency runs driver -> POU library, never back. */
    struct Context {
        uint64_t now_ms;   ///< beat timestamp; the same for every program this beat
        uint32_t dt_ms;    ///< REAL ms since this program last ran; drives every timed block
        uint16_t missed;   ///< activations dropped since the last run; 0 normally
        /** @brief The program's FIRST activation since it last had nothing to
         *  go on: at bring-up, or on the beat control came back.
         *
         *  IEC 61131-3's first-scan flag. A law that ramps, latches or compares
         *  against "last time" has no last time here, so it must ask rather
         *  than read a member's initial value. */
        bool     first_scan;
        /** @brief The variables it reads and drives - the narrowed view, not
         *  the Bank: a program has no business editing a parameter, forcing an
         *  override or withdrawing control authority (@ref Botanik::Plc::Io). */
        Io       io;
    };

    /**
     * @brief Base class for a site's control programs: name and rate to the
     * constructor, function blocks as members, run() overridden.
     *
     * RAM-resident, so the NSDMIs are fine. A site's table of programs is an
     * array of POINTERS, which stays in flash.
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
        /** @brief This program's index in the site's table, stamped onto every
         *  row it writes. The driver assigns it; WRITER_NONE until then. */
        uint8_t     _slot = Vars::WRITER_NONE;
        /** @brief What this program costs. The scan is ONE task, so without
         *  this every program is a single row on the TASKS page. */
        Tasks::TaskCost _cost;
        /** @brief How far past its nominal period this activation was, and the
         *  worst yet. Zero for a period-0 program: due every beat, so it has no
         *  nominal to be late against. */
        uint32_t    _late_ms     = 0;
        uint32_t    _late_max_ms = 0;
        /** @brief Whether the next activation is a first scan. Starts true, so
         *  bring-up is a first scan without the driver having to say so. */
        bool        _first_scan  = true;

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

        /** @brief Tell the program which slot of the site's table it occupies.
         *  The DRIVER's to call: two programs claiming one index would silently
         *  share an author. */
        void set_slot(uint8_t slot) { _slot = slot; }
        uint8_t slot() const        { return _slot; }

        /** @} */
        /** @name Accounting
         *  Per PROGRAM, because the scan is one task.
         *  @{ */

        /** @brief Duration of the last run, the peak, the mean and the count. */
        const Tasks::TaskCost& cost() const { return _cost; }
        /** @brief Milliseconds past its nominal period on the last activation,
         *  and the worst so far. Always 0 for a period-0 program. */
        uint32_t late_ms() const     { return _late_ms; }
        uint32_t late_max_ms() const { return _late_max_ms; }

        /** @brief Drop state a control withdrawal has made stale; default,
         *  nothing is (@ref plc_program_bind). Public because it is the
         *  DRIVER's to call, on an edge the program cannot see. */
        virtual void reset() {}

        /** @brief Control authority has come back; the next activation will
         *  drive the field again. Default: raise the first-scan flag and
         *  nothing else (@ref plc_program_bind). Public for the same reason
         *  reset() is: the edge is the driver's. */
        virtual void resume() { _first_scan = true; }

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
        void on_control(uint64_t now_ms) override {
            if (_io == nullptr) return;

            // Against the program's OWN period, not the beat: 40 ms late is
            // fine for a 1 s program and not for a 100 ms one.
            const uint32_t p = period_ms();
            _late_ms = (p != 0 && dt_ms() > p) ? (dt_ms() - p) : 0;
            if (_late_ms > _late_max_ms) _late_max_ms = _late_ms;

            Context c = { now_ms, dt_ms(), missed(), _first_scan, Io(*_io, _slot) };
            const uint64_t t0 = HAL::Clock::uptime_us();
            run(c);
            _cost.record((uint32_t)(HAL::Clock::uptime_us() - t0), missed());
            // Cleared after the run: the flag describes the activation that
            // just happened.
            _first_scan = false;
        }

        /** @} */
    };

    /**
     * @brief One statically-allocated @p T, safe to name from a header.
     *
     * A program is mutable state declared in a header several TUs include, so a
     * plain definition would be a duplicate symbol. A class template's static
     * member has vague linkage: emitted per TU, folded by the linker. It lives
     * here so a site never reproduces the trick:
     *
     * @code
     * Plc::Program* const g_programs[] = {
     *     &Plc::Instance<SupplyWatch>::obj,
     *     &Plc::Instance<SupplyReport>::obj,
     * };
     * @endcode
     *
     * @tparam Tag distinguishes two instances of the same program class (two
     *  irrigation zones running the same logic). Default: the only one.
     */
    template <typename T, typename Tag = void>
    struct Instance { static T obj; };
    template <typename T, typename Tag> T Instance<T, Tag>::obj;

}}  // namespace Botanik::Plc

#endif // PLC_PROGRAM_H
