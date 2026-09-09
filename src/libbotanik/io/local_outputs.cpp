/**
 * @file local_outputs.cpp
 * @brief Finding the local output rows, and driving them.
 */

#include "libbotanik/io/local_outputs.h"

#include "libbotanik/tasks/tasks.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/topics.h"

namespace Botanik { namespace Io {

    void LocalOutputs::register_tasks() {
        _tasks->add(*this, "local_out", Tasks::EVERY_BEAT, Tasks::PHASE_OUTPUT);
    }

    void LocalOutputs::on_setup(uint64_t now_ms) {
        (void)now_ms;
        if (_io == nullptr) return;

        for (uint16_t i = 0; i < _io->count(); i++) {
            const Vars::Descriptor& d = _io->descriptor(i);
            if (!Vars::local(d) || d.kind != Vars::K_OUTPUT) continue;

            // The row's `reg` IS the pin: a local row has no register to
            // address, so the column carries the HAL::Pin label instead.
            const uint16_t pin = d.bind.reg;
            if (_count < PIN_MAX && HAL::DigitalOut::attach(pin)) {
                _row[_count] = i;
                _pin[_count] = pin;
                _count++;
                continue;
            }

            // Declared a pin and did not get one. Worse than the input case:
            // the law goes on writing the row and the field never moves, so
            // the only symptom is an actuator that never acts.
            if (_refused < 0xFFu) _refused++;
            if (_logger != nullptr)
                _logger->critical(Logs::BOOT, "no output pin for '%s'", d.name);
        }

        // Safe BEFORE the first law runs. attach() already dropped the pin, but
        // the ROW may declare its safe state as asserted - a valve that must be
        // open when the coil is off - and the two have to agree from the start.
        on_output(now_ms);
    }

    void LocalOutputs::on_output(uint64_t now_ms) {
        (void)now_ms;
        if (_io == nullptr) return;
        for (uint8_t i = 0; i < _count; i++) {
            const Vars::Descriptor& d = _io->descriptor(_row[i]);
            bool on = _io->var(uint16_t{ _row[i] }).as_bool();
            // The one place active-low wiring is folded in for a local pin,
            // exactly as the field bus does it for a remote coil.
            if (d.flags & Vars::F_INVERT) on = !on;
            HAL::DigitalOut::write(_pin[i], on);
        }
    }

}}  // namespace Botanik::Io

