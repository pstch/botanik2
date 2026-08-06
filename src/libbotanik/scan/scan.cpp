/**
 * @file scan.cpp
 * @brief Scan driver: decide which programs are due and run them (see scan.h).
 */

#include "libbotanik/scan/scan.h"     // Scan::Driver - what this file implements

#include "libbotanik_config.h"        // Config::SCAN_STAGGER_MS - spreads the programs across beats

#include "libbotanik/hal/platform.h"  // HAL::Clock - how late a due program actually is

namespace Botanik { namespace Scan {

    void Driver::on_setup(uint64_t now_ms) {
        if (_programs == nullptr || _io == nullptr) return;

        for (uint8_t i = 0; i < _count; i++) {
            Plc::Program* p = _programs[i];
            if (p == nullptr) continue;

            p->attach(*_io, _logger);
            p->set_offset(Scheduler::stagger_offset(i, Config::SCAN_STAGGER_MS,
                                                    p->period_ms()));
            // Resolve its names now rather than letting step() do it lazily on
            // the first beat: this runs inside BotanikFirmware::setup(), before
            // the watchdog is armed, which is where the rest of the firmware's
            // one-shot work already sits.
            p->setup(now_ms);
        }
    }

    void Driver::on_step(uint64_t now_ms) {
        if (_programs == nullptr || _io == nullptr) return;

        // Control authority gates the programs, and it lives here rather than at
        // the call site: the driver already holds the Bank, and the phase around
        // it must NOT be gated with it - the collector still has to carry the
        // safe states this withdrawal just wrote into the image out to the
        // field (see docs/dev/scan.md).
        if (!_io->control_enabled()) {
            if (_enabled) {
                // Falling edge. From here the process moves without us, so
                // whatever a program remembered about the field is stale the
                // moment we stop driving - and reseeding dt on the way back in
                // cannot express that. Each program drops what has gone stale
                // and keeps what is a record (see plc/program.h).
                for (uint8_t i = 0; i < _count; i++)
                    if (_programs[i] != nullptr) _programs[i]->reset();
            }
            _enabled = false;
            return;
        }
        if (!_enabled) {
            // Control just came back. Reseed every program's timer so the first
            // activation reports its nominal period instead of the whole
            // outage: a Ton holding 5 s handed an hour of dt fires the instant
            // control returns, which is the one thing a withdrawal exists to
            // prevent.
            //
            // reseed() leaves a program that has never run alone: its first
            // activation is due immediately by design and must not be spent
            // here. That also covers bring-up, where control goes from withheld
            // to held with nothing having run yet.
            _enabled = true;
            for (uint8_t i = 0; i < _count; i++)
                if (_programs[i] != nullptr) _programs[i]->reseed(now_ms);
        }

        // Due, how late, and how many activations were dropped is Component's
        // arithmetic, the same for a control program as for any other component
        // that runs at its own rate; step() is what applies it.
        for (uint8_t i = 0; i < _count; i++)
            if (_programs[i] != nullptr) _programs[i]->step(now_ms);
    }

}}  // namespace Botanik::Scan
