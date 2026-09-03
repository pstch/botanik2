/**
 * @file modbus/rtu/client.cpp
 * @brief @see client.h
 */

#include "libbotanik/tasks/tasks.h"
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
const uint8_t  RtuClient::NO_PEER;

uint32_t RtuClient::_tune(Vars::VarRef r, uint32_t dflt) const {
    if (_io == nullptr || r.index == Vars::VAR_NONE) return dflt;
    const Vars::Var h = _io->var(r);
    if (!h.valid()) return dflt;
    const int64_t v = h.as_i64();
    // Out of BAND is fine and deliberate - an override is unbanded so that a
    // maintenance session can ask for what the band refuses. Out of TYPE is
    // not: the value has to fit what every caller here reads it as.
    if (v < 0 || v > (int64_t)0xFFFFFFFFll) return dflt;
    return (uint32_t)v;
}

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
    _line_ref  = _io->find("alarm_fieldbus_line");
    _drop_ref  = _io->find("alarm_io_drop");
    _dead_ref  = _io->find("sys_fieldbus_dead");
    _crc_ref   = _io->find("sys_fieldbus_crc");
    _stray_ref = _io->find("sys_fieldbus_stray");
    // The tuning rows, the same way. Absent (no library splice) is not a fault:
    // _tune() falls back to the Config constant these default from.
    _p_timeout     = _io->find("rtu_timeout_ms");
    _p_retries     = _io->find("rtu_retries");
    _p_backoff     = _io->find("rtu_backoff_ms");
    _p_backoff_max = _io->find("rtu_backoff_max_ms");
    _p_write_min   = _io->find("rtu_write_min_ms");
    _p_rewrite     = _io->find("rtu_rewrite_ms");
    _publish_health();

    if (_logger == nullptr) return;
    // Two lines, and the first one has to FIT: at 80 columns the timestamp,
    // level and MODBUS take 34, leaving 46 for the message (see logs/format.h).
    // The duty gets its own line so a fold cannot split it. No "modbus:" scope
    // word: the topic column already says it.
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

uint8_t RtuClient::_next_suspect(uint64_t now_ms) const {
    const uint8_t n = _plan.device_count();
    if (n == 0 || now_ms < _probe_gate_ms) return NO_PEER;   // the gate is shut
    for (uint8_t k = 0; k < n; ++k) {
        const uint8_t p = (uint8_t)((_probe_cursor + k) % n);
        if (_peer[p].misses == 0) continue;        // answering; not a suspect
        if (now_ms < _peer[p].probe_ms) continue;  // still serving its backoff
        return p;
    }
    return NO_PEER;
}

