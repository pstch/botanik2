/**
 * @file local_inputs.cpp
 * @brief Finding the local input rows, and publishing them.
 */

#include "libbotanik/io/local_inputs.h"
#if BOTANIK_FEATURE_VARS

#include "libbotanik/tasks/tasks.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"

namespace Botanik { namespace Io {

    void LocalInputs::on_setup(uint64_t now_ms) {
        (void)now_ms;
        if (_io == nullptr) return;

        for (uint16_t i = 0; i < _io->count(); i++) {
            const Vars::Descriptor& d = _io->descriptor(i);
            if (d.bind.transport != Vars::XP_LOCAL) continue;
            if (d.scope != Vars::SCOPE_DI && d.scope != Vars::SCOPE_AI) continue;

            // The SAME eight terminals, read as a voltage instead of a level.
            // Which it is, is the row's declaration and nothing else - so the
            // pin is configured one way here, once, and a terminal claimed by
            // two rows of different kinds is refused rather than reconfigured
            // under whichever ran last.
            if (d.scope == Vars::SCOPE_AI) {
                if (_analogs < HAL::DigitalIn::CHANNELS
                    && HAL::AnalogIn::attach(d.bind.reg)) {
                    _analog_row[_analogs] = i;
                    _analog_pin[_analogs] = d.bind.reg;
                    _analogs++;
                    continue;
                }
                if (_refused < 0xFFu) _refused++;
                if (_logger != nullptr)
                    _logger->critical(Logs::BOOT, "no analog terminal for '%s'", d.name);
                continue;
            }

            // An edge-counted terminal is a COUNTER, not a level: the level of
            // a pulse line is a moment of a train and not a reading of
            // anything. It is attached once, here, and configuring one pin two
            // ways is a hardware fault waiting for the first wet week.
            if (d.flags & Vars::F_PULSE) {
                if (_pulses < HAL::Pulse::MAX_CHANNELS) {
                    const uint8_t pch = Io::pulse_channel(d);
                    if (HAL::Pulse::attach(pch, Io::pulse_edge(d))) {
                        _pulse_row[_pulses] = i;
                        _pulse_ch[_pulses]  = pch;
                        _pulses++;
                        continue;
                    }
                }
            } else if (_count < LEVEL_MAX) {
                // The row's `reg` IS the terminal: a local row has no register
                // to address, so the column carries the HAL::Pin label the
                // site's table named.
                const uint16_t ch = d.bind.reg;
                if (HAL::DigitalIn::attach(ch)) {
                    _row[_count] = i;
                    _pin[_count] = ch;
                    _count++;
                    continue;
                }
            }

            // Declared a terminal and did not get one: the two classes share
            // the same eight pins, so running out of one is a real answer -
            // but the row then reads its declared value for the life of the
            // run, which is indistinguishable from an open contact.
            if (_refused < 0xFFu) _refused++;
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "no terminal for '%s'", d.name);
        }
    }

    void LocalInputs::on_input(uint64_t now_ms) {
        if (_io == nullptr) return;
        for (uint8_t i = 0; i < _count; i++) {
            const Vars::Descriptor& d = _io->descriptor(_row[i]);
            bool on = HAL::DigitalIn::read(_pin[i]);
            // The one place active-low wiring is folded in for a local
            // terminal, exactly as the field bus does it for a remote one.
            if (d.flags & Vars::F_INVERT) on = !on;
            _io->var(_row[i]).publish(Vars::from_bool(d.type, on), now_ms);
        }
        // A RAW COUNT, through publish_wire: the row's calibration turns it
        // into engineering units, which is the same path a Modbus analog row
        // takes - so a local sensor and a remote one are scaled by one piece
        // of code and a recalibration lands on both the same way.
        for (uint8_t i = 0; i < _analogs; i++) {
            const Vars::Descriptor& d = _io->descriptor(_analog_row[i]);
            _io->publish_wire(Vars::VarRef{ _analog_row[i] },
                              Vars::from_f32(d.bind.wire,
                                             (float)HAL::AnalogIn::read(_analog_pin[i])),
                              now_ms);
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
