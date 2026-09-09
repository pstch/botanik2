#ifndef IO_LOCAL_INPUTS_H
#define IO_LOCAL_INPUTS_H

/**
 * @file local_inputs.h
 * @brief LocalInputs - the controller's own input terminals, published as rows.
 * @note The wiring rules and the channel map: @ref dev_io
 */

#include <cstdint>

#include "libbotanik/config/base.h"
#include "libbotanik/core/component.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Io {

    /** @brief Publishes every local input row from the PLC's own terminals.
     *
     *  `F_INVERT` is folded in here, so a row reads true = asserted whatever
     *  the contact does. @see @ref dev_io */
    class LocalInputs : public Component {

        /** @name Wiring
         *  @{ */

        Vars::Bank* _io;   ///< borrowed; the firmware owns it

        /** @} */
        /** @name State
         *  @{ */

        /** @brief The most level-read rows there can be: the eight terminals
         *  and the button. */
        static const uint8_t LEVEL_MAX = HAL::DigitalIn::CHANNELS + 1;

        /** @brief The rows to publish, and the PIN each reads. Bounded by the
         *  terminal count: two rows on one pin would both attach and both
         *  publish, which the site test refuses. */
        uint16_t _row[LEVEL_MAX];
        uint16_t _pin[LEVEL_MAX];
        uint8_t  _count = 0;

        /** @brief The terminals read as VOLTAGES, and the row each goes to.
         *  Bounded by the terminal count, being the same eight pins. */
        uint16_t _analog_row[HAL::DigitalIn::CHANNELS];
        uint16_t _analog_pin[HAL::DigitalIn::CHANNELS];
        uint8_t  _analogs = 0;

        /** @brief The edge counters: the row each total goes TO, which is the
         *  same row that declared the channel, and the channel itself. */
        uint16_t _pulse_row[HAL::Pulse::MAX_CHANNELS];
        uint8_t  _pulse_ch[HAL::Pulse::MAX_CHANNELS];
        uint8_t  _pulses = 0;

        /** @brief Local rows that got no terminal. Both classes come off the
         *  same eight pins, so a table can ask for more than exist; such a row
         *  reads its declared value for ever and looks like an open contact.
         *  Counted rather than dropped, and the site test asserts it is 0. */
        uint8_t  _refused = 0;

        /** @} */

    public:
        LocalInputs(const SetupContext& ctx, Vars::Bank& io)
            : Component(ctx, "local_in"), _io(&io) {}

        /** @brief How many terminals this build actually reads. */
        uint8_t count() const { return _count; }
        /** @brief How many edge counters it publishes. */
        uint8_t pulse_count() const { return _pulses; }
        /** @brief Local rows that asked for a terminal and did not get one.
         *  Zero on a correctly wired site. @see _refused */
        uint8_t refused() const { return _refused; }

        /** @brief One entry, PHASE_INPUT: a terminal has to be in the bank
         *  before the programs run on it. */
        void register_tasks() override;

    protected:
        /** @brief Find the rows and configure their terminals. */
        void on_setup(uint64_t now_ms) override;
        void on_input(uint64_t now_ms) override;

    };

}}  // namespace Botanik::Io


#endif // IO_LOCAL_INPUTS_H
