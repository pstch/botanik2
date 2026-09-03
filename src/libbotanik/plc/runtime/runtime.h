/**
 * @file runtime.h
 * @brief Runs this site's ST programs, one registry task each.
 */

#ifndef BOTANIK_PLC_RUNTIME_RUNTIME_H
#define BOTANIK_PLC_RUNTIME_RUNTIME_H

#include "libbotanik/plc/health.h"
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

        /** @brief Per program, in `programs[]` order. */
        struct Slot {
            Vars::VarRef  health_row;
            Health        health  = Health::HEALTHY;
            /** @brief The OPERATOR's gate, beside the system's. Not retained:
             *  a program left out of the scan by a maintenance session must
             *  not come back silently disabled after a power cut. */
            bool          enabled = true;
            /** @brief Whether the body ran last beat, so the drop to safe
             *  happens on the EDGE rather than every beat of an outage. */
            bool          ran     = false;
            /** @brief This program is TRAVELLING to its parked state: out of
             *  the scan, but still being called, because it asked for the
             *  chance to put its actuators somewhere chosen first. */
            bool          parking = false;
            /** @brief When that stops being allowed to take longer. */
            uint64_t      park_until_ms = 0;
            /** @brief The parking pair, resolved once; VAR_NONE for a program
             *  that does not park. */
            Vars::VarRef  park_row;
            Vars::VarRef  parked_row;
        };

        Vars::Bank&  _io;
        Vars::VarRef _now_ref;   ///< sys_now_ms
        Vars::VarRef _day_ref;   ///< sys_today
        Slot                  _slot[32];
        uint64_t              _now_ms = 0;
        /** @brief The lifecycle, borrowed from the scheduler. Null in a bare
         *  rig, which then runs: a test that constructs a Plant and nothing
         *  else is asking for the laws, not for the unit's life. */
        const Scheduler::RunState* _state = nullptr;
        /** @brief sys_control: the operator's standing choice, for all of them. */
        Vars::VarRef          _control_ref;
        /** @brief plc_parkTimeout_sec: the cap on any one park. */
        Vars::VarRef          _park_timeout_ref;

    public:
        Plant(const SetupContext& ctx, Vars::Bank& io)
            : Component(ctx, "st"), _io(io) {}

        /** @brief Borrow the lifecycle. No program body runs outside RUN. */
        void set_run_state(const Scheduler::RunState* s) { _state = s; }

        void register_tasks() override;

        /** @brief Bind every program, each in its OWN writer window so a row
         *  two of them claim is caught, and each counted separately so one
         *  that missed a name is the only one held back. */
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
         *  drives to its safe state on the next beat. */
        void        set_enabled(uint8_t i, bool on);
        /** @brief Whether @p i would run right now: the operator's gate, the
         *  system's hold and its own health, strictest first. */
        bool        running(uint8_t i) const;
        /** @brief sys_control, or true where the row is absent. */
        bool        control_allowed() const;
        /** @} */

    protected:
        void on_setup(uint64_t now_ms) override;

    };

}}}  // namespace Botanik::Plc::Runtime

#endif // BOTANIK_FEATURE_PLC

#endif
