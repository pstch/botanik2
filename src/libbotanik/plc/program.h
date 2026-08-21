#ifndef PLC_PROGRAM_H
#define PLC_PROGRAM_H

/**
 * @file program.h
 * @brief Plc::Program - the POU a site writes its control logic in.
 * @note Lifecycle, first-scan semantics and a worked example: docs/dev/scan.md
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/core/context.h"   // ProgramContext
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Plc {

    /** @brief What a program can do right now.
     *
     *  The first three are a SEVERITY run and are ordered: a supervisor takes
     *  the worst across the programs, and one comparison answers "is anything
     *  going wrong". The distinction that matters there is DEGRADED against
     *  FAULTED - short of an input but still producing good demands needs an
     *  engineer eventually; outputs gone invalid needs one now.
     *
     *  INHIBITED is off that axis: the program's CONTROL has stopped and its
     *  actuators are parked, but it is still reporting - status values and
     *  alarms keep being published, which is the whole reason the state is
     *  named for what stops rather than for the program being off. It is
     *  REACHED rather than commanded: an actuator takes time to park, and the
     *  program keeps running until it is there.
     *
     *  One state, two causes. A plant-wide safe request and the operator's
     *  per-program switch inhibit the same things in the same way; which one
     *  asked is safe_requested() / disable_requested(), not a second value
     *  every reader would have to treat alike. */
    enum class Health : uint8_t {
        HEALTHY = 0,   ///< normal operation, every output valid
        DEGRADED,      ///< an input is invalid; the outputs are still good
        FAULTED,       ///< at least one output is invalid
        INHIBITED      ///< control stopped and actuators parked; still reporting
    };

    /** @brief Whether @p h is one of the severity states, so a supervisor knows
     *  which values it may compare. */
    inline bool health_is_severity(Health h) { return h <= Health::FAULTED; }

    /** @brief A health CODE as a word: the converter a status row over this
     *  vocabulary takes the address of.
     *  @note Null for anything that is not one of the four, which is what a
     *  T_ENUM row refuses. @see health_name() for the typed form. */
    inline const char* health_label(uint64_t code) {
        switch (code) {
            case (uint64_t)Health::HEALTHY:   return "HEALTHY";
            case (uint64_t)Health::DEGRADED:  return "DEGRADED";
            case (uint64_t)Health::FAULTED:   return "FAULTED";
            case (uint64_t)Health::INHIBITED: return "INHIBITED";
            default:                          return nullptr;
        }
    }

    /** @brief @p h as a word, for the TASKS page and the logs. */
    inline const char* health_name(Health h) {
        switch (h) {
            case Health::HEALTHY:  return "HEALTHY";
            case Health::DEGRADED: return "DEGRADED";
            case Health::FAULTED:   return "FAULTED";
            case Health::INHIBITED: return "INHIBITED";
            default:                return "?";
        }
    }

    /**
     * @brief Base for a site's control programs: blocks as members, run()
     *        overridden. RAM-resident; a site's table holds pointers.
     * @note Worked example and the first-scan rules: docs/dev/scan.md
     */
    class Program : public Component {

        /** @name Wiring
         *  @{ */

        Vars::Bank*    _io    = nullptr;  ///< borrowed; the firmware owns it
        Tasks::GroupId _group = Tasks::GROUP_FAST;
        uint8_t        _slot  = Vars::WRITER_NONE;  ///< writer id stamped on rows it drives
        uint32_t       _late_ms     = 0;
        uint32_t       _late_max_ms = 0;
        /** @brief INHIBITED until the first activation: before the scan runs at
         *  all a program is not controlling anything, and reading HEALTHY there
         *  would tell a supervisor the plant was in hand during bring-up. */
        Health         _health = Health::INHIBITED;
        /** @brief Whether a safe state has been ASKED for. Distinct from having
         *  reached one, which is what @c _health says. */
        bool           _safe_asked    = false;
        bool           _disable_asked = false;
        /** @brief Set by parking() during one activation: the program is still
         *  driving toward its fail position. */
        bool           _travelling    = false;
        /** @brief Where health is published, and what was last LOGGED there.
         *  0xFF is "nothing yet", so the first pass announces whatever it
         *  finds rather than only a change from HEALTHY. */
        Vars::RefOut<uint8_t> _health_row;
        uint8_t        _health_said   = 0xFF;
        /** @brief The readable rows this program connects to. @see set_inputs */
        const Vars::VarRef* _watched = nullptr;
        uint8_t             _watched_count = 0;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name is borrowed and used for the boot announce, the TASKS
         *  row and diagnostics; @p group is the rate class it runs at. */
        Program(const SetupContext& ctx, const char* name, Tasks::GroupId group,
                Vars::Bank& io)
            : Component(ctx, name), _io(&io), _group(group) {}

        /** @brief Where this program's health is PUBLISHED, or the sink.
         *
         *  Set from the generated bind() when `programs.toml` declares a health
         *  row. The program does not write it itself: it is written by a
         *  supervisor walking every program, so a program that is not being
         *  activated - which is most of what "inhibited" means - still has a
         *  current row. @see publish_health() */
        void set_health_row(Vars::RefOut<uint8_t> r) { _health_row = r; }

        /** @brief The rows this program READS that can stop being believable,
         *  handed over by the generated bind(). Borrowed and flash-resident for
         *  the life of the program.
         *
         *  Swept before every activation: an input a law is entitled to expect
         *  and does not have is DEGRADED, whether or not the law thought to ask.
         *  Deciding it here rather than in each run() is what makes it true of
         *  EVERY program instead of the ones somebody remembered. */
        void set_inputs(const Vars::VarRef* rows, uint8_t count) {
            _watched = rows;
            _watched_count = count;
        }

        /** @brief The rows set_inputs() was handed, for a reader asking which
         *  programs consume one. Borrowed; null until bind() has run. */
        const Vars::VarRef* watched() const   { return _watched; }
        uint8_t             watched_count() const { return _watched_count; }

        Vars::RefOut<uint8_t> health_row() const { return _health_row; }

        /** @brief Registry::add_program()'s to set: two programs claiming one
         *  index would silently share an author. */
        void set_slot(uint8_t slot) { _slot = slot; }
        uint8_t slot() const        { return _slot; }

        Tasks::GroupId group() const { return _group; }

        /** @name Health
         *  What this program could do on its last activation. Re-derived each
         *  time, so a fault that clears is not sticky - a program that stops
         *  running keeps its last verdict, which is why withdrawal sets SAFE.
         *  @{ */

        Health health() const { return _health; }

        /** @name Going safe
         *
         *  SAFE is REACHED, not commanded. A request tells a program to put its
         *  own actuators where they belong; it keeps being activated while it
         *  does, because a program that is not run cannot close a valve or park
         *  a window. It reports SAFE once it is actually there, and a supervisor
         *  waits for every program before it acts - dropping control the instant
         *  the operator asked would leave a half-travelled actuator wherever the
         *  request happened to catch it.
         *  @{ */

        /** @brief Ask this program to reach its safe state, or release it. */
        void request_safe(bool on) { _safe_asked = on; }
        bool safe_requested() const { return _safe_asked; }

        /** @brief Turn this program off, or back on. The operator's switch, and
         *  it parks the same actuators the same way: what differs is the state
         *  reported once it is there. */
        void request_disable(bool on) { _disable_asked = on; }
        bool disable_requested() const { return _disable_asked; }

        /** @brief Whether EITHER request stands. What a program's run() tests
         *  to know it should be driving to its fail position. */
        bool inhibit_requested() const { return _safe_asked || _disable_asked; }

        /** @brief Whether it has ARRIVED, not whether it was asked. What a
         *  supervisor waits on before it drops control. */
        bool is_parked() const { return _health == Health::INHIBITED; }

        /** @brief Force the verdict without asking. For the authority gate,
         *  which drops every output to its declared safe state itself: the
         *  plant is out of this program's hands whether or not it agreed. */
        void set_safe() { _health = Health::INHIBITED; }

        /** @} */

    protected:
        /** @name Reporting health, from run()
         *  Worst wins within one activation, so a program can report each
         *  finding as it makes it without tracking which was worst.
         *  @{ */

        /** @brief An input this program wanted is not believable, but what it
         *  produced still is. */
        void degraded() { _worsen(Health::DEGRADED); }
        /** @brief An output could not be produced: this program is not
         *  controlling what it is supposed to control. */
        void faulted()  { _worsen(Health::FAULTED); }

        /** @} */

        /** @brief Report what is true, every activation, in EVERY state.
         *
         *  Status values and alarm conditions belong here rather than in run():
         *  they are observations, not control, and they must survive the
         *  program being parked or switched off. A watch program that only
         *  derives alarms has an empty run() and does all its work here.
         *
         *  @warning Drive only observations. An actuator moved from here would
         *  move while the program is SAFE, which is the one thing that state
         *  promises does not happen. */
        virtual void observe(ProgramContext& c) { (void)c; }

        /** @brief DEGRADED if any watched input is not believable. The first
         *  one is enough - the verdict is a severity, not a count - so a table
         *  of thirty rows costs the walk only while everything is good. */
        void _watch_inputs() {
            if (_io == nullptr) return;
            for (uint8_t i = 0; i < _watched_count; i++) {
                if (!_io->var(_watched[i]).valid()) { degraded(); return; }
            }
        }

        /** @brief Say that this program is still driving toward its parked
         *  position, so it has NOT arrived yet.
         *
         *  Only an activation that calls this counts as travelling. A program
         *  with no actuators of its own is parked the moment it is asked, which
         *  is the default and needs no code.
         *
         *  @warning The travel must terminate. A supervisor waits on every
         *  program, so one that never arrives holds the whole plant out of its
         *  reboot. */
        void parking() { _travelling = true; }
    public:

        /** @brief One entry, PHASE_CONTROL by construction and gated by control
         *  authority. Registering also runs bind(), via Component::setup(). */
        void register_tasks() override;

        /** @} */
        /** @name Accounting
         *  @{ */

        /** @brief Ms past its nominal period on the last activation, and the
         *  worst so far. Always 0 for GROUP_FAST: due every beat, so no slack. */
        uint32_t late_ms() const     { return _late_ms; }
        uint32_t late_max_ms() const { return _late_max_ms; }

        /** @} */

    protected:
        /** @name Hooks (implemented by the program)
         *  @{ */

        /** @brief Resolve this program's variable names, once, at bring-up. */
        virtual void bind(Vars::Bank& io) { (void)io; }

        /** @brief One activation. The only required override. */
        virtual void run(ProgramContext& c) = 0;

        /** @} */
        /** @name Component plumbing
         *  @{ */

        void on_setup(uint64_t now_ms) override {
            (void)now_ms;
            if (_io == nullptr) return;
            // The writer window is open across bind(), so declaring a row as an
            // OUTPUT claims it there and then. Two programs producing one row is
            // found at BRING-UP, where it is a POST failure someone can act on,
            // rather than on whichever beat both first happened to write - which
            // for a row only a fault path drives may be months in.
            _io->begin_writer(_slot);
            bind(*_io);
            _io->end_writer();
        }

        /** @} */

    private:
        /** @brief The supervisor sets `_health_said`; nothing else may. */
        friend void publish_health(Vars::Bank&, const Tasks::Registry&,
                                   Logs::Logger*);

        void _worsen(Health h) { if (h > _health) _health = h; }

        /** @brief Registration handler. A static MEMBER, so the cast reaches
         *  the private activation below. */
        static void _run_task(Component* c, const Tasks::Tick& t);

        void _activate(const Tasks::Tick& t) {
            if (_io == nullptr) return;

            // Against the program's OWN period, not the beat: 40 ms late is
            // fine for a 1 s program and not for a 100 ms one.
            const uint32_t p = Tasks::group_period_ms(_group);
            _late_ms = (t.dt_ms > p) ? (t.dt_ms - p) : 0;
            if (_late_ms > _late_max_ms) _late_max_ms = _late_ms;

            // HEALTHY until this activation says otherwise. A program that never
            // calls degraded()/faulted() is one whose run() cannot fail, and it
            // should read as healthy rather than as unknown.
            _health = Health::HEALTHY;
            // BEFORE run(), so a law reading an unbelievable input starts from
            // DEGRADED and can still escalate to FAULTED on its own account.
            _watch_inputs();
            ProgramContext c = { t.now_ms, t.dt_ms, t.missed, t.first, *_io };
            // The window, not the program, says who is writing: a program cannot
            // name itself, and a write outside an activation claims nothing.
            _io->begin_writer(_slot);
            // run() ALWAYS, inhibited or not. Parking is part of the control
            // law: an actuator program already knows how to drive its valve to a
            // position, and a separate hook would make it own that twice. What
            // changes under a request is the TARGET, which is the program's own
            // business - it reads inhibit_requested() and drives to its fail
            // position instead of to a demand.
            _travelling = false;
            run(c);
            // Arrived unless the program said otherwise. Either request parks
            // the same actuators the same way, so one the operator had already
            // switched off is ALREADY where a reboot needs it.
            if (inhibit_requested() && !_travelling) _health = Health::INHIBITED;
            // ALWAYS, and after the decision so it can report what this beat
            // decided. Saying what is true does not stop because control has:
            // a plant going into its safe state is exactly when an operator is
            // watching, and a status row or an alarm that froze at the moment
            // of the request would be worse than absent - it would be stale and
            // look live.
            observe(c);
            _io->end_writer();
        }
    };

    /** @brief The worst lateness across the registry's programs.
     *  @param worst receives the owner, or null when nothing was late. */
    uint32_t worst_lateness(const Tasks::Registry& r, const Program** worst);

    /** @brief The worst health across the registry's programs, on the SEVERITY
     *  axis alone. INHIBITED is not on it - a parked plant is not a degraded
     *  one - so it is reported separately.
     *  @param inhibited receives how many programs are parked */
    Health worst_health(const Tasks::Registry& r, uint8_t* inhibited);

    /** @brief Publish every program's health to the row it declared, and log
     *  the changes.
     *
     *  Walked by a SUPERVISOR rather than written by each program in its own
     *  activation: a program that is not being activated is exactly the case a
     *  reader most wants the row for, and it cannot write its own row then. */
    void publish_health(Vars::Bank& io, const Tasks::Registry& r,
                        Logs::Logger* logger);

    /** @brief Name a row that has acquired a second author, once per new conflict.
     *  @param said count already reported, so a defect true on every beat is
     *         logged once rather than every 10 ms.
     *  @return whether it logged. */
    bool report_writer_conflicts(Vars::Bank& io, const Tasks::Registry& r,
                                 Logs::Logger* logger, uint16_t& said);


}}  // namespace Botanik::Plc

#endif // PLC_PROGRAM_H
