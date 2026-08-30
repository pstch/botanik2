/**
 * @file st_runtime.h
 * @brief The component that runs this site's ST, in place of Plc::Program.
 */

#ifndef BOTANIK_SITE_ST_RUNTIME_H
#define BOTANIK_SITE_ST_RUNTIME_H

#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/vars/model.h"

namespace Site {

    /** @brief Drives the generated resources, one task per rate group.
     *
     *  matiec generates the binding (`__INIT_EXTERNAL`), the law (`X_body__`)
     *  and the dispatch (`X_run__`). What is left is calling each resource at
     *  its group's rate and telling the ST how long the beat was.
     */
    class StRuntime final : public Botanik::Component {

        Botanik::Vars::Bank&  _io;
        Botanik::Vars::VarRef _dt_ref;    ///< sys_now_ms
        bool _wired = false;

    public:
        StRuntime(const Botanik::SetupContext& ctx, Botanik::Vars::Bank& io)
            : Component(ctx, "st"), _io(io) {}

        /** @brief True once the plant wired with no unresolved name. A plant
         *  that could not be wired must not drive the field. */
        bool wired() const { return _wired; }

        /** @brief Registers one entry per RESOURCE. Public, because the site
         *  calls it beside every program's. */
        void register_tasks() override;

        /** @brief Run the SLOW resource once, with the clock at @p now_ms.
         *  The beat calls this through the registry; a test calls it directly,
         *  because ST has no program object to hold a rig open on. */
        void scan_slow(uint64_t now_ms);

        /** @brief Bind the plant. Called by `on_setup()`; public so a test can
         *  wire without standing a whole firmware up. */
        void wire();

    protected:
        /** @brief The plant's binding: `SLOW_init__()` reaches every POU's at
         *  once, so it runs in ONE writer window. */
        void on_setup(uint64_t now_ms) override;

    private:
        /** @brief One per RESOURCE. Publishes this group's ACTUAL elapsed into
         *  `sys_now_ms`, then runs the group's bodies.
         *  @note The tick passed on is always 0: every resource declares the
         *  base interval and compiles to `tick % 1`, so the RATE is this
         *  registry entry's, not the ST's. Declaring it in both places is what
         *  the single interval avoids. */
        static void _run_slow(Botanik::Component* self,
                              const Botanik::Tasks::Tick& t);
    };

}  // namespace Site

#endif
