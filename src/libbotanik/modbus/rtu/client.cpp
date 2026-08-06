/**
 * @file modbus/rtu/client.cpp
 * @brief @see client.h
 */

#include "libbotanik/modbus/rtu/client.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Modbus {

void RtuClient::on_setup(uint64_t now_ms) {
    HAL::Serial::fieldbus_begin(Config::MODBUS_BAUD, 8, HAL::Serial::PARITY_NONE, 1);
    _port = HAL::Serial::get(HAL::Serial::FIELDBUS);

    if (_io == nullptr) return;
    _plan.plan(*_io, Config::MODBUS_BAUD, Config::MODBUS_TURNAROUND_MS);
    _plan.seed(now_ms);
    _enter(MB_IDLE, now_ms);

    // One health entry per device the plan actually addresses - taken from the
    // plan rather than from the table, so a row that was dropped cannot leave a
    // device being watched that is never spoken to.
    _peer_count = 0;
    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const uint8_t unit = _plan.block(i).device;
        if (_peer_of(unit) != nullptr) continue;
        if (_peer_count >= Config::MODBUS_MAX_DEVICES) break;
        _peer[_peer_count].unit = unit;
        _peer_count++;
    }
    // Resolved once, by name, like every other consumer of the bank.
    _alarm_ref = _io->find("alarm_fieldbus");
    _dead_ref  = _io->find("sys_fieldbus_dead");
    _publish_health();

    if (_logger == nullptr) return;
    _logger->info(Logs::SHARED_IO,
                  "modbus: %u rows -> %u blocks, %u%% of the bus at %u baud",
                  (unsigned)_plan.row_count(), (unsigned)_plan.block_count(),
                  (unsigned)(_plan.duty_ppm() / 10000u),
                  (unsigned)Config::MODBUS_BAUD);
    // Neither of these is allowed to be silent: a dropped row is a signal that
    // will never be scanned, and it would otherwise look exactly like a sensor
    // that is simply never answering.
    if (_plan.rows_dropped() != 0)
        _logger->critical(Logs::SHARED_IO, "modbus: %u row(s) unplanned",
                          (unsigned)_plan.rows_dropped());
    if (_plan.blocks_dropped() != 0)
        _logger->critical(Logs::SHARED_IO,
                          "modbus: %u row(s) past MODBUS_MAX_BLOCKS",
                          (unsigned)_plan.blocks_dropped());
}

RtuClient::Peer* RtuClient::_peer_of(uint8_t unit) {
    for (uint8_t i = 0; i < _peer_count; ++i)
        if (_peer[i].unit == unit) return &_peer[i];
    return nullptr;
}

const RtuClient::Peer* RtuClient::_peer_of(uint8_t unit) const {
    for (uint8_t i = 0; i < _peer_count; ++i)
        if (_peer[i].unit == unit) return &_peer[i];
    return nullptr;
}

bool RtuClient::_due(uint8_t unit, uint64_t now_ms) const {
    const Peer* p = _peer_of(unit);
    // An untracked unit is always due: the plan asked for it, and refusing here
    // would drop a block silently rather than fail it visibly.
    return (p == nullptr) || (now_ms >= p->probe_ms);
}

void RtuClient::_answered(uint8_t unit) {
    Peer* p = _peer_of(unit);
    if (p == nullptr || p->misses == 0) return;
    if (p->misses > Config::MODBUS_RETRIES && _logger != nullptr)
        _logger->notice(Logs::SHARED_IO, "modbus: dev %u answering again",
                        (unsigned)unit);
    p->misses   = 0;
    p->probe_ms = 0;
    _publish_health();
}

void RtuClient::_missed(uint8_t unit, uint64_t now_ms) {
    Peer* p = _peer_of(unit);
    if (p == nullptr) return;
    if (p->misses < 255) p->misses++;
    // The retries are free-running: until they are spent the device keeps its
    // place in the rotation, because one lost frame is weather.
    if (p->misses <= Config::MODBUS_RETRIES) return;

    // Spent. Double from the base, capped - and shift by the number of backoffs
    // already served, not by the miss count, so the first one is the base.
    const uint8_t steps = (uint8_t)(p->misses - Config::MODBUS_RETRIES - 1);
    uint32_t wait = Config::MODBUS_BACKOFF_MS;
    for (uint8_t i = 0; i < steps && wait < Config::MODBUS_BACKOFF_MAX_MS; ++i)
        wait *= 2;
    if (wait > Config::MODBUS_BACKOFF_MAX_MS) wait = Config::MODBUS_BACKOFF_MAX_MS;
    p->probe_ms = now_ms + wait;

    // Once, on the way in. A device that is unplugged for a week must not be a
    // log entry every 30 seconds.
    if (p->misses == Config::MODBUS_RETRIES + 1 && _logger != nullptr)
        _logger->error(Logs::SHARED_IO,
                       "modbus: dev %u not answering, probing every %u ms",
                       (unsigned)unit, (unsigned)wait);
    _publish_health();
}

