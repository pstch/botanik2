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
 * A program is a Component, and it REGISTERS ITSELF: one entry in the task
 * registry, in PHASE_CONTROL, at the rate group it names. The registry does the
 * due/late/dropped arithmetic every task gets, and hands it back in the Tick -
 * so a program carries no timer of its own and gets a row on the TASKS page like
 * anything else.
 *
 * Two hooks, in lifecycle order:
 *
 * | Hook | When | For |
 * |---|---|---|
 * | `bind(io)` | once, at bring-up | names into VarRefs, so `run()` never searches the table |
 * | `run(c)` | each activation | read, apply blocks, drive |
 *
 * @section plc_program_bind The first scan
 *
 * There is no `reset()` and no `resume()`. While control is withdrawn the
 * program is simply not called, and nothing outside it reads what it remembers -
 * so there is nothing to do on the way down. On the way back up
 * `Context::first_scan` is raised, and **that is where a program drops what has
 * gone stale and looks at the plant before commanding it**:
 *
 * @code
 * void run(Plc::Context& c) override {
 *     if (c.first_scan) { _bad_for.reset(); _lockout.reset(); }
 *     ...
 * }
 * @endcode
 *
 * Opt-in, and not "clear everything": a dwell timer is stale, a totalizer
 * holding the day's litres is a record.
 *
 * Doing it here rather than on the edge is not only fewer hooks, it is better
 * placed. The registry reseeds the timer on the way back, so the program does
 * NOT run on the beat authority returned - it waits its full period. A hook on
 * that edge would look at an image its own next `run()` never sees.
 *
 * Bring-up is the same event and needs no special case: a task's first
 * activation ever is a first scan too, because a program knows no more about the
 * plant at bring-up than after an outage.
 */

#include <cstdint>                      // uint16_t/uint32_t/uint64_t

#include "libbotanik/core/component.h"  // Component
#include "libbotanik/tasks/tasks.h"     // Tasks::Registry / Tick / GroupId
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
         *  go on: at bring-up, or on the first activation after control came
         *  back. Straight off Tasks::Tick::first.
         *
         *  IEC 61131-3's first-scan flag, and the ONLY control-authority signal
         *  a program gets. A law that ramps, latches or compares against "last
         *  time" has no last time here, so it must ask rather than read a
         *  member's initial value - and anything gone stale is dropped under
         *  it. @see plc_program_bind */
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
     *     SupplyWatch() : Program("supply_watch", Tasks::GROUP_SLOW) {}
     *
     * protected:
     *     void bind(Vars::Bank& io) override {
     *         _ln  = io.find("grid_LN_V");
     *         _out = io.find("sys_supply_lockout");
     *     }
     *     void run(Plc::Context& c) override {
     *         if (c.first_scan) { _bad_for.reset(); _lockout.reset(); }
     *         const bool bad = !Plc::in_range(c.io.value(_ln), 200.0f, 260.0f);
     *         c.io.set(_out, _lockout(_bad_for(bad, 5000, c.dt_ms), !bad));
     *     }
     * };
     * @endcode
     */
    class Program : public Component {

        /** @name Wiring
         *  @{ */

        /** @brief Borrowed; the firmware owns it. Held rather than passed per
         *  activation because the registry's handler takes only a Tick. */
        Vars::Bank* _io = nullptr;
        /** @brief The rate this program named. Its task carries the timer; this
         *  is kept only to measure lateness against the nominal period. */
        Tasks::GroupId _group = Tasks::GROUP_FAST;
        /** @brief This program's index in the site's table, stamped onto every
         *  row it writes. The firmware assigns it; WRITER_NONE until then. */
        uint8_t     _slot = Vars::WRITER_NONE;
        /** @brief How far past its nominal period this activation was, and the
         *  worst yet. */
        uint32_t    _late_ms     = 0;
        uint32_t    _late_max_ms = 0;
        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name for the boot announce, the TASKS row and diagnostics;
         *  @p group is the rate it runs at, and @p io is the field it drives.
         *  The name is a borrowed constant.
         *
         *  A group, not a period: a program is scheduled like everything else
         *  in the beat, so retuning a rate class retunes it too.
         *
         *  The bank comes in HERE rather than through a later attach(), and a
         *  site can do that because its programs are members of its firmware
         *  class: the base is fully constructed before any derived member, so
         *  io() and logger() are alive by the time these run. A program with no
         *  field to drive is not a state worth having. */
        Program(const char* name, Tasks::GroupId group, Vars::Bank& io,
                Logs::Logger* logger = nullptr)
            : Component(logger, name), _io(&io), _group(group) {}

        /** @brief Which slot of the registry's program list this occupies -
         *  the writer id stamped onto every row it drives.
         *  Registry::add_program()'s to set: two programs claiming one index
         *  would silently share an author. */
        void set_slot(uint8_t slot) { _slot = slot; }
        uint8_t slot() const        { return _slot; }

        /** @brief The rate group this program named. */
        Tasks::GroupId group() const { return _group; }

        /** @brief One entry, PHASE_CONTROL by construction and gated by control
         *  authority. Registering is also what runs bind(), through
         *  Component::setup(). */
        void register_tasks(Tasks::Registry& r) override;

        /** @} */
        /** @name Accounting
         *  @{ */

        /** @brief Milliseconds past its nominal period on the last activation,
         *  and the worst so far. Always 0 for a GROUP_FAST program: due every
         *  beat, so it has no slack to be late in. */
        uint32_t late_ms() const     { return _late_ms; }
        uint32_t late_max_ms() const { return _late_max_ms; }

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

        /** @} */

    private:
        /** @brief The registration handler. A static MEMBER, so the cast has
         *  access to the private activation below and stays beside the class. */
        static void _run_task(Component* c, const Tasks::Tick& t);

        /** @brief One activation, off the Tick the registry measured. The
         *  registry already timed it into the task's own TaskCost, so there is
         *  nothing to account for here beyond the lateness. */
        void _activate(const Tasks::Tick& t) {
            if (_io == nullptr) return;

            // Against the program's OWN period, not the beat: 40 ms late is
            // fine for a 1 s program and not for a 100 ms one.
            const uint32_t p = Tasks::group_period_ms(_group);
            _late_ms = (t.dt_ms > p) ? (t.dt_ms - p) : 0;
            if (_late_ms > _late_max_ms) _late_max_ms = _late_ms;

            Context c = { t.now_ms, t.dt_ms, t.missed, t.first, Io(*_io, _slot) };
            run(c);
        }
    };

    /** @brief The worst lateness across @p programs, and who owned it (null
     *  when nothing was late). The alarm row says a program is missing its
     *  period; this says how badly and by whom. */
    uint32_t worst_lateness(const Tasks::Registry& r, const Program** worst);

    /** @brief Name a row that has acquired a second author, once per new
     *  conflict.
     *
     *  The bank COUNTS them (Vars::Bank::claim_writer) but cannot name them: a
     *  writer is an index, and only the program table turns one back into a
     *  name. @p said carries the count already reported, so a defect that is
     *  true on every beat from here is logged once rather than every 10 ms.
     *
     *  @return whether it logged. */
    bool report_writer_conflicts(Vars::Bank& io, const Tasks::Registry& r,
                                 Logs::Logger* logger, uint16_t& said);


}}  // namespace Botanik::Plc

#endif // PLC_PROGRAM_H
