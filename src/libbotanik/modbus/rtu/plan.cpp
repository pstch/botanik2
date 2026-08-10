/**
 * @file modbus/rtu/plan.cpp
 * @brief @see plan.h
 */

#include "libbotanik/modbus/rtu/plan.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

#include "libbotanik/tasks/task_timer.h"   // Tasks::stagger_offset - one rule for spreading a set
#include "libbotanik/vars/model.h"             // Vars::Bank / Variable - the table this reads

namespace Botanik { namespace Modbus {

namespace {

    /** @brief What a row sorts and groups by. Two rows share a block only if
     *  all four agree, and then only if their registers are close enough. */
    struct Key {
        uint8_t  device;
        uint8_t  fc;
        uint8_t  rung;
        uint16_t reg;
    };

    /** @brief The function code a row's block is ISSUED with, which is not
     *  always the one it declares.
     *
     *  Reads use theirs as-is: FC03 and FC04 address different register spaces
     *  and must never share a block. Writes are declared one at a time (a relay
     *  row is an FC05 coil) and group under the multi-write code, unless
     *  `DeviceModel::write_multiple` says FC15/FC16 do not exist there, in which
     *  case the blocks are cut at one point (span_cap) and issued as FC05/FC06.
     *  Decided HERE, not at build time, so `Expect::fc` matches: the client sets
     *  it straight from the block.
     *
     *  @return 0 for a code we cannot plan, which drops the row. */
    uint8_t block_fc(uint8_t fc, const DeviceModel& m) {
        if (is_read(fc)) return fc;
        if (fc == FC_WRITE_COIL || fc == FC_WRITE_COILS)
            return m.write_multiple ? FC_WRITE_COILS : FC_WRITE_COIL;
        if (fc == FC_WRITE_REGISTER || fc == FC_WRITE_REGISTERS)
            return m.write_multiple ? FC_WRITE_REGISTERS : FC_WRITE_REGISTER;
        return 0;
    }

    /** @brief The rung a row polls on.
     *
     *  Writes have none: they go out when a control program demands something,
     *  so they all share rung 0 and their block's period stays 0.
     *
     *  A read declaring no timeout at all gets the SLOWEST rung - though an
     *  INPUT declaring none is refused outright in _collect(), because a row
     *  expire_stale() skips can never be invalidated by anything. This is the
     *  fallback for the other roles. */
    uint8_t rung_of(const Vars::Variable& d) {
        if (!is_read(d.bind.fc)) return 0;
        // poll_ms and timeout_ms are two facts: how often to spend the bus, and
        // how long a reading may be believed. A row stating the first gets it
        // honoured rather than back-computed from the second.
        if (d.poll_ms != 0) return (uint8_t)ladder_of(d.poll_ms);
        if (d.timeout_ms == 0) return (uint8_t)L_60S;
        return (uint8_t)ladder_of(period_of(d.timeout_ms));
    }

    Key key_of(const Vars::Variable& d, const DeviceTable* t) {
        Key k;
        k.device = d.bind.device;
        k.fc     = block_fc(d.bind.fc, model_of(t, d.bind.device));
        k.rung   = rung_of(d);
        k.reg    = d.bind.reg;
        return k;
    }

    /** @brief Sort order: device, then function code, then rung, then register.
     *  Register last, because that is the axis blocks are cut along. */
    bool before(const Key& a, const Key& b) {
        if (a.device != b.device) return a.device < b.device;
        if (a.fc     != b.fc)     return a.fc     < b.fc;
        if (a.rung   != b.rung)   return a.rung   < b.rung;
        return a.reg < b.reg;
    }

    /** @brief One past a row's last register. A row may span two (a 32-bit
     *  value), and a declared span of 0 still occupies its own register. */
    uint16_t row_end(const Vars::Variable& d) {
        return (uint16_t)(d.bind.reg + (d.bind.regs ? d.bind.regs : 1));
    }

