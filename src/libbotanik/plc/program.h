#ifndef PLC_PROGRAM_H
#define PLC_PROGRAM_H

/**
 * @file program.h
 * @brief Plc::Program - the POU a site writes its control logic in.
 * @note Lifecycle, first-scan semantics and a worked example: docs/dev/scan.md
 */

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"
#include "libbotanik/plc/io.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Plc {

    /** @brief What a program is handed each activation. */
    struct Context {
        uint64_t now_ms;     ///< beat timestamp, same for every program this beat
        uint32_t dt_ms;      ///< REAL ms since this program last ran, never the nominal period
        uint16_t missed;     ///< activations dropped since the last run; 0 normally
        bool     first_scan; ///< first activation at bring-up, or after control returned
        Io       io;         ///< narrowed bank view: read and drive only
    };

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

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief @p name is borrowed and used for the boot announce, the TASKS
         *  row and diagnostics; @p group is the rate class it runs at. */
        Program(const SetupContext& ctx, const char* name, Tasks::GroupId group,
                Vars::Bank& io)
            : Component(ctx, name), _io(&io), _group(group) {}

        /** @brief Registry::add_program()'s to set: two programs claiming one
         *  index would silently share an author. */
        void set_slot(uint8_t slot) { _slot = slot; }
        uint8_t slot() const        { return _slot; }

        Tasks::GroupId group() const { return _group; }

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
        virtual void run(Context& c) = 0;

        /** @} */
        /** @name Component plumbing
         *  @{ */

        void on_setup(uint64_t now_ms) override {
            (void)now_ms;
            if (_io != nullptr) bind(*_io);
        }

        /** @} */

    private:
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

            Context c = { t.now_ms, t.dt_ms, t.missed, t.first, Io(*_io, _slot) };
            run(c);
        }
    };

    /** @brief The worst lateness across the registry's programs.
     *  @param worst receives the owner, or null when nothing was late. */
    uint32_t worst_lateness(const Tasks::Registry& r, const Program** worst);

    /** @brief Name a row that has acquired a second author, once per new conflict.
     *  @param said count already reported, so a defect true on every beat is
     *         logged once rather than every 10 ms.
     *  @return whether it logged. */
    bool report_writer_conflicts(Vars::Bank& io, const Tasks::Registry& r,
                                 Logs::Logger* logger, uint16_t& said);


}}  // namespace Botanik::Plc

#endif // PLC_PROGRAM_H
