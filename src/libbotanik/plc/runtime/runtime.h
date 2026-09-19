/**
 * @file runtime.h
 * @brief Runs this site's ST programs, one registry task each.
 */

#ifndef BOTANIK_PLC_RUNTIME_RUNTIME_H
#define BOTANIK_PLC_RUNTIME_RUNTIME_H

#include "libbotanik/core/health.h"
#include "libbotanik/core/component.h"
#include "libbotanik/config/base.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/model.h"

#include "libbotanik/plc/runtime/program.h"

#if BOTANIK_FEATURE_PLC

namespace Botanik { namespace Plc { namespace Runtime {

    /** @brief One `Tasks::Registry` entry per ST program.
     *
     *  matiec generates the binding, the law and a resource that would
     *  dispatch them as a fixed sequence. The sequence is what we do NOT use:
     *  calling each body ourselves is what keeps a program a program - its own
     *  period, its own writer slot, its own health row, its own place in the
     *  TASKS table, and its own verdict when a name it wanted does not resolve.
     */
    class Plant final : public Component {

        /** @brief Per program, in `programs[]` order. Everything a program is
         *  DECLARED with - its rows, its period - is in `Program`; this is what
         *  it acquires while running. */
        struct Slot {
            /** @brief The law's last verdict, read back off its row after
             *  each scan. Cached because the TASKS page holds a POINTER.
             *  HEALTHY is what a program with no health row keeps: it did not
             *  opt in, so nothing here has an opinion, and its task entry gets
             *  a null pointer rather than this. */
            Health   health  = Health::HEALTHY;
            /** @brief Whether the body ran last beat, so the drop to safe
             *  happens on the EDGE rather than every beat of an outage. */
            bool     ran     = false;
            /** @brief This program is TRAVELLING to its parked state: out of
             *  the scan, but still being called, because it asked for the
             *  chance to put its actuators somewhere chosen first. */
            bool     parking = false;
            /** @brief When that stops being allowed to take longer. */
            uint64_t park_until_ms = 0;
        };

        Vars::Bank& _io;
        Slot        _slot[32];
        uint64_t    _now_ms = 0;
        /** @brief The lifecycle, borrowed from the scheduler. Null in a bare
         *  rig, which then runs: a test that constructs a Plant and nothing
         *  else is asking for the laws, not for the unit's life. */
        const Scheduler::RunState* _state = nullptr;

    public:
        Plant(const SetupContext& ctx, Vars::Bank& io)
            : Component(ctx, "st"), _io(io) {}

        /** @brief Borrow the lifecycle. No program body runs outside RUN. */
        void set_run_state(const Scheduler::RunState* s) { _state = s; }

        void register_tasks() override;

        /** @brief Point the ST runtime at this bank and run every generated
         *  bind. A binding is a generated index, so there is nothing here that
         *  can fail. */
        void wire();

        /** @brief Run program @p i, with the clock at @p now_ms. The beat
         *  calls this through the registry; a test calls it directly. */
        void scan(uint8_t i, uint64_t now_ms);

        /** @brief Run every program in roster order. What a test wants when it
         *  does not care which program did what. */
        void scan(uint64_t now_ms);

        Health health(uint8_t i) const;

        /** @brief Is any program still TRAVELLING to its parked state?
         *
         *  What holds the lifecycle in STOP: a roof that has been told to shut
         *  is worth a few seconds of the way out. Bounded by
         *  plc_parkTimeout_sec per program, so a ram that has seized cannot
         *  hold the unit up. False the moment the last one answers. */
        bool parking() const;

    private:
        /** @brief Begin, end, and give up on @p i's park. Split out because
         *  the three of them are the whole state machine and scan() should
         *  read as the beat, not as it. */
        /** @brief Take @p i's verdict off its row into the slot the TASKS page
         *  points at. No row, no change. */
        void _read_health(uint8_t i);
        bool _park_begin(uint8_t i, uint64_t now_ms);
        void _park_end(uint8_t i, bool answered);
        /** @brief _safe_outputs() plus the line that says it happened. */
        void _drop_safe(uint8_t i);
        /** @brief plc_parkTimeout_sec in ms, or the built-in floor where there
         *  is no table - a bare rig runs the laws, not the lifecycle. */
        uint32_t _park_timeout_ms() const;
        /** @brief Drop what @p i drives to its declared safe state.
         *  @return how many rows were dropped. */
        uint16_t _safe_outputs(uint8_t i);

    public:

        /** @name The scan roster, for the console
         *  There is no Plc::Program behind an ST program, so what wants to
         *  list them asks HERE rather than walking the registry.
         *  @{ */
        uint8_t     count() const;
        const char* name(uint8_t i) const;
        /** @brief Whether @p i is in the scan: the operator's gate alone, not
         *  the system's. @see running() */
        bool        enabled(uint8_t i) const;
        /** @brief Put @p i in or out of the scan. Taking it out drops what it
         *  drives to its safe state on the next beat. Live at once; durable
         *  only once gate(i) is saved. */
        void        set_enabled(uint8_t i, bool on);
        /** @brief `prog_<name>_run`, the parameter the gate IS. What
         *  `programs disable` hands to Vars::Persist. The sink for an index
         *  off the roster. */
        Vars::Var   gate(uint8_t i) const;
        /** @brief Whether @p i would run right now: the operator's gate and
         *  the system's hold, stricter first.
         *  @note Health is NOT one of them. A FAULTED program keeps being
         *  scanned, because what it writes when it cannot answer IS its safe
         *  state; dropping it would leave its rows wherever they stood. */
        bool        running(uint8_t i) const;
        /** @brief sys_control, or true where the row is absent. */
        bool        control_allowed() const;
        /** @brief One bit per program, set where running(). A supervisor
         *  watching for a change of the scan roster compares this against its
         *  own last reading; PROGRAM_COUNT is asserted <= 32 in wire(). */
        uint32_t    running_mask() const;
        /** @} */

    protected:
        void on_setup(uint64_t now_ms) override;

    };

}}}  // namespace Botanik::Plc::Runtime

#endif // BOTANIK_FEATURE_PLC

#endif