void RtuClient::_publish_health() {
    _dead = 0;
    for (uint8_t i = 0; i < _peer_count; ++i)
        if (_peer[i].misses > Config::MODBUS_RETRIES) _dead++;
    if (_io == nullptr) return;
    // Written like any other driver writes a variable: the alarm model reads the
    // boolean, and the status value is published to MQTT and the TCP slave.
    _io->var(_alarm_ref).set(_dead != 0);
    _io->var(_dead_ref).set_i((int64_t)_dead);
}

void RtuClient::_enter(State s, uint64_t now_ms) {
    _state    = s;
    _since_ms = now_ms;
}

uint16_t RtuClient::_pick(uint64_t now_ms) const {
    // Writes before reads. A demand that has changed is worth more than a
    // reading about to be taken again anyway, and a safe state is worth more
    // than either - which is why this runs whatever control authority says.
    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const Block& b = _plan.block(i);
        if (is_read(b.fc)) continue;
        if (!_due(b.device, now_ms)) continue;
        const uint64_t since = now_ms - b.last_ms;
        if (since >= Config::MODBUS_REWRITE_MS) return i;
        if (since >= Config::MODBUS_WRITE_MIN_MS && block_dirty(*_io, _plan, b))
            return i;
    }

    // Then the most overdue read, so a starved block cannot stay starved.
    uint16_t best = NO_BLOCK;
    uint64_t worst = 0;
    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const Block& b = _plan.block(i);
        if (!is_read(b.fc) || now_ms < b.due_ms) continue;
        if (!_due(b.device, now_ms)) continue;
        const uint64_t late = now_ms - b.due_ms;
        if (best == NO_BLOCK || late > worst) { best = i; worst = late; }
    }
    return best;
}

bool RtuClient::_build(uint16_t block, uint64_t now_ms) {
    (void)now_ms;
    const Block& b = _plan.block(block);
    _expect.unit = b.device;
    _expect.fc   = b.fc;
    _expect.addr = b.reg;
    _expect.qty  = b.regs;

    if (is_read(b.fc)) {
        _frame_len = build_read(_frame, (uint16_t)sizeof _frame, b.device, b.fc,
                                b.reg, b.regs);
        return _frame_len != 0;
    }

    // Collect into the tail of the frame buffer, then build over it. The payload
    // is at most 250 bytes and the header is 7, so the two never overlap.
    uint8_t payload[Config::MODBUS_MAX_REGS_PER_BLOCK * 2];
    const uint16_t n = collect_write(*_io, _plan, b, payload, (uint16_t)sizeof payload);
    if (n == 0) return false;

    if (b.fc == FC_WRITE_COILS) {
        _frame_len = build_write_coils(_frame, (uint16_t)sizeof _frame, b.device,
                                       b.reg, b.regs, payload);
    } else {
        // collect_write laid the registers out big-endian already; hand them
        // over as words so the builder does not have to know either way.
        uint16_t regs[Config::MODBUS_MAX_REGS_PER_BLOCK];
        for (uint16_t i = 0; i < b.regs; ++i) regs[i] = be16(payload[i * 2], payload[i * 2 + 1]);
        _frame_len = build_write_registers(_frame, (uint16_t)sizeof _frame,
                                           b.device, b.reg, b.regs, regs);
    }
    return _frame_len != 0;
}

bool RtuClient::_send() {
    if (_port == nullptr) return false;
    // In one pass, deliberately. The USART drops DE when its shift register
    // empties, so a frame handed over in pieces would be cut in half on the
    // wire - see the header. The buffer is empty whenever a transaction starts
    // and the maximum request is 255 bytes, so this fits.
    for (uint16_t i = 0; i < _frame_len; ++i) {
        if (_port->write(_frame[i]) != 1) return false;
    }
    return true;
}

