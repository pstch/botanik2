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

// C++11 needs an out-of-class definition for a static const member that is
// ODR-used - bound to a reference, which any comparison in a test macro does.
// Without it the header's initialiser is a compile-time value and nothing else.
const uint16_t RtuClient::NO_BLOCK;

void RtuClient::on_setup(uint64_t now_ms) {
    HAL::Serial::fieldbus_begin(Config::MODBUS_BAUD, 8, HAL::Serial::PARITY_NONE, 1);
    _port = HAL::Serial::get(HAL::Serial::FIELDBUS);
    // Once, before the first transaction: whatever a half-configured line or
    // another master left in the receive path is not an answer to anything.
    HAL::Serial::fieldbus_flush_input();

    if (_io == nullptr) return;
    _plan.plan(*_io, Config::MODBUS_BAUD, Config::MODBUS_TURNAROUND_MS, _devices);
    _plan.seed(now_ms);
    _enter(MB_IDLE, now_ms);

    // Health entries are the planner's device set, indexed by Block::peer - so
    // there is nothing to populate, nothing to cap, and no way to end up with a
    // block whose device nobody is watching.
    for (uint8_t i = 0; i < _plan.device_count(); ++i) _peer[i] = Peer();
    // Resolved once, by name, like every other consumer of the bank.
    _alarm_ref = _io->find("alarm_fieldbus");
    _dead_ref  = _io->find("sys_fieldbus_dead");
    _publish_health();

    if (_logger == nullptr) return;
    // Two lines, and the first one has to FIT (see logs/format.h for the
    // arithmetic). At 80 columns the timestamp, level and MODBUS take 34, so
    // the message gets 46 - which "102 rows -> 22 blocks on 20 device(s)"
    // overran, wrapping mid-word into "device(" / "s)" and pushing the duty
    // figure onto a third line. Commas instead of arrows and "devices" instead
    // of "device(s)" is 29 characters and reads better besides. The duty is the
    // second line because it is the number an engineer actually goes looking
    // for, and it must not be split across a fold. No "modbus:" scope word on
    // any of these: the topic column already says it.
    _logger->info(Logs::MODBUS,
                  "%u rows, %u blocks, %u devices\n"
                  "%u.%u%% of the bus at %u baud",
                  (unsigned)_plan.row_count(), (unsigned)_plan.block_count(),
                  (unsigned)_plan.device_count(),
                  (unsigned)(_plan.duty_ppm() / 10000u),
                  (unsigned)((_plan.duty_ppm() / 1000u) % 10u),
                  (unsigned)Config::MODBUS_BAUD);
    // Neither of these is allowed to be silent: a dropped row is a signal that
    // will never be scanned, and it would otherwise look exactly like a sensor
    // that is simply never answering.
    if (_plan.rows_dropped() != 0)
        _logger->critical(Logs::MODBUS, "%u row(s) unplanned",
                          (unsigned)_plan.rows_dropped());
    if (_plan.rows_unscanned() != 0)
        _logger->critical(Logs::MODBUS, "%u row(s) never scanned",
                          (unsigned)_plan.rows_unscanned());
}

bool RtuClient::_due(const Block& b, uint64_t now_ms) const {
    // An array index now, not a search: the planner gave every block its peer.
    return now_ms >= _peer[b.peer].probe_ms;
}

void RtuClient::_answered(const Block& b) {
    Peer* p = &_peer[b.peer];
    if (p->misses == 0) return;

    // Only when the device was actually DECLARED down. A frame or two lost to
    // noise is weather, and the box never stopped running - re-writing its
    // outputs there would put a write on the bus every time the line glitched.
    // Past the retries we have to assume it rebooted, and a device that
    // rebooted is back at its own power-on defaults.
    if (p->misses > Config::MODBUS_RETRIES) {
        if (_logger != nullptr) {
            char who[40];
            _logger->notice(Logs::MODBUS, "dev %s answering again",
                            device_label(_devices, b.device, who, sizeof who));
        }
        // Every write block of THIS device, not just the block that happened to
        // get the answer: the whole box came back, and its coils and registers
        // came back together.
        reassert_outputs(b.peer);
    }

    p->misses   = 0;
    p->probe_ms = 0;
    _publish_health();
}

void RtuClient::reassert_outputs(uint8_t peer) {
    if (_io == nullptr) return;
    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const Block& w = _plan.block(i);
        if (w.peer != peer || is_read(w.fc)) continue;
        reassert_block(*_io, _plan, w);
    }
}

