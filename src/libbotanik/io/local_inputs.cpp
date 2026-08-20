/**
 * @file local_inputs.cpp
 * @brief Finding the local input rows, and publishing them.
 */

#include "libbotanik/io/local_inputs.h"
#if BOTANIK_FEATURE_VARS

#include "libbotanik/tasks/tasks.h"

namespace Botanik { namespace Io {

    void LocalInputs::on_setup(uint64_t now_ms) {
        (void)now_ms;
        if (_io == nullptr) return;

        for (uint16_t i = 0; i < _io->count(); i++) {
            const Vars::Descriptor& d = _io->descriptor(i);
            if (d.bind.transport != Vars::XP_LOCAL || d.scope != Vars::SCOPE_DI) continue;
            // An edge-counted terminal is attached ONCE, as a counter, by
            // whoever owns the meter: the level of a pulse line is not a
            // reading of anything, and configuring one pin two ways is a
            // hardware fault waiting to happen.
            if (d.flags & Vars::F_PULSE) continue;
            if (_count >= HAL::DigitalIn::CHANNELS) break;

            // The row's `reg` IS the terminal: a local row has no register to
            // address, so the column carries the channel instead.
            const uint8_t ch = (uint8_t)d.bind.reg;
            if (!HAL::DigitalIn::attach(ch)) continue;
            _row[_count]     = i;
            _channel[_count] = ch;
            _count++;
        }
    }

    void LocalInputs::on_input(uint64_t now_ms) {
        if (_io == nullptr) return;
        for (uint8_t i = 0; i < _count; i++) {
            const Vars::Descriptor& d = _io->descriptor(_row[i]);
            bool on = HAL::DigitalIn::read(_channel[i]);
            // The one place active-low wiring is folded in for a local
            // terminal, exactly as the field bus does it for a remote one.
            if (d.flags & Vars::F_INVERT) on = !on;
            _io->var(_row[i]).publish(Vars::from_bool(d.type, on), now_ms);
        }
    }

    void LocalInputs::_input_task(Component* c, const Tasks::Tick& t) {
        static_cast<LocalInputs*>(c)->on_input(t.now_ms);
    }

    void LocalInputs::register_tasks() {
        _tasks->add(*this, &LocalInputs::_input_task, "sys_local_in",
                    Tasks::GROUP_FAST, Tasks::PHASE_INPUT);
    }

}}  // namespace Botanik::Io

#endif // BOTANIK_FEATURE_VARS
