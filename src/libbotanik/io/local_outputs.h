#ifndef BOTANIK_IO_LOCAL_OUTPUTS_H
#define BOTANIK_IO_LOCAL_OUTPUTS_H

/**
 * @file local_outputs.h
 * @brief The controller's own output terminals: the on-board relays and the
 *        status LEDs.
 */

#include "libbotanik/core/component.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/vars/model.h"

#if BOTANIK_FEATURE_VARS

namespace Botanik { namespace Io {

    /**
     * @brief Carries every local output row to the pin it named.
     *
     * The mirror image of LocalInputs, and the same rule: local is a Modbus
     * device address of 0, the row's `reg` is the `HAL::Pin` label its table
     * named, and `F_INVERT` is folded in HERE - one place per direction, the
     * way the field bus does it for a remote coil.
     *
     * A DO row and an RO row are the same thing to this class. The scopes
     * differ for the operator, who sees a relay and a lamp as different kinds
     * of thing; the firmware drives a pin either way.
     */
    class LocalOutputs final : public Component {

        /** @name Wiring
         *  @{ */
        Vars::Bank* _io;   ///< borrowed; the firmware owns it
        /** @} */
        /** @name State
         *  @{ */

        /** @brief The most output pins there are: four relays, four LEDs and
         *  the blue one. */
        static const uint8_t PIN_MAX = 9;

        /** @brief The rows to carry, and the pin each drives. */
        uint16_t _row[PIN_MAX];
        uint16_t _pin[PIN_MAX];
        uint8_t  _count = 0;

        /** @brief Local output rows that got no pin. Such a row is DRIVEN BY
         *  NOTHING - the law writes it every beat and the field never moves -
         *  which is the one failure a coil must not have quietly. Counted, and
         *  the site test asserts it is 0. */
        uint8_t  _refused = 0;

        /** @} */

    public:
        LocalOutputs(const SetupContext& ctx, Vars::Bank& io)
            : Component(ctx, "local_out"), _io(&io) {}

        /** @brief Rows carried, and rows that asked for a pin and got none. */
        uint8_t count() const   { return _count; }
        uint8_t refused() const { return _refused; }

        void register_tasks() override;

    protected:
        /** @brief Claim a pin per local output row, and drive each to its
         *  declared safe state before the first law runs. */
        void on_setup(uint64_t now_ms) override;

        /** @brief PHASE_OUTPUT: whatever the laws decided, onto the pins. */
        void on_output(uint64_t now_ms) override;
    };

}}  // namespace Botanik::Io

#endif // BOTANIK_FEATURE_VARS

#endif