void RtuClient::_missed(const Block& b, uint64_t now_ms) {
    Peer* p = &_peer[b.peer];
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
    if (p->misses == Config::MODBUS_RETRIES + 1 && _logger != nullptr) {
        char who[40];
        _logger->error(Logs::MODBUS,
                       "dev %s not answering\n"
                       "probing every %u ms",
                       device_label(_devices, b.device, who, sizeof who),
                       (unsigned)wait);
    }
    _publish_health();
}

void RtuClient::_publish_health() {
    _dead = 0;
    for (uint8_t i = 0; i < _plan.device_count(); ++i)
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
    // One pass, scoring each block, highest score wins. The precedence used to
    // be four sequential loops you had to read in order to know what beat what;
    // as a table it can be read at a glance, and the bus is walked once.
    //
    // The tiers are decades apart so nothing inside one can ever outrank the
    // one above: lateness is measured in whole periods and could otherwise
    // climb into a neighbour's range on a starved block.
    static const uint32_t T_SAFE    = 4000000u;  // the field is owed a safe state
    static const uint32_t T_STARVED = 3000000u;  // a read a whole period late
    static const uint32_t T_WRITE   = 2000000u;  // a demand that has changed
    static const uint32_t T_READ    = 1000000u;  // a reading that is due

    uint16_t best  = NO_BLOCK;
    uint32_t score = 0;

    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const Block& b = _plan.block(i);
        if (!_due(b, now_ms)) continue;          // its device is in backoff

        uint32_t s = 0;
        if (!is_read(b.fc)) {
            const uint64_t since = now_ms - b.last_ms;
            const bool dirty = block_dirty(*_io, _plan, b);
            if (_urgent && dirty) {
                // disable_control() marked every output dirty precisely so a
                // transaction carries the safe state out. Nothing pre-empts it,
                // and it does not wait for the ordinary write interval.
                s = T_SAFE;
            } else if (since >= Config::MODBUS_REWRITE_MS) {
                s = T_WRITE + 1u;                // the unconditional re-drive
            } else if (dirty && since >= Config::MODBUS_WRITE_MIN_MS) {
                s = T_WRITE;
            }
        } else if (now_ms >= b.due_ms) {
            const uint64_t late = now_ms - b.due_ms;
            // A read jumps the write queue once it is later than the period it
            // asks for - per block, since a 1 s rung and a 60 s rung do not
            // agree about what late means. Without that floor an Actuator
            // timing a stroke keeps enough blocks dirty to hold the bus and
            // starve the image underneath the programs reading it.
            const bool starved = b.period_ms != 0 && late >= b.period_ms;
            // Lateness RELATIVE to the period the block asked for, in permille -
            // for exactly the reason the starvation floor is per-block: a 1 s
            // rung and a 10 s rung do not agree about what late means, and
            // ranking them by absolute milliseconds hands the bus to the fast
            // ones for ever.
            //
            // Twelve 1 s blocks re-arm constantly, so at any moment one of them
            // is a few hundred ms late. A 10 s block that has just come due is
            // 0 ms late and loses to every one of them; and it keeps losing,
            // because their lateness resets and its own only creeps up. It could
            // not win until it crossed the starvation floor at +10 s - so a 10 s
            // meter refreshed every TWENTY seconds, went invalid at its 15 s
            // timeout on the way, and came back at 20. In permille the same
            // comparison is fair: 900 ms late on a 1 s rung is 900, and 800 ms
            // late on a 10 s rung is 80, so the 1 s block still wins - but a
            // 10 s block 5 s late is 500 and beats a 1 s block 400 ms late,
            // which is the outcome absolute milliseconds could never reach.
            const uint64_t denom = (b.period_ms != 0) ? (uint64_t)b.period_ms : 1u;
            uint64_t permille = (late * 1000u) / denom;
            if (permille > 999999u) permille = 999999u;
            s = (starved ? T_STARVED : T_READ) + (uint32_t)permille;
        }

        if (s > score) { score = s; best = i; }
    }
    return best;
}

void RtuClient::_track_control(uint64_t now_ms) {
    (void)now_ms;
    if (_io == nullptr) return;
    const bool on = _io->control_enabled();
    // The falling edge, and only that: an operator who leaves control disabled
    // must not keep the bus at emergency priority for the rest of the day.
    if (_control_was && !on) _urgent = true;
    _control_was = on;

    // Spent once the safe state is out. Cheaper than it looks - it stops at the
    // first dirty block, and after the sweep there are none.
    if (!_urgent) return;
    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const Block& b = _plan.block(i);
        if (is_read(b.fc)) continue;
        if (block_dirty(*_io, _plan, b)) return;
    }
    _urgent = false;
}

