#ifndef PLC_AUTHORITY_H
#define PLC_AUTHORITY_H

/**
 * @file authority.h
 * @brief ControlAuthority - who may drive the field, and why not.
 *
 * Two INDEPENDENT sources decide it and the STRICTER wins: the operator's
 * standing choice and the system's hold. It starts held, so nothing before the
 * end of setup() can energise an actuator. See docs/dev/scan.md.
 *
 * The DECISION lives here; the ENACTMENT is Vars::Bank's. This object pushes the
 * effective mode down with set_effective_control(), and the bank never asks who
 * decided it - the same shape Tasks::Registry uses for its own gate.
 */

#include "libbotanik/vars/descriptor.h"   // ControlMode

namespace Botanik { namespace Vars { class Bank; } }

namespace Botanik { namespace Plc {

    /** @brief The operator's standing choice and the system's hold, and the
     *  stricter of the two (see the file header). */
    class ControlAuthority {

        /** @name State
         *  @{ */

        /** @brief Where the verdict is enacted. Borrowed; the firmware owns
         *  both and declares the bank first. */
        Vars::Bank&       _bank;
        /** @brief The operator's standing choice. Survives a health fault
         *  coming and going, which is the point of keeping it separate. */
        Vars::ControlMode _op_mode = Vars::CONTROL_ENABLED;
        /** @brief The system's hold: bring-up, a health fault, a reset we have
         *  decided to take. Starts SET - outputs are not ours to move yet. */
        bool              _health_hold = true;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Built over the bank it will drive. Pushes nothing: the bank
         *  starts at CONTROL_DISABLED for the same reason the hold starts
         *  raised, so the two already agree. */
        explicit ControlAuthority(Vars::Bank& bank) : _bank(bank) {}

        /** @} */

        /** @name The two sources
         *  @{ */

        /** @brief Set the OPERATOR's standing mode (the console's `control`). */
        void set_control_mode(Vars::ControlMode mode);

        /** @brief The operator's standing choice, whatever the system is doing. */
        Vars::ControlMode operator_mode() const { return _op_mode; }

        /** @brief Raise or release the SYSTEM's hold. Forces CONTROL_DISABLED.
         *  @warning Unlike the operator's own disable this ERASES every output
         *  override, stored copy included. */
        void set_health_hold(bool held);

        /** @brief Whether the system is holding control down, so the console can
         *  say WHY a `control enable` did nothing. */
        bool health_hold() const { return _health_hold; }

        /** @} */

        /** @name The verdict
         *  @{ */

        /** @brief What is in force: the stricter of the two sources. */
        Vars::ControlMode control_mode() const {
            return _health_hold ? Vars::CONTROL_DISABLED : _op_mode;
        }

        /** @brief Whether the field may be driven (the scan is gated on it). */
        bool control_enabled() const {
            return control_mode() == Vars::CONTROL_ENABLED;
        }

        /** @} */

    private:
        /** @brief Push the current verdict at the bank. @p erase_overrides only
         *  on the system's raising edge - the per-beat watchdog path re-asserts
         *  the hold for as long as the fault lasts. */
        void _push(bool erase_overrides);
    };

}}  // namespace Botanik::Plc

#endif // PLC_AUTHORITY_H
