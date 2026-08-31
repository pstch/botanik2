/**
 * @file runtime.h
 * @brief Runs this site's ST programs, one registry task each.
 */

#ifndef BOTANIK_PLC_RUNTIME_RUNTIME_H
#define BOTANIK_PLC_RUNTIME_RUNTIME_H

#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/plc/program.h"

#include "libbotanik/plc/runtime/program.h"

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
            Health   health = Health::HEALTHY;
        };

        Vars::Bank&  _io;
        Vars::VarRef _now_ref;   ///< sys_now_ms
        Slot                  _slot[32];
        uint64_t              _now_ms = 0;

    public:
        Plant(const SetupContext& ctx, Vars::Bank& io)
            : Component(ctx, "st"), _io(io) {}

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

    protected:
        void on_setup(uint64_t now_ms) override;

    };

}}}  // namespace Botanik::Plc::Runtime

#endif
