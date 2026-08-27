#ifndef PLC_PROGRAM_H
#define PLC_PROGRAM_H

/**
 * @file program.h
 * @brief Plc::Program - the POU a site writes its control logic in.
 * @note Lifecycle, first-scan semantics and a worked example: docs/dev/scan.md
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/plc/mode.h"
#include "libbotanik/core/context.h"   // ProgramContext
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Plc {

    /** @brief What a program can do right now.
     *
     *  HEALTHY and DEGRADED are a severity pair and are ordered, so a reader
     *  takes the worse of two and one comparison answers "is anything going
     *  wrong". INVALID sorts above them and is not one of them: it is decided
     *  once and never re-derived.
     *
     *  INVALID is a program that could not be WIRED. bind() named a row no
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
        INVALID,       ///< could not be wired at bring-up; never runs
        INHIBITED,     ///< the law stopped, the outputs stand where it left them
        PARKING,       ///< on the way to one of the two below
        SAFE,          ///< the law stopped and the outputs are at their safe state
        DISABLED       ///< the same, and it has stopped reporting too
    };

    /** @brief How many values Health has, so a per-state tally can be sized. */
    constexpr uint8_t HEALTH_COUNT = 7;

    /** @brief A health CODE as a word: the converter a status row over this
     *  vocabulary takes the address of.
     *  @note Null for anything that is not one of the five, which is what a
     *  T_ENUM row refuses. @see health_name() for the typed form. */
    inline const char* health_label(uint64_t code) {
        switch (code) {
            case (uint64_t)Health::HEALTHY:   return "HEALTHY";
            case (uint64_t)Health::DEGRADED:  return "DEGRADED";
            case (uint64_t)Health::INVALID:   return "INVALID";
            case (uint64_t)Health::INHIBITED: return "INHIBITED";
            case (uint64_t)Health::PARKING:   return "PARKING";
            case (uint64_t)Health::SAFE:      return "SAFE";
            case (uint64_t)Health::DISABLED:  return "DISABLED";
            default:                          return nullptr;
        }
    }

    /** @brief @p h as a word, for the TASKS page and the logs. The typed form
     *  of health_label(), so the two vocabularies cannot drift. */
    inline const char* health_name(Health h) {
        const char* n = health_label((uint64_t)h);
        return (n != nullptr) ? n : "?";
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
        /** @brief DERIVED, never assigned from outside: refresh() computes it
         *  from the mode and the flags below. Cached because a program that is
         *  not being activated still needs a current verdict.
         *
         *  INHIBITED until the first activation: before the scan runs at all a
         *  program is not controlling anything, and reading HEALTHY there would
         *  tell a supervisor the plant was in hand during bring-up. */
        Health         _health = Health::INHIBITED;
        /** @name The three asks
         *  Weighed by mode(), strictest wins. Independent by construction: the
         *  operator's standing choice, the plant's, and the law's own.
         *  @{ */
        Mode           _op_mode    = Mode::NORMAL;
        bool           _plant_safe = false;
        bool           _self_safe  = false;
        /** @} */
        /** @brief Set by parking() during one activation: the program is still
         *  driving toward its fail position. */
        bool           _travelling    = false;
        /** @brief Whether it has ARRIVED. Set on an activation that did NOT
         *  say parking(); cleared whenever the mode is back above SAFE, so a
         *  program asked again parks from scratch. */
        bool           _parked        = false;
        /** @brief Whether this activation was short of an input. Its own flag
         *  rather than a Health value, because the verdict is derived and
         *  DEGRADED has to outrank the state it may have caused. */
        bool           _degraded      = false;
        /** @brief Whether setup decided this program cannot be wired. Latches
         *  _health at INVALID and stops it ever being activated. */
        bool           _invalid       = false;
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
         *  INVALID is the exception: setup decides it, and nothing re-derives it.
         *  @{ */

        Health health() const { return _health; }

        /** @brief Whether setup found this program unwireable. It is never
         *  activated, so a reader asking why nothing is happening asks here. */
        bool is_invalid() const { return _invalid; }

        /** @} */

        /** @name The mode ladder
         *
         *  Four rungs, each taking away one more thing, weighed from three
         *  independent asks with the STRICTEST winning. @see plc/mode.h
         *
         *  A rung is REACHED, not commanded. Parking is work - a valve has
         *  travel, a window has travel - so a program asked to go safe keeps
         *  being activated while it drives there, reads PARKING for as long as
         *  it is travelling, and only then reports where it arrived. A
         *  supervisor waits on every program before it acts: dropping control
         *  the instant the operator asked would leave a half-travelled actuator
         *  wherever the request happened to catch it.
         *  @{ */

        /** @brief The rung in force: the strictest of the three asks. */
        Mode mode() const {
            const Mode plant = _plant_safe ? Mode::SAFE : Mode::NORMAL;
            const Mode self  = _self_safe  ? Mode::SAFE : Mode::NORMAL;
            return stricter(_op_mode, stricter(plant, self));
        }

        /** @brief The operator's standing choice for this program, which is
         *  what @c programs sets and what is persisted. Enacts itself: there is
         *  no set-then-refresh protocol for a caller to forget. */
        void set_operator_mode(Mode m) { _op_mode = m; refresh(); }
        Mode operator_mode() const     { return _op_mode; }

        /** @brief The PLANT's ask, raised by the registry - a safe request, or
         *  control authority being withheld, which are the same thing to a
         *  program. Enacts itself for the same reason. */
        void hold_safe(bool on) { _plant_safe = on; refresh(); }

        /** @brief Whether the outputs are to end up at their safe state. What a
         *  site law tests; it never names a Mode, so a fifth rung stays a
         *  one-line edit here rather than N edits across the site. */
        bool going_safe() const { return mode() <= Mode::SAFE; }

        /** @brief Whether run() is called this activation: at NORMAL always,
         *  and below it only while there is still travel to do. */
        bool runs() const {
            return mode() == Mode::NORMAL || (going_safe() && !_parked);
        }

        /** @brief Whether the dispatcher activates it at all. A DISABLED
         *  program that has arrived has stopped reporting too. */
        bool wants_activation() const {
            return !_invalid && !(mode() == Mode::DISABLED && _parked);
        }

        /** @brief Whether it has ARRIVED, not whether it was asked. What a
         *  supervisor waits on before it drops control.
         *
         *  An INVALID program counts: it is never activated, so it can neither
         *  travel nor arrive, and waiting on one would hold the whole plant out
         *  of its reboot for ever. So does one at a rung it is not travelling
         *  away from. */
        bool is_parked() const { return _invalid || !going_safe() || _parked; }

        /** @} */

    protected:
        /** @name Reporting health
         *  @{ */

        /** @brief From run() or observe(): an input this program wanted is not
         *  believable, but what it produced still is. Cleared by the next
         *  activation. Outranks the state it may have caused - "short of an
         *  input" is the answer to the question an operator is really asking. */
        void degraded() { _degraded = true; }

        /** @brief From observe(): this law cannot command at all, so ask for
         *  the safe rung on its own account. Released by simply not saying it
         *  again, which is why it is cleared before every activation.
         *
         *  @warning State it from observe(), never from run(). observe() runs
         *  in every rung a program can come back from; run() is what STOPS once
         *  it has parked, so a law that only said this from there could never
         *  take it back. */
        void go_safe(bool blind) { if (blind) _self_safe = true; }

        /** @brief From bind(), and only from there: this program cannot be
         *  wired, so it must never run.
         *
         *  For a dependency that is null rather than a name that is missing -
         *  an unresolved name is caught by _bind_input() and its two siblings
         *  without the program asking. Permanent either way. */
        void invalidate() { _invalid = true; }

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
            if (_io->unresolved_count() != missed) invalidate();
            // The one place INVALID is decided. It outranks the INHIBITED a
            // program starts at, and no activation ever revisits it.
            if (_invalid) _health = Health::INVALID;
        }

        /** @} */

    private:
        /** @brief Re-derive the verdict. The ONE place _health is assigned.
         *
         *  Ordered by what an operator is asking when they read it: a wiring
         *  fault outranks everything, travel outranks the rung it travels to,
         *  and being short of an input outranks the state that shortage caused
         *  - a program parked BECAUSE it went blind reads DEGRADED, not SAFE. */
        void refresh() {
            const Mode m = mode();
            if (m > Mode::SAFE) _parked = false;   // asked again, park afresh
            _health = _invalid                  ? Health::INVALID
                    : (m <= Mode::SAFE && !_parked) ? Health::PARKING
                    : (m == Mode::DISABLED)     ? Health::DISABLED
                    : (m == Mode::INHIBITED)    ? Health::INHIBITED
                    : _degraded                 ? Health::DEGRADED
                    : (m == Mode::SAFE)         ? Health::SAFE
                                                : Health::HEALTHY;
        }

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
            if (_invalid) return;

            // Against the program's OWN period, not the beat: 40 ms late is
            // fine for a 1 s program and not for a 100 ms one.
            const uint32_t p = Tasks::group_period_ms(_group);
            _late_ms = (t.dt_ms > p) ? (t.dt_ms - p) : 0;
            if (_late_ms > _late_max_ms) _late_max_ms = _late_ms;

            ProgramContext c = { t.now_ms, t.dt_ms, t.missed, t.first, *_io };
            // The window, not the program, says who is writing: a program cannot
            // name itself, and a write outside an activation claims nothing.
            _io->begin_writer(_slot);

            // SAMPLED BEFORE THE RESET below, which clears the law's own ask.
            // Reading it after would forget that the law is blind and run it on
            // the inputs it just disowned.
            const bool ran = runs();
            _degraded = _self_safe = _travelling = false;

            // run() while there is anything to drive TO. Parking is part of the
            // control law: an actuator program already knows how to take its
            // valve to a position, and a separate hook would make it own that
            // twice. What changes below NORMAL is the TARGET, which the law
            // reads off going_safe() and drives to instead of a demand.
            if (ran) run(c);

            // ALWAYS, and after run() so it reports what this beat decided.
            // Saying what is true does not stop because control has: a plant
            // going into its safe state is exactly when an operator is
            // watching, and a status row that froze at the moment of the
            // request would be worse than absent - stale, and looking live.
            // It is also where go_safe() is stated, so the ask below is this
            // beat's rather than the one before it.
            observe(c);

            // Arrived, unless this activation said otherwise. Only an
            // activation that ACTUALLY RAN can settle it: one that was skipped
            // because the program had already parked must not un-park it.
            if (ran) _parked = !_travelling;
            refresh();
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
     *  @param quiet suppress the log during bring-up. INVALID is said anyway -
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
