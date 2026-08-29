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
     *  Two answers, because there are only two questions worth asking of a
     *  program: is it wired, and is it running. Anything finer belongs to the
     *  ROWS - a reading that is not believable says so in its own quality, and
     *  every consumer already reads that.
     *
     *  @note The codes are published as a T_ENUM row, so the vocabulary may
     *  gain labels and must never renumber them. INVALID keeps the code it has
     *  always had. */
    enum class Health : uint8_t {
        HEALTHY = 0,   ///< wired, and running
        INVALID = 2    ///< could not be wired at bring-up; never runs
    };

    /** @brief A health CODE as a word: the converter a status row over this
     *  vocabulary takes the address of.
     *  @note Null for anything that is not one of the two, which is what a
     *  T_ENUM row refuses. @see health_name() for the typed form. */
    inline const char* health_label(uint64_t code) {
        switch (code) {
            case (uint64_t)Health::HEALTHY: return "HEALTHY";
            case (uint64_t)Health::INVALID: return "INVALID";
            default:                        return nullptr;
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
        /** @brief HEALTHY, or INVALID once setup decides it cannot be wired. */
        Health         _health  = Health::HEALTHY;
        /** @brief Whether setup found this program unwireable. Latches _health
         *  at INVALID and stops it ever being activated. */
        bool           _invalid = false;
        /** @brief Where health is published, and what was last LOGGED there.
         *  0xFF is "nothing yet", so a program that comes up INVALID is said
         *  once rather than never. */
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
         *  program does not write it itself: it is written by a supervisor
         *  walking every program, so one that is never activated still has a
         *  current row - which is exactly the INVALID case a reader wants it
         *  for. @see publish_health() */
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
         *  Whether this program was WIRED. Decided once, in setup, and never
         *  re-derived: everything that could change beat to beat is a property
         *  of the ROWS, and carries its own quality.
         *  @{ */

        Health health() const { return _health; }

        /** @brief Whether setup found this program unwireable. It is never
         *  activated, so a reader asking why nothing is happening asks here. */
        bool is_invalid() const { return _invalid; }

        /** @} */

    protected:
        /** @name Reporting health
         *  @{ */

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
         *  has already claimed it - logs whichever failed, and INVALIDATES.
         *  A program bound to an inert sink runs a law on zeros and publishes
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

    public:

        /** @brief One entry, PHASE_CONTROL by construction and gated by control
         *  authority. Registering also runs bind(), via Component::setup(). */
        void register_tasks() override;

        /** @name Accounting
         *  @{ */

        /** @brief Ms past its nominal period on the last activation. Always 0
         *  for GROUP_FAST: due every beat, so there is no slack to be late by. */
        uint32_t late_ms() const     { return _late_ms; }

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
                                   Logs::Logger*);

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

            ProgramContext c = { t.now_ms, t.dt_ms, t.missed, t.first, *_io };
            // The window, not the program, says who is writing: a program cannot
            // name itself, and a write outside an activation claims nothing.
            _io->begin_writer(_slot);

            run(c);

            // ALWAYS, and after run() so it reports what this beat decided.
            // Status values and alarm conditions belong here: they are
            // observations, not control, and a watch program that only derives
            // alarms has an empty run() and does all its work in observe().
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
     *  @note ONLY an INVALID one is logged: a program being wired is the
     *  normal case, and thirty such lines at boot say nothing thirty times. */
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