    /** @brief The most items one block of @p fc may span.
     *
     *  Registers are held under the protocol's own ceiling so a full block still
     *  fits the UART buffer in one go. Coils are not: eight to a byte, their
     *  protocol ceiling binds long before any buffer does. */
    uint16_t span_cap(uint8_t fc, const DeviceModel& m) {
        // FC05 and FC06 carry exactly one point, so a device without FC15/FC16
        // gets one-point write blocks: two adjacent dirty rows must not be
        // coalesced into a block the wire form cannot express.
        if (fc == FC_WRITE_COIL || fc == FC_WRITE_REGISTER) return 1;

        uint16_t proto = max_quantity(fc);
        // What the DEVICE will answer, when that is tighter than the protocol.
        // The SDM630 refuses past 80 registers with an exception, which on a bus
        // looks exactly like a broken cable.
        const uint16_t dev = is_bit(fc) ? m.max_bits : m.max_regs;
        if (dev != 0 && dev < proto) proto = dev;
        // Bits are eight to a byte, so their protocol ceiling is far above what
        // the collect buffer holds (1968 coils against 240 bytes). Capping at
        // the buffer stops a wide coil block silently failing to build on every
        // turn.
        const uint16_t buf = is_bit(fc)
                                 ? (uint16_t)(Config::MODBUS_MAX_REGS_PER_BLOCK * 16)
                                 : Config::MODBUS_MAX_REGS_PER_BLOCK;
        return (proto < buf) ? proto : buf;
    }

}  // namespace

void Planner::plan(const Vars::Bank& io, uint32_t baud, uint32_t turnaround_ms,
                   const DeviceTable* devices) {
    _devices        = devices;
    _row_count      = 0;
    _block_count    = 0;
    _rows_dropped   = 0;
    _rows_unscanned = 0;
    _device_count   = 0;
    _baud           = baud;
    _turnaround_us  = turnaround_ms * 1000u;
    _merge_gap      = merge_gap(baud, _turnaround_us);

    _collect(io);
    _sort(io);
    _cut(io);
}

void Planner::_collect(const Vars::Bank& io) {
    for (uint16_t i = 0; i < io.count(); ++i) {
        const Vars::Variable* d = io.var(i).meta();
        if (d->bind.transport != Vars::XP_MODBUS) continue;
        // A row whose code we cannot issue is not a row we can plan. Counted
        // with the overflow: to an operator both mean a row that is not scanned.
        if (block_fc(d->bind.fc, model_of(_devices, d->bind.device)) == 0) {
            _rows_dropped++;
            continue;
        }
        // An input that can never go stale is a trap: expire_stale() skips
        // timeout_ms == 0, so a row would hold its last reading at Q_GOOD for
        // the life of the firmware once its device went quiet. Refused loudly
        // instead, since _rows_dropped is a bring-up critical.
        if (d->role == Vars::ROLE_INPUT && is_read(d->bind.fc) &&
            d->timeout_ms == 0) { _rows_dropped++; continue; }
        if (_row_count >= Config::MODBUS_MAX_ROWS) { _rows_dropped++; continue; }
        _rows[_row_count++] = i;
    }
}

void Planner::_sort(const Vars::Bank& io) {
    // Insertion sort: the table is tens of rows, this runs once at bring-up, and
    // it needs no scratch. The bank's own name index is built the same way.
    for (uint16_t i = 1; i < _row_count; ++i) {
        const uint16_t v = _rows[i];
        const Key kv = key_of(*io.var(v).meta(), _devices);
        uint16_t j = i;
        while (j > 0 && before(kv, key_of(*io.var(_rows[j - 1]).meta(), _devices))) {
            _rows[j] = _rows[j - 1];
            --j;
        }
        _rows[j] = v;
    }
}

void Planner::_cut(const Vars::Bank& io) {
    uint16_t i = 0;
    while (i < _row_count) {
        if (_block_count >= Config::MODBUS_MAX_BLOCKS) {
            // Everything left is unreachable. Counted in ROWS, the unit every
            // drop path here uses and the one an operator can look for.
            _rows_unscanned = (uint16_t)(_rows_unscanned + (_row_count - i));
            return;
        }

        const Vars::Variable& first = *io.var(_rows[i]).meta();
        const Key k = key_of(first, _devices);
        const DeviceModel& m = model_of(_devices, k.device);
        const uint16_t cap = span_cap(k.fc, m);

        Block b;
        b.device     = k.device;
        b.fc         = k.fc;
        b.reg        = k.reg;
        b.first_row  = i;
        b.row_count  = 1;
        b.period_ms  = is_read(k.fc) ? ladder_ms(k.rung) : 0;
        b.due_ms     = 0;
        b.last_ms    = 0;

        uint16_t end = row_end(first);
        uint16_t j   = i + 1;
        while (j < _row_count) {
            const Vars::Variable& d = *io.var(_rows[j]).meta();
            const Key kj = key_of(d, _devices);
            if (kj.device != k.device || kj.fc != k.fc || kj.rung != k.rung) break;

            // Sorted by register, so anything at or below the current end merely
            // overlaps and costs nothing to absorb. The gap is a read-side
            // economy only: a merged write zero-fills it and would command
            // registers no row claims.
            const uint16_t allow = is_read(k.fc) ? _merge_gap : 0;
            const uint16_t gap = (kj.reg > end) ? (uint16_t)(kj.reg - end) : 0;
            if (gap > allow) break;

            const uint16_t next_end = (row_end(d) > end) ? row_end(d) : end;
            if ((uint16_t)(next_end - b.reg) > cap) break;

            end = next_end;
            b.row_count++;
            ++j;
        }

        b.regs = (uint16_t)(end - b.reg);

        // The merge loop bounds what it ADDS by cap, but the first row is
        // admitted unconditionally, so a single row wider than the device will
        // accept lands here over-wide. Reads are caught after their alignment
        // padding below; a write must be dropped, because span_cap() is 1 on a
        // device without FC16 and a 2-register setpoint would go out as its HIGH
        // WORD ALONE, with a matching echo (the echo carries the address and
        // value that were sent) and no diagnostic anywhere. Dropping makes it a
        // bring-up critical through rows_unscanned() instead.
        if (!is_read(k.fc) && b.regs > cap) {
            _rows_unscanned = (uint16_t)(_rows_unscanned + (j - i));
            i = j;
            continue;
        }

        // The device set, in first-seen order. Bounded by the block table, so a
        // block that exists always has a peer and no unit escapes the backoff.
        b.peer = _device_count;
        for (uint8_t p = 0; p < _device_count; ++p)
            if (_devices_seen[p] == b.device) { b.peer = p; break; }
        if (b.peer == _device_count) _devices_seen[_device_count++] = b.device;

        // Widen a READ out to what this model insists on being asked for: a
        // device storing 32-bit values in register pairs answers exception 02 to
        // a request landing in the middle of one, so the request grows to the
        // boundary and the extra registers are read and ignored. Reads only -
        // padding a WRITE would command registers no row claims, so a misaligned
        // write block is left alone and shows up as a refusal.
        if (is_read(k.fc) && !is_bit(k.fc)) {
            const uint16_t aa = m.addr_align > 1 ? m.addr_align : 1;
            const uint16_t qa = m.qty_align  > 1 ? m.qty_align  : 1;
            const uint16_t lead = (uint16_t)(b.reg % aa);
            b.reg  = (uint16_t)(b.reg - lead);
            b.regs = (uint16_t)(b.regs + lead);
            const uint16_t tail = (uint16_t)(b.regs % qa);
            if (tail != 0) b.regs = (uint16_t)(b.regs + (qa - tail));
            // Only if the padding still fits what the device will answer. The
            // whole block goes, so it is that block's ROWS that are unscanned.
            if (b.regs > cap) {
                _rows_unscanned = (uint16_t)(_rows_unscanned + (j - i));
                i = j;
                continue;
            }
        }

        _blocks[_block_count++] = b;
        i = j;
    }
}

void Planner::seed(uint64_t now_ms) {
    for (uint16_t i = 0; i < _block_count; ++i) {
        _blocks[i].due_ms = now_ms + Tasks::stagger_offset(
                                         i, Config::MODBUS_BLOCK_STAGGER_MS,
                                         _blocks[i].period_ms);
        // Zero rather than now_ms, so every write block reads as overdue for its
        // unconditional rewrite: the outputs must be driven to their safe state
        // once at bring-up, not first at the rewrite interval.
        _blocks[i].last_ms = 0;
    }
}

uint32_t Planner::wire_duty_ppm() const {
    uint32_t ppm = 0;
    for (uint16_t i = 0; i < _block_count; ++i) {
        const Block& b = _blocks[i];
        if (!is_read(b.fc) || b.period_ms == 0) continue;
        // us per activation, over ms of period, is already parts per thousand;
        // one more factor of a thousand makes it ppm. Integer throughout, and
        // the numerator cannot overflow: a transaction is under a second.
        ppm += (txn_us(_baud, _turnaround_us, b.fc, b.regs) * 1000u) / b.period_ms;
    }
    return ppm;
}

uint32_t Planner::duty_ppm() const {
    const uint32_t beat_us = (uint32_t)Config::GROUP_FAST_MS * 1000u;
    uint32_t ppm = 0;
    for (uint16_t i = 0; i < _block_count; ++i) {
        const Block& b = _blocks[i];
        if (!is_read(b.fc) || b.period_ms == 0) continue;
        // Whichever is larger: the frame's own byte time, or the beat it is
        // quantised to. One transaction at a time, advanced from the beat, so a
        // frame shorter than a beat still costs one.
        uint32_t cost = txn_us(_baud, _turnaround_us, b.fc, b.regs);
        if (cost < beat_us) cost = beat_us;
        ppm += (cost * 1000u) / b.period_ms;
    }
    return ppm;
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
