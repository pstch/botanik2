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
     *  HEALTHY and DEGRADED are a severity pair and are ordered, so a reader
     *  takes the worse of two and one comparison answers "is anything going
     *  wrong". FAULTED sorts above them and is not one of them: it is decided
     *  once and never re-derived.
     *
     *  FAULTED is a program that could not be WIRED. bind() named a row no
     *  table declares, or a dependency it was handed is null. Nothing about it
     *  gets better, so it is set during setup and never again, and the program
     *  is never activated - no run(), no observe(). One that ran on inert
     *  sinks would publish zeros as though they were readings.
     *
     *  INHIBITED and PARKING are off the severity axis: the program's CONTROL
     *  has stopped, but it is still reporting - status values and alarms keep
     *  being published, which is the whole reason the states are named for
     *  what stops rather than for the program being off. They are REACHED
     *  rather than commanded: an actuator takes time to park, PARKING is the
     *  travel and INHIBITED is arrival, and the program keeps running through
     *  both.
     *
     *  Two causes, one pair of states. A plant-wide safe request and the
     *  operator's per-program switch inhibit the same things in the same way;
     *  which one asked is safe_requested() / disable_requested(), not a second
     *  value every reader would have to treat alike.
     *
     *  @note The codes are published as a T_ENUM row, so the vocabulary may
     *  gain labels and must never renumber them. */
    enum class Health : uint8_t {
        HEALTHY = 0,   ///< normal operation, every output valid
        DEGRADED,      ///< an input is invalid; the outputs are still driven
        FAULTED,       ///< could not be wired at bring-up; never runs
        INHIBITED,     ///< control stopped and actuators parked; still reporting
        PARKING        ///< control stopped, actuators still travelling
    };

    /** @brief How many values Health has, so a per-state tally can be sized. */
    constexpr uint8_t HEALTH_COUNT = 5;

    /** @brief A health CODE as a word: the converter a status row over this
     *  vocabulary takes the address of.
     *  @note Null for anything that is not one of the five, which is what a
     *  T_ENUM row refuses. @see health_name() for the typed form. */
    inline const char* health_label(uint64_t code) {
        switch (code) {
            case (uint64_t)Health::HEALTHY:   return "HEALTHY";
            case (uint64_t)Health::DEGRADED:  return "DEGRADED";
            case (uint64_t)Health::FAULTED:   return "FAULTED";
            case (uint64_t)Health::INHIBITED: return "INHIBITED";
            case (uint64_t)Health::PARKING:   return "PARKING";
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
            case Health::PARKING:   return "PARKING";
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
        /** @brief Whether setup decided this program cannot be wired. Latches
         *  _health at FAULTED and stops it ever being activated. */
        bool           _faulted       = false;
        /** @brief Where health is published, and what was last LOGGED there.
         *  0xFF is "nothing yet", so the first pass announces whatever it
         *  finds rather than only a change from HEALTHY. */
        Vars::RefOut<uint8_t> _health_row;
        uint8_t        _health_said   = 0xFF;
        /** @brief The rows _bind_input()/_bind_inout() resolved, in declaration
         *  order. Recorded for the "who reads this row" query and nothing else:
         *  a law that wants an input's validity asks the row itself. Truncated
         *  at the cap, which _input_overflow says. */
        Vars::VarRef _inputs[Config::PROGRAM_MAX_INPUTS];
        uint8_t      _input_count    = 0;
        bool         _input_overflow = false;

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
         *  Set from bind() by a program that declares a health row. The
         *  program does not write it itself: it is written by a
         *  supervisor walking every program, so a program that is not being
         *  activated - which is most of what "inhibited" means - still has a
         *  current row. @see publish_health() */
        void set_health_row(Vars::RefOut<uint8_t> r) { _health_row = r; }

        /** @brief The rows this program reads, for a reader asking which
         *  programs consume one. Empty until bind() has run. */
        const Vars::VarRef* inputs() const { return _inputs; }
        uint8_t             input_count() const { return _input_count; }
        /** @brief Whether more inputs were bound than fit, so inputs() is a
         *  prefix rather than the whole list. */
        bool input_overflow() const { return _input_overflow; }

        Vars::RefOut<uint8_t> health_row() const { return _health_row; }

        /** @brief Registry::add_program()'s to set: two programs claiming one
         *  index would silently share an author. */
        void set_slot(uint8_t slot) { _slot = slot; }
        uint8_t slot() const        { return _slot; }

        Tasks::GroupId group() const { return _group; }

        /** @} */

        /** @name Health
         *  What this program could do on its last activation. Re-derived each
         *  time, so a condition that clears is not sticky - a program that stops
         *  running keeps its last verdict, which is why withdrawal sets SAFE.
         *  FAULTED is the exception: setup decides it, and nothing re-derives it.
         *  @{ */

        Health health() const { return _health; }

        /** @brief Whether setup found this program unwireable. It is never
         *  activated, so a reader asking why nothing is happening asks here. */
        bool is_faulted() const { return _faulted; }

        /** @} */

        /** @name Going safe
         *
         *  INHIBITED is REACHED, not commanded. A request tells a program to put
         *  its own actuators where they belong; it keeps being activated while
         *  it does, because a program that is not run cannot close a valve or
         *  park a window. It reads PARKING for as long as it is travelling and
         *  INHIBITED once it is there, and a supervisor waits for every program
         *  before it acts - dropping control the instant the operator asked
         *  would leave a half-travelled actuator wherever the request happened
         *  to catch it.
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
         *  supervisor waits on before it drops control.
         *
         *  A FAULTED program counts: it is never activated, so it can neither
         *  travel nor arrive, and waiting on one would hold the whole plant out
         *  of its reboot for ever. */
        bool is_parked() const {
            return _health == Health::INHIBITED || _health == Health::FAULTED;
        }

        /** @brief Force the verdict without asking. For the authority gate,
         *  which drops every output to its declared safe state itself: the
         *  plant is out of this program's hands whether or not it agreed. */
        void set_safe() { if (!_faulted) _health = Health::INHIBITED; }

        /** @} */

    protected:
        /** @name Reporting health
         *  @{ */

        /** @brief From run(): an input this program wanted is not believable,
         *  but what it produced still is. Cleared by the next activation. */
        void degraded() { if (_health == Health::HEALTHY) _health = Health::DEGRADED; }

        /** @brief From bind(), and only from there: this program cannot be
         *  wired, so it must never run.
         *
         *  For a dependency that is null rather than a name that is missing -
         *  an unresolved name is caught by _bind_input() and its two siblings
         *  without the program asking. Permanent either way. */
        void fault() { _faulted = true; }

        /** @} */

        /** @name Binding
         *
         *  What a bind() resolves its names through. Each checks what the
         *  declaration promised - that the name exists, that the row's type is
         *  one the reference can carry, and for an output that no other program
         *  has already claimed it - logs whichever failed, and FAULTS. A
         *  program bound to an inert sink runs a law on zeros and publishes
         *  them as readings, which is the one outcome worth refusing to start
         *  for.
         *
         *  Validity is NOT one of the checks: a row that is merely not
         *  believable right now is the law's business, tested in run() or
         *  observe() where the law knows what it can do without it.
         *  @{ */

        template <typename T> Vars::RefIn<T> _bind_input(Vars::Bank& io, const char* name) {
            _bind_check(io, name, BIND_IN, &Vars::RefIo<T>::accepts);
            return io.resolve_in<T>(name);
        }
        template <typename T> Vars::RefOut<T> _bind_output(Vars::Bank& io, const char* name) {
            _bind_check(io, name, BIND_OUT, &Vars::RefIo<T>::accepts);
            return io.resolve_out<T>(name);
        }
        template <typename T> Vars::RefInOut<T> _bind_inout(Vars::Bank& io, const char* name) {
            _bind_check(io, name, BIND_INOUT, &Vars::RefIo<T>::accepts);
            return io.resolve_inout<T>(name);
        }

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
            // OUTPUT claims it there and then, and _bind_output() refuses one
            // another program already holds. Both belong at BRING-UP, where a
            // POST failure is something someone can act on.
            //
            // The count is the BACKSTOP under the three wrappers: a bind() that
            // reached the bank directly gets no message naming the row, but a
            // program bound to the inert sink still must not run - it would
            // publish zeros indistinguishable from readings.
            const uint16_t missed = _io->unresolved_count();
            _io->begin_writer(_slot);
            bind(*_io);
            _io->end_writer();
            if (_io->unresolved_count() != missed) fault();
            // The one place FAULTED is decided. It outranks the INHIBITED a
            // program starts at, and no activation ever revisits it.
            if (_faulted) _health = Health::FAULTED;
        }

        /** @} */

    private:
        /** @brief Which of the three wrappers is asking. Only the direction the
         *  checks differ on: an output is the one that can be taken. */
        enum BindDir : uint8_t { BIND_IN, BIND_OUT, BIND_INOUT };

        /** @brief The three checks, run before the resolve that follows. Faults
         *  and logs on the first one that fails.
         *  @param accepts RefIo<T>::accepts, passed as a function rather than a
         *         template parameter so the body stays out of this header and
         *         off every program's include of the logger. */
        void _bind_check(Vars::Bank& io, const char* name, BindDir dir,
                         bool (*accepts)(Vars::VarType));

        /** @brief The supervisor sets `_health_said`; nothing else may. */
        friend void publish_health(Vars::Bank&, const Tasks::Registry&,
                                   Logs::Logger*, bool);

        /** @brief Registration handler. A static MEMBER, so the cast reaches
         *  the private activation below. */
        static void _run_task(Component* c, const Tasks::Tick& t);

        void _activate(const Tasks::Tick& t) {
            if (_io == nullptr) return;
            // Nothing at all for a program setup could not wire: not run(), not
            // observe(), not even the lateness. Its refs are inert sinks, so
            // every value it produced would be a zero indistinguishable from a
            // reading, and its verdict stands from setup for the life of the run.
            if (_faulted) return;

            // Against the program's OWN period, not the beat: 40 ms late is
            // fine for a 1 s program and not for a 100 ms one.
            const uint32_t p = Tasks::group_period_ms(_group);
            _late_ms = (t.dt_ms > p) ? (t.dt_ms - p) : 0;
            if (_late_ms > _late_max_ms) _late_max_ms = _late_ms;

            // HEALTHY until this activation says otherwise. A program that never
            // calls degraded() is one whose run() cannot fail, and it should
            // read as healthy rather than as unknown.
            _health = Health::HEALTHY;
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
            // Still travelling, or arrived. Either request parks the same
            // actuators the same way, so one the operator had already switched
            // off is ALREADY where a reboot needs it. Both outrank whatever the
            // law reported: a program that has stopped controlling is not a
            // degraded one, and a reader asking why the plant is still says so.
            if (inhibit_requested())
                _health = _travelling ? Health::PARKING : Health::INHIBITED;
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

    /** @brief Publish every program's health to the row it declared, and log
     *  the changes.
     *
     *  Walked by a SUPERVISOR rather than written by each program in its own
     *  activation: a program that is not being activated is exactly the case a
     *  reader most wants the row for, and it cannot write its own row then.
     *  @param quiet suppress the log during bring-up. FAULTED is said anyway -
     *         setup is the only place it is reached. */
    void publish_health(Vars::Bank& io, const Tasks::Registry& r,
                        Logs::Logger* logger, bool quiet);

    /** @brief Name a row that has acquired a second author, once per new conflict.
     *  @param said count already reported, so a defect true on every beat is
     *         logged once rather than every 10 ms.
     *  @return whether it logged. */
    bool report_writer_conflicts(Vars::Bank& io, const Tasks::Registry& r,
                                 Logs::Logger* logger, uint16_t& said);


}}  // namespace Botanik::Plc

#endif // PLC_PROGRAM_H