void RtuClient::_answered(const Block& b) {
    Peer* p = &_peer[b.peer];
    if (p->misses == 0) return;

    // Only when the device was actually DECLARED down. A frame or two lost to
    // noise is weather, and the box never stopped running - re-writing its
    // outputs there would put a write on the bus every time the line glitched.
    // Past the retries we have to assume it rebooted, and a device that
    // rebooted is back at its own power-on defaults.
    if (p->misses > _retries()) {
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
    if (p->misses <= _retries()) return;

    // Spent. Double from the base, capped - and shift by the number of backoffs
    // already served, not by the miss count, so the first one is the base.
    const uint8_t steps = (uint8_t)(p->misses - _retries() - 1);
    const uint32_t cap = _backoff_max_ms();
    uint32_t wait = _backoff_ms();
    // Capped BEFORE the doubling, not after: an unbanded override can put the
    // cap high enough that `wait *= 2` wraps, and a wrapped value is still
    // below it - so the loop kept doubling and the probe landed back at now,
    // which is the whole failure the spacing exists to prevent.
    for (uint8_t i = 0; i < steps; ++i) {
        if (wait > cap / 2u) { wait = cap; break; }
        wait *= 2;
    }
    if (wait > cap) wait = cap;
    p->probe_ms = now_ms + wait;

    // Once, on the way in. A device that is unplugged for a week must not be a
    // log entry every 30 seconds.
    if (p->misses == _retries() + 1 && _logger != nullptr) {
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
        if (_peer[i].misses > _retries()) _dead++;
    if (_io == nullptr) return;
    // Written like any other driver writes a variable: the alarm model reads the
    // boolean, and the status value is published to MQTT and the TCP slave.
    _io->var(_alarm_ref).set(_dead != 0);
    _io->var(_dead_ref).set_i((int64_t)_dead);

    // Every planned device at once is not twenty devices failing, it is the
    // segment. Guarded on _dead so a plan with no devices in it does not read as
    // a dead line.
    _io->var(_line_ref).set(_dead != 0 && _dead == _plan.device_count());

    // A verdict from the PLAN, not from the wire, so it stands from bring-up
    // and does not clear on its own: a row nobody scans is a signal that will
    // never be read, and it looks exactly like a sensor that never answers.
    _io->var(_drop_ref).set(_plan.rows_dropped() != 0 ||
                            _plan.rows_unscanned() != 0);

    // The framer has always kept these apart because they point at different
    // physical faults; until now nothing read them. Saturating rather than
    // wrapping: a counter that rolls over to nothing reads as a bus that
    // recovered.
    const uint32_t crc   = _framer.crc_errors();
    const uint32_t stray = _framer.stray_bytes() + _stale_rx;
    _io->var(_crc_ref).set_i((int64_t)(crc   > 65535u ? 65535u : crc));
    _io->var(_stray_ref).set_i((int64_t)(stray > 65535u ? 65535u : stray));
}

void RtuClient::_enter(State s, uint64_t now_ms) {
    _state    = s;
    _since_ms = now_ms;
}

RtuClient::Slot RtuClient::_slot_of(const Block& b) const {
    Slot s;
    if (is_read(b.fc)) {
        // A poll falls due on its period, and the period is also what it may
        // acceptably slip by: poll any later and the row flaps between valid and
        // stale, which is the relationship the ladder is derived from.
        s.due      = b.due_ms;
        // The planner's, never an operator's, and it does not emit 0 - but it
        // is the divisor in _pick(), so the guarantee is stated where it is
        // relied on rather than two files away.
        s.deadline = (b.period_ms != 0) ? b.period_ms : 1u;
        return s;
    }

    // A write has no period. It falls due a minimum interval after the last one
    // went out, so a chattering demand still cannot monopolise the bus, and
    // due_ms raises that floor further while a refusal penalty is running.
    const bool dirty = block_dirty(*_io, _plan, b);
    // last_ms 0 is NEVER WRITTEN, not "written at uptime 0". Adding an interval
    // to it made the floor an ABSOLUTE uptime, so at bring-up a block that has
    // never gone out waited the whole rewrite interval before it could: a relay
    // board that kept its power across a reset held its pre-crash coils for
    // rtu_rewrite_ms, which the band allows up to ten minutes. seed() has always
    // said this sentinel means overdue.
    const uint32_t interval = dirty ? _write_min_ms() : _rewrite_ms();
    // An interval of 0 is the operator saying STOP. Reachable, because an
    // override is unbanded on purpose, and the honest answer is to stop: a
    // block with nothing new to say simply never asks for the bus again. A
    // DIRTY block is unaffected - a demand that moved is not a cadence, and
    // rtu_write_min_ms 0 means "as soon as it moves".
    if (!dirty && interval == 0) { s.due = NEVER; s.deadline = 1u; return s; }

    const uint64_t floor_ms =
        (b.last_ms == 0) ? 0 : b.last_ms + interval;
    s.due      = (b.due_ms > floor_ms) ? b.due_ms : floor_ms;
    // A demand that has moved is owed to the field inside MODBUS_WRITE_DEADLINE_MS;
    // an unchanged one is only the keepalive a slave-side failsafe watches for,
    // so its own interval is the deadline.
    // Non-zero on both arms: the constant is, and the interval arm was checked
    // above. That is what keeps _pick()'s divide safe with no floor to impose.
    s.deadline = dirty ? Config::MODBUS_WRITE_DEADLINE_MS : interval;
    return s;
}

uint16_t RtuClient::_pick(uint64_t now_ms) const {
    // ONE clock for every block: how far it is through the lateness it is
    // allowed, in permille. Highest wins, ties to the lowest index. Reads and
    // writes differ in how their due time and deadline are DERIVED (_slot_of),
    // never in how they are compared, so a write's urgency grows like a read's
    // and starvation is unreachable. Urgency past 1000 is the starvation floor.
    //
    // Relative, not absolute: ranked by milliseconds the fast rungs would hold
    // the bus for ever, re-arming constantly while a slow block's lateness only
    // creeps. In permille 900 ms late on a 1 s rung is 900 and 800 ms on a 10 s
    // rung is 80 (fast still wins), but a 10 s block 5 s late is 500 and beats a
    // 1 s block 400 ms late.

    // At most ONE device that is not answering may be offered the bus, and only
    // when the gate is open. Computed once: a property of the beat, not of the
    // block. A probe is otherwise enormously late against its own period and
    // would win every comparison it entered.
    const uint8_t suspect = _next_suspect(now_ms);

    uint16_t best    = NO_BLOCK;
    uint32_t urgency = 0;

    for (uint16_t i = 0; i < _plan.block_count(); ++i) {
        const Block& b = _plan.block(i);
        if (_peer[b.peer].misses != 0 && b.peer != suspect) continue;
        if (!_due(b, now_ms)) continue;          // its device is in backoff

        const Slot s = _slot_of(b);
        if (now_ms < s.due) continue;            // not yet asking for the bus

        uint64_t permille = ((now_ms - s.due) * 1000u) / s.deadline;
        if (permille > 0xFFFFFFFFull) permille = 0xFFFFFFFFull;
        // `best == NO_BLOCK` and not `> urgency` alone: a block exactly at its
        // due time is urgency 0, and is still asking for the bus.
        if (best == NO_BLOCK || (uint32_t)permille > urgency) {
            urgency = (uint32_t)permille;
            best    = i;
        }
    }
    return best;
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

    // Four write codes, and the block already says which: the planner picks the
    // single form for a device whose model has no FC15/FC16 and caps those
    // blocks at one point. A single write echoes its VALUE rather than a count,
    // so the expectation carries it - that echo is the only confirmation the
    // point moved, on exactly the devices that cannot be driven another way.
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
        // layout they go out in, so they go straight across.
        _frame_len = build_write_registers(_frame, (uint16_t)sizeof _frame,
                                           b.device, b.reg, b.regs, payload);
        break;
    }
    return _frame_len != 0;
}

// The two conditions that together make a partial frame unreachable. The first
// is checked here, once; the second is enforced per transaction by the tx_idle()
// guard in on_output(), which will not start a frame until the ring has drained.
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

void RtuClient::on_output(uint64_t now_ms) {
    if (_io == nullptr || _port == nullptr) return;

    // The gap expires INTO this beat rather than costing one of its own: it is
    // inter-frame silence, and once it has elapsed there is nothing left to wait
    // for. Falling through is worth a whole beat per transaction.
    if (_state == MB_GAP) {
        if ((now_ms - _since_ms) < _gap_ms) return;
        _enter(MB_IDLE, now_ms);
    }

    // MB_RX belongs to on_input(): it owns the listening half, and it runs
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

    // Clear the receive path BEFORE the frame goes out: on_input() only listens
    // while MB_RX, so anything from the gap is still in the UART ring and arm()
    // resets the FRAMER, not the port. A device answering after its timeout
    // would otherwise be read as the head of the NEXT transaction. Here rather
    // than after _send(), where it would eat our own echo.
    _stale_rx += HAL::Serial::fieldbus_flush_input();

    if (!_send()) {
        _tx_short++;
        _faults++;
        _missed(_plan.block(pick), now_ms);
        _active = NO_BLOCK;
        _enter(MB_GAP, now_ms);
        return;
    }

    // Armed HERE, not a beat later: the answer can be on the wire before this
    // beat ends (at 38400 an 8-byte request clears in 2.3 ms) and anything not
    // being listened for is lost. Whether our own frame comes back first is a
    // property of the PORT, not something to infer from the bytes: a transceiver
    // with RE tied low echoes, the simulator's socketpair does not, and HAL
    // knows which. Straight to MB_RX - the tx_idle() check above is what stops
    // the NEXT frame starting before this one has gone.
    // The deadline is the DEVICE's where it declares one, beside the gap that
    // comes from the same model. A unit slower than the bus default is otherwise
    // indistinguishable from a dead one, and no amount of probing fixes it.
    const DeviceModel& m = model_of(_devices, _plan.block(pick).device);
    _framer.arm(_expect, now_ms,
                (m.timeout_ms != 0) ? m.timeout_ms : _timeout_ms(),
                HAL::Serial::fieldbus_echoes() ? _frame_len : 0);
    _enter(MB_RX, now_ms);
}

void RtuClient::on_input(uint64_t now_ms) {
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

    // Before _settle(), which clears the miss count on an answer: the cursor has
    // to move on from a device it has just spoken to whether or not that device
    // turned out to still be down.
    const bool was_suspect = (_active != NO_BLOCK) &&
                             _peer[_plan.block(_active).peer].misses != 0;

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

    // A transaction that produced no answer costs the bus a full response
    // timeout, so the next one to a device that is not answering owes the live
    // devices some traffic first. Any failure arms it, not only a probe's: the
    // FIRST miss of each device is what a mass outage is made of.
    if (r != 1) _probe_gate_ms = now_ms + Config::MODBUS_PROBE_SPACING_MS;

    // Whatever happened, this block has had its turn. A read comes round again
    // on its period; a failed write stays dirty and will be retried at the
    // minimum interval.
    if (_active != NO_BLOCK) {
        Block& b = _plan.block(_active);
        if (is_read(b.fc)) {
            uint32_t next = b.period_ms;
            // A miss must not push the retry out by a whole poll period: that is
            // what made declaring a device dead cost two of them, so two minutes
            // on the 60 s rung. Capped, the latency is the fault's and not the
            // rung's. The gate above is what keeps the shorter retry affordable.
            if (r != 1 && next > Config::MODBUS_RETRY_DELAY_MS)
                next = Config::MODBUS_RETRY_DELAY_MS;
            b.due_ms = now_ms + next;
        }
        b.last_ms = now_ms;
        // Round-robin: the next probe goes to a different device, however late
        // this one's blocks have become.
        if (was_suspect && _plan.device_count() != 0)
            _probe_cursor = (uint8_t)((b.peer + 1u) % _plan.device_count());
    }
    // How much silence this device wants before the next frame, which some
    // models specify well above the protocol's 3.5 character times.
    if (_active != NO_BLOCK) {
        const DeviceModel& m = model_of(_devices, _plan.block(_active).device);
        _gap_ms = (m.min_gap_ms > Config::MODBUS_GAP_MS) ? m.min_gap_ms
                                                         : Config::MODBUS_GAP_MS;
    }
    // Once per transaction, not only on a health transition: the two counter
    // rows move with the frames and not with the device set.
    _publish_health();

    _framer.disarm();
    _active = NO_BLOCK;
    _enter(MB_GAP, now_ms);
}

void RtuClient::_settle(uint64_t now_ms) {
    if (_active == NO_BLOCK) return;
    Block& b = _plan.block(_active);

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
        // Q_VALID until expire_stale() catches up - and a row declared with no
        // timeout is never caught up with at all.
        //
        // Only the LOG is edge-triggered below; a read block needs no penalty
        // because it already waits for its own poll period.
        if (is_read(b.fc)) invalidate_block(*_io, _plan, b, Vars::Q_REFUSED);

        // A refused write stays dirty - correctly, since it never reached the
        // field - so nothing else would ever slow it down. Back the BLOCK off,
        // doubling, while the device keeps its place in the rotation.
        uint32_t wait = 0;
        if (!is_read(b.fc)) {
            wait = _write_min_ms();
            for (uint8_t i = 0; i < b.refusals && wait < Config::MODBUS_REFUSAL_MAX_MS; ++i)
                wait *= 2;
            if (wait > Config::MODBUS_REFUSAL_MAX_MS) wait = Config::MODBUS_REFUSAL_MAX_MS;
            b.due_ms = now_ms + wait;
            if (b.refusals < 255) b.refusals++;
        }

        // Once, on the way in, like _missed(): a map that does not match the
        // device is refused on every attempt, and at five a second that is the
        // rate limiter's whole budget spent on one wrong register.
        const bool first = is_read(b.fc) || b.refusals == 1;
        if (_logger != nullptr && first) {
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
        // The payload is still where the builder left it, for as long as that
        // frame is the request in flight. Handing it back is what lets
        // clear_dirty() tell a row this transaction carried from one a program
        // moved while it was in flight.
        //
        // Where it sits depends on the code. FC15/FC16 have a byte count, so
        // the payload starts at 7; FC05/FC06 carry their one value at 4 with no
        // count. A single-write block is one point by construction (span_cap),
        // so the row's offset within it is 0 and one value is the whole payload
        // - and FC05's 0xFF00/0x0000 has the same bit 0 the packed form would.
        const bool     single = (b.fc == FC_WRITE_COIL || b.fc == FC_WRITE_REGISTER);
        const uint16_t head   = single ? 4u : 7u;   // unit, fc, addr[, qty, count]
        const uint16_t sent   = single
                                  ? (b.fc == FC_WRITE_COIL ? 1u : 2u)
                                  : ((_frame_len > head + 2u)
                                         ? (uint16_t)(_frame_len - head - 2u) : 0u);
        clear_dirty(*_io, _plan, b, sent != 0 ? _frame + head : nullptr, sent);
        // Accepted, so whatever the map was wrong about has been fixed. The
        // penalty starts from the base again rather than from where it stopped.
        b.refusals = 0;
    }
}

// Deliberately without a consumer: nothing withdraws control authority on a
// field fault, and nothing should. A failed humidity sensor raises an alarm; it
// does not shut a greenhouse down. This is reported (the shell, the status bar,
// alarm_fieldbus), not acted on.
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

namespace Botanik { namespace Modbus {



    void RtuClient::register_tasks() {
        // The field bus commits its readings ahead of the bank's expiry sweep,
        // which must judge the freshest image and not the one from before this
        // beat's answers.
        _tasks->add(*this, "sys_mbrtu_rx",
              Tasks::EVERY_BEAT, Tasks::PHASE_INPUT);
        _tasks->add(*this, "sys_mbrtu_tx",
              Tasks::EVERY_BEAT, Tasks::PHASE_OUTPUT);
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