bool RtuClient::_build(uint16_t block, uint64_t now_ms) {
    (void)now_ms;
    const Block& b = _plan.block(block);
    _expect.unit = b.device;
    _expect.fc   = b.fc;
    _expect.addr  = b.reg;
    _expect.qty   = b.regs;
    _expect.value = 0;

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

    // Four write codes, and the block already says which one: the planner picks
    // the single form for a device whose model has no FC15/FC16, and caps those
    // blocks at one point so the form can always carry them.
    //
    // A single write echoes its VALUE rather than a count, so the expectation
    // has to carry it - that echo is the only confirmation the point actually
    // moved, on exactly the devices that cannot be driven any other way.
    switch (b.fc) {
    case FC_WRITE_COIL:
        _expect.value = bit_at(payload, 0) ? 0xFF00u : 0x0000u;
        _frame_len = build_write_coil(_frame, (uint16_t)sizeof _frame, b.device,
                                      b.reg, bit_at(payload, 0));
        break;
    case FC_WRITE_REGISTER:
        _expect.value = be16(payload[0], payload[1]);
        _frame_len = build_write_register(_frame, (uint16_t)sizeof _frame,
                                          b.device, b.reg, _expect.value);
        break;
    case FC_WRITE_COILS:
        _frame_len = build_write_coils(_frame, (uint16_t)sizeof _frame, b.device,
                                       b.reg, b.regs, payload);
        break;
    default:
        // collect_write laid the registers out big-endian already, which is the
        // layout they go out in - so they go straight across. Unpacking them
        // into words for the builder to repack cost a second stack array the
        // size of a whole block.
        _frame_len = build_write_registers(_frame, (uint16_t)sizeof _frame,
                                           b.device, b.reg, b.regs, payload);
        break;
    }
    return _frame_len != 0;
}

// The two conditions that together make a partial frame unreachable. The first
// is checked here, once; the second is enforced per transaction by the tx_idle()
// guard in on_step(), which will not start a frame until the ring has drained.
static_assert(9u + 2u * Config::MODBUS_MAX_REGS_PER_BLOCK
                  <= Config::MODBUS_TX_RING_BYTES,
              "a full FC16 must fit the transmit ring in one go: raise "
              "BOTANIK_MODBUS_TX_RING_BYTES or lower MODBUS_MAX_REGS_PER_BLOCK");

bool RtuClient::_send() {
    if (_port == nullptr) return false;
    // In one pass, deliberately. The USART drops DE when its shift register
    // empties, so a frame handed over in pieces would be cut in half on the
    // wire - see the header.
    for (uint16_t i = 0; i < _frame_len; ++i) {
        if (_port->write(_frame[i]) != 1) {
            // Unreachable: the ring was empty (tx_idle) and the frame is
            // provably smaller than it. Reaching here means the declared ring
            // size is a lie, and the consequence is a CRC runt on a shared bus
            // that reads as a wiring fault - so it is said out loud rather than
            // counted quietly.
            if (_logger != nullptr)
                _logger->critical(Logs::MODBUS,
                                  "TX ring refused byte %u of %u\n"
                                  "MODBUS_TX_RING_BYTES is wrong",
                                  (unsigned)i, (unsigned)_frame_len);
            return false;
        }
    }
    return true;
}

