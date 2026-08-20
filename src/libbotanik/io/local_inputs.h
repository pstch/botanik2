#ifndef IO_LOCAL_INPUTS_H
#define IO_LOCAL_INPUTS_H

/**
 * @file local_inputs.h
 * @brief LocalInputs - the controller's own input terminals, published as rows.
 * @note The wiring rules and the channel map: docs/dev/io.md
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/core/component.h"
#include "libbotanik/hal/platform.h"
#if BOTANIK_FEATURE_VARS
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Io {

    /**
     * @brief Publishes every XP_LOCAL digital row from the PLC's own terminals.
     *
     * What the RTU client is to the field bus, this is to the wiring that lands
     * on the controller: a program reads a contact by NAME, and nothing above
     * it knows which side of the boundary the bit came from. Active-low wiring
     * is folded in here, so a row reads true = asserted whatever the contact
     * does.
     *
     * The rows are found once, at bring-up, by walking the table for
     * `XP_LOCAL` + `SCOPE_DI`. There is no channel table to keep in step with
     * the variable table, because the variable table IS it. An `F_PULSE` row is
     * skipped: that terminal is a counter, attached once by whoever owns the
     * meter.
     */
    class LocalInputs : public Component {

        /** @name Wiring
         *  @{ */

        Vars::Bank* _io;   ///< borrowed; the firmware owns it

        /** @} */
        /** @name State
         *  @{ */

        /** @brief The rows to publish, and the terminal each reads. Bounded by
         *  the terminal count: two rows on one channel is a table defect the
         *  generator already refuses. */
        uint16_t _row[HAL::DigitalIn::CHANNELS];
        uint8_t  _channel[HAL::DigitalIn::CHANNELS];
        uint8_t  _count = 0;

        /** @} */

    public:
        LocalInputs(const SetupContext& ctx, Vars::Bank& io)
            : Component(ctx, "local_in"), _io(&io) {}

        /** @brief How many terminals this build actually reads. */
        uint8_t count() const { return _count; }

        /** @brief One entry, PHASE_INPUT: a terminal has to be in the bank
         *  before the programs run on it. */
        void register_tasks() override;

    protected:
        /** @brief Find the rows and configure their terminals. */
        void on_setup(uint64_t now_ms) override;
        void on_input(uint64_t now_ms) override;

    private:
        /** @brief Registration handler. A static MEMBER, so it reaches the
         *  protected hook. */
        static void _input_task(Component* c, const Tasks::Tick& t);
    };

}}  // namespace Botanik::Io

#endif // BOTANIK_FEATURE_VARS

#endif // IO_LOCAL_INPUTS_H
