/**
 * @file scan.cpp
 * @brief Scan driver: decide which programs are due and run them (see scan.h).
 */

#include "scan.h"

#include "../../libbotanik_config.h"
#include "../hal/platform.h"

namespace Botanik { namespace Scan {

    void Driver::on_setup(uint64_t now_ms) {
        (void)now_ms;
        if (_programs == nullptr || _io == nullptr) return;

        for (uint8_t i = 0; i < _count; i++) {
            Plc::Program* p = _programs[i];
            if (p == nullptr) continue;

            // Names into VarRefs, once. A program that skipped this would still
            // work - Bank::var(name) resolves on every call - but at the cost of
            // a linear walk of the whole table per named access per activation.
            p->bind(*_io);

            if (Config::SCAN_STAGGER_MS == 0) continue;
            // An every-beat program has no interval to shift inside, and the
            // modulo would divide by zero.
            if (p->period_ms() == 0) continue;
            // Wrapped into the period: an offset at or past it would skip a
            // whole activation rather than shifting one, and with enough
            // programs the raw i*step runs past the shorter periods.
            p->_timer.set_offset((uint32_t)i * Config::SCAN_STAGGER_MS
                                 % p->period_ms());
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
            // Only a program that has ALREADY run: one that has not is at its
            // first activation, which is due immediately by design and must not
            // be spent here. That also covers bring-up, where control goes from
            // withheld to held with nothing having run yet.
            _enabled = true;
            for (uint8_t i = 0; i < _count; i++)
                if (_programs[i] != nullptr && _programs[i]->runs() > 0)
                    _programs[i]->_timer.ran(now_ms);
        }

        for (uint8_t i = 0; i < _count; i++) {
            Plc::Program* p = _programs[i];
            if (p == nullptr) continue;

            // Due, how late, and how many activations were dropped: one rule,
            // shared with every other multi-rate caller (scheduler/task_timer.h).
            uint32_t dt     = 0;
            uint16_t missed = 0;
            if (!p->_timer.due(now_ms, p->period_ms(), dt, missed)) continue;

            Plc::Context ctx = { now_ms, dt, missed, *_io };

            p->run(ctx);

            p->_timer.ran(now_ms);
        }
    }

}}  // namespace Botanik::Scan