void RtuClient::on_step(uint64_t now_ms) {
    if (_io == nullptr || _port == nullptr) return;

    switch (_state) {
        case MB_IDLE: {
            const uint16_t pick = _pick(now_ms);
            if (pick == NO_BLOCK) return;
            _active = pick;
            if (!_build(pick, now_ms)) {
                // Nothing on the wire went wrong; the plan asked for a frame
                // that cannot be built. Retire the block's turn so it does not
                // spin on it every beat.
                _plan.block(pick).due_ms  = now_ms + _plan.block(pick).period_ms;
                _plan.block(pick).last_ms = now_ms;
                _faults++;
                _active = NO_BLOCK;
                return;
            }
            if (!_send()) {
                _tx_short++;
                _faults++;
                _missed(_plan.block(pick).device, now_ms);
                _active = NO_BLOCK;
                _enter(MB_GAP, now_ms);
                return;
            }
            _enter(MB_DRAIN, now_ms);
            return;
        }

        case MB_DRAIN: {
            if (!HAL::Serial::fieldbus_tx_idle()) return;
            // The frame has gone. Whatever is in the receive path now is our own
            // echo or noise - the slave cannot have answered yet - and an echo
            // taken for a response would be indistinguishable from one for a
            // write, whose reply is byte-identical to its request.
            HAL::Serial::fieldbus_flush_input();
            _framer.arm(_expect, now_ms, Config::MODBUS_TIMEOUT_MS);
            _enter(MB_RX, now_ms);
            return;
        }

        case MB_GAP: {
            if ((now_ms - _since_ms) < Config::MODBUS_GAP_MS) return;
            _enter(MB_IDLE, now_ms);
            return;
        }

        case MB_TX:
        case MB_RX:
            // receive_step() owns the listening half.
            return;
    }
}

void RtuClient::receive_step(uint64_t now_ms) {
    if (_port == nullptr || _state != MB_RX) return;

    // Drain what the UART has, bounded: a wedged port must not turn into an
    // unbounded loop inside the beat.
    uint8_t chunk[64];
    for (uint8_t pass = 0; pass < 4; ++pass) {
        uint16_t n = 0;
        while (n < (uint16_t)sizeof chunk) {
            const int c = _port->read();
            if (c < 0) break;
            chunk[n++] = (uint8_t)c;
        }
        if (n == 0) break;
        _framer.feed(chunk, n);
    }

    const int r = _framer.poll(now_ms);
    if (r == 0) return;

    if (r == 1) {
        _settle(now_ms);
    } else {
        _faults++;
        // Silence, noise, or an answer from the wrong device: none of them is
        // this device answering, and the backoff is what stops each of them
        // costing a response timeout on every sweep from here on.
        if (_active != NO_BLOCK) _missed(_plan.block(_active).device, now_ms);
    }

    // Whatever happened, this block has had its turn. A read comes round again
    // on its period; a failed write stays dirty and will be retried at the
    // minimum interval.
    if (_active != NO_BLOCK) {
        Block& b = _plan.block(_active);
        if (is_read(b.fc)) b.due_ms = now_ms + b.period_ms;
        b.last_ms = now_ms;
    }
    _framer.disarm();
    _active = NO_BLOCK;
    _enter(MB_GAP, now_ms);
}

void RtuClient::_settle(uint64_t now_ms) {
    if (_active == NO_BLOCK) return;
    const Block& b = _plan.block(_active);

    // Whatever it said, it said something: the device is alive and its silence
    // count starts again. A refusal is a register map that does not match the
    // device, which is a different engineer's problem from a dead cable.
    _answered(b.device);

    if (_framer.err() == ERR_EXCEPTION) {
        // A healthy slave saying no. Counted apart from silence, because the two
        // send an engineer to different places: this one is a register map that
        // does not match the device.
        _refusals++;
        if (_logger != nullptr)
            _logger->warning(Logs::SHARED_IO,
                          "modbus: dev %u fc %u reg %u refused (exception %u)",
                          (unsigned)b.device, (unsigned)b.fc, (unsigned)b.reg,
                          (unsigned)_framer.response().ex);
        return;
    }

    _txns++;
    if (is_read(b.fc)) {
        commit_read(*_io, _plan, b, _framer.response(), now_ms);
    } else {
        // Only now, with the slave's echo in hand. Clearing on send would lose a
        // demand to a frame that never arrived.
        clear_dirty(*_io, _plan, b);
    }
}

bool RtuClient::healthy() const {
    // Nothing planned is not a fault: a site with no field bus is a legitimate
    // build, and saying otherwise would raise an alarm nobody can clear.
    if (_plan.block_count() == 0) return true;
    // Not "has it ever timed out": one lost frame in a week is a healthy bus,
    // and a latch on that would read unhealthy forever after. What matters is
    // whether anything is dead RIGHT NOW - which is what the alarm says too.
    return _txns > 0 && _dead == 0;
}

const char* RtuClient::status_str() const {
    if (_plan.block_count() == 0) return "idle";
    if (_txns == 0)               return "silent";
    switch (_state) {
        case MB_TX:
        case MB_DRAIN: return "sending";
        case MB_RX:    return "waiting";
        default:       return "online";
    }
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
