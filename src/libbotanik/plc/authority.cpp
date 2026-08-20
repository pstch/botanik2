/**
 * @file plc/authority.cpp
 * @brief The two edges that change who may drive the field.
 */
#include "libbotanik/plc/authority.h"

#include "libbotanik/vars/model.h"

namespace Botanik { namespace Plc {

    void ControlAuthority::set_control_mode(Vars::ControlMode mode) {
        _op_mode = mode;
        _push(false);
    }

    void ControlAuthority::set_health_hold(bool held) {
        const bool was = _health_hold;
        _health_hold = held;
        // The system RAISING its hold is the destructive edge, and only on the
        // transition: the per-beat watchdog path re-asserts it for as long as
        // the fault lasts, so re-erasing every beat would be wasted work.
        _push(held && !was);
    }

    void ControlAuthority::_push(bool erase_overrides) {
        _bank.set_effective_control(control_mode(), erase_overrides);
    }

}}  // namespace Botanik::Plc
