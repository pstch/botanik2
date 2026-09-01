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
            // An edge-counted terminal is a COUNTER, not a level: the level
            // of a pulse line is a moment of a train and not a reading of
            // anything. It is attached once, below, and configuring one pin
            // two ways is a hardware fault waiting for the first wet week.
            if (d.flags & Vars::F_PULSE) {
                if (_pulses >= HAL::Pulse::MAX_CHANNELS) continue;
                const uint8_t pch = (uint8_t)d.bind.reg;
                // The EDGE rides in `fc`, which a local row has no other use
                // for - the same trick `reg` plays for the channel.
                // attach() answers false for an out-of-range or colliding
                // channel AND for one already counting, which is the ordinary
                // case on a second bring-up in one process. Only the first two
                // are a reason not to publish the row.
                if (!HAL::Pulse::attach(pch, (HAL::Pulse::Edge)d.bind.fc)
                    && !HAL::Pulse::attached(pch)) continue;
                _pulse_row[_pulses] = i;
                _pulse_ch[_pulses]  = pch;
                _pulses++;
                continue;
            }
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
        // The raw edge total, as the counter has it. A meter's law turns it
        // into litres: how many per pulse is the site's, and the wrap is the
        // reader's to carry with an unsigned difference.
        for (uint8_t i = 0; i < _pulses; i++)
            _io->var(_pulse_row[i]).publish(
                Vars::Scalar::U(HAL::Pulse::sample(_pulse_ch[i]).count), now_ms);
    }


    void LocalInputs::register_tasks() {
        _tasks->add(*this, "sys_local_in",
                    Tasks::EVERY_BEAT, Tasks::PHASE_INPUT);
    }

}}  // namespace Botanik::Io

#endif // BOTANIK_FEATURE_VARS