void RtuClient::on_step(uint64_t now_ms) {
    if (_io == nullptr || _port == nullptr) return;

    // Every beat, not only when idle. The control-authority edge is what raises
    // the safe-state urgency, and a withdrawal during a transaction would
    // otherwise be seen only once the transaction ended.
    _track_control(now_ms);

    // The gap expires INTO this beat rather than costing one of its own: it is
    // inter-frame silence, and once it has elapsed there is nothing left to wait
    // for. Falling through is worth a whole beat per transaction.
    if (_state == MB_GAP) {
        if ((now_ms - _since_ms) < _gap_ms) return;
        _enter(MB_IDLE, now_ms);
    }

    // MB_RX belongs to receive_step(): it owns the listening half, and it runs
    // in PHASE_INPUT, ahead of this.
    if (_state != MB_IDLE) return;

    // Whole frame or nothing, and this is what makes that true: the ring must
    // have DRAINED, not merely have room for one more byte. A write that stops
    // half way leaves a CRC runt on a shared bus, which every other device reads
    // as line noise. Not a fault - just not yet.
    if (!HAL::Serial::fieldbus_tx_idle()) return;

    const uint16_t pick = _pick(now_ms);
    if (pick == NO_BLOCK) return;
    _active = pick;

    if (!_build(pick, now_ms)) {
        // Nothing on the wire went wrong; the plan asked for a frame that cannot
        // be built. Retire the block's turn so it does not spin on it every beat.
        _plan.block(pick).due_ms  = now_ms + _plan.block(pick).period_ms;
        _plan.block(pick).last_ms = now_ms;
        _faults++;
        _active = NO_BLOCK;
        return;
    }

    if (!_send()) {
        _tx_short++;
        _faults++;
        _missed(_plan.block(pick), now_ms);
        _active = NO_BLOCK;
        _enter(MB_GAP, now_ms);
        return;
    }

    // Armed HERE, not a beat later. The answer can be on the wire before this
    // beat ends - at 38400 an 8-byte request clears in 2.3 ms - and anything not
    // being listened for is lost.
    //
    // Whether our own frame comes back first is a property of the PORT, not
    // something to infer from the bytes: a transceiver with RE tied low echoes,
    // and the simulator's socketpair does not. HAL knows which it is.
    //
    // Straight to MB_RX: there is no separate state for "the frame is still
    // leaving". Nothing waits on that any more - the framer is listening from
    // this instant, and the tx_idle() check above is what stops the NEXT frame
    // starting before this one has gone.
    _framer.arm(_expect, now_ms, Config::MODBUS_TIMEOUT_MS,
                HAL::Serial::fieldbus_echoes() ? _frame_len : 0);
    _enter(MB_RX, now_ms);
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
        if (_active != NO_BLOCK) {
            const Block& b = _plan.block(_active);
            _missed(b, now_ms);
            // Nothing came back, so nothing about these rows is current. Their
            // own timeouts would get here eventually; this is the driver saying
            // it already knows.
            if (is_read(b.fc)) invalidate_block(*_io, _plan, b, Vars::Q_STALE);
        }
    }

    // Whatever happened, this block has had its turn. A read comes round again
    // on its period; a failed write stays dirty and will be retried at the
    // minimum interval.
    if (_active != NO_BLOCK) {
        Block& b = _plan.block(_active);
        if (is_read(b.fc)) b.due_ms = now_ms + b.period_ms;
        b.last_ms = now_ms;
    }
    // How much silence this device wants before the next frame, which some
    // models specify well above the protocol's 3.5 character times.
    if (_active != NO_BLOCK) {
        const DeviceModel& m = model_of(_devices, _plan.block(_active).device);
        _gap_ms = (m.min_gap_ms > Config::MODBUS_GAP_MS) ? m.min_gap_ms
                                                         : Config::MODBUS_GAP_MS;
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
    _answered(b);

    if (_framer.err() == ERR_EXCEPTION) {
        // A healthy slave saying no. Counted apart from silence, because the two
        // send an engineer to different places: this one is a register map that
        // does not match the device.
        _refusals++;
        // And the rows say so too. Without this they keep their last value at
        // Q_GOOD until expire_stale() catches up - and a row declared with no
        // timeout is never caught up with at all.
        if (is_read(b.fc)) invalidate_block(*_io, _plan, b, Vars::Q_REFUSED);
        if (_logger != nullptr) {
            char who[40];
            _logger->warning(Logs::MODBUS,
                          "dev %s refused\n"
                          "fc %u reg %u, exception %u",
                          device_label(_devices, b.device, who, sizeof who),
                          (unsigned)b.fc, (unsigned)b.reg,
                          (unsigned)_framer.response().ex);
        }
        return;
    }

    _txns++;
    if (is_read(b.fc)) {
        commit_read(*_io, _plan, b, _framer.response(), now_ms);
    } else {
        // Only now, with the slave's echo in hand. Clearing on send would lose a
        // demand to a frame that never arrived.
        //
        // The payload is still where the builder left it: byte 7 of the frame,
        // for as long as that frame is the request in flight. Handing it back is
        // what lets clear_dirty() tell a row this transaction carried from one a
        // program moved while it was in flight.
        const uint16_t head = 7u;                    // unit, fc, addr, qty, count
        const uint16_t sent = (_frame_len > head + 2u)
                                  ? (uint16_t)(_frame_len - head - 2u) : 0u;
        clear_dirty(*_io, _plan, b, sent != 0 ? _frame + head : nullptr, sent);
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
        case MB_RX:  return "waiting";
        case MB_GAP: return "gap";
        default:     return "online";
    }
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
