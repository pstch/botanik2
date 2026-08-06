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
     *  row is an FC05 coil) but are always issued coalesced, so they group under
     *  the multi-write code and the client degrades to the single-write form
     *  when only one row turned out to be dirty.
     *
     *  @return 0 for a code we cannot plan, which drops the row. */
    uint8_t block_fc(uint8_t fc) {
        if (is_read(fc)) return fc;
        if (fc == FC_WRITE_COIL || fc == FC_WRITE_COILS) return FC_WRITE_COILS;
        if (fc == FC_WRITE_REGISTER || fc == FC_WRITE_REGISTERS) return FC_WRITE_REGISTERS;
        return 0;
    }

    /** @brief The rung a row polls on.
     *
     *  Writes have none: they go out when a control program demands something,
     *  so they all share rung 0 and their block's period stays 0.
     *
     *  A read declaring no timeout at all gets the SLOWEST rung. It never goes
     *  stale, so nothing is asking to be told promptly, and the alternative
     *  reading - that timeout 0 means poll flat out - would put a row nobody
     *  waits on at the top of the bus. */
    uint8_t rung_of(const Vars::Variable& d) {
        if (!is_read(d.bind.fc)) return 0;
        if (d.timeout_ms == 0) return (uint8_t)L_60S;
        return (uint8_t)ladder_of(period_of(d.timeout_ms));
    }

    Key key_of(const Vars::Variable& d) {
        Key k;
        k.device = d.bind.device;
        k.fc     = block_fc(d.bind.fc);
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
    uint16_t span_cap(uint8_t fc) {
        const uint16_t proto = max_quantity(fc);
        if (is_bit(fc)) return proto;
        return (proto < Config::MODBUS_MAX_REGS_PER_BLOCK)
                   ? proto : Config::MODBUS_MAX_REGS_PER_BLOCK;
    }

}  // namespace

void Planner::plan(const Vars::Bank& io, uint32_t baud, uint32_t turnaround_ms) {
    _row_count      = 0;
    _block_count    = 0;
    _rows_dropped   = 0;
    _blocks_dropped = 0;
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
        // with the overflow, because both mean the same thing to an operator:
        // something in the table is not going to be scanned.
        if (block_fc(d->bind.fc) == 0) { _rows_dropped++; continue; }
        if (_row_count >= Config::MODBUS_MAX_ROWS) { _rows_dropped++; continue; }
        _rows[_row_count++] = i;
    }
}

void Planner::_sort(const Vars::Bank& io) {
    // Insertion sort: the table is tens of rows, this runs once at bring-up, and
    // it needs no scratch. The bank's own name index is built the same way.
    for (uint16_t i = 1; i < _row_count; ++i) {
        const uint16_t v = _rows[i];
        const Key kv = key_of(*io.var(v).meta());
        uint16_t j = i;
        while (j > 0 && before(kv, key_of(*io.var(_rows[j - 1]).meta()))) {
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
            // Everything left is unreachable. Count the rows rather than the
            // blocks we did not make: rows are what an operator can look for.
            _blocks_dropped = (uint16_t)(_row_count - i);
            return;
        }

        const Vars::Variable& first = *io.var(_rows[i]).meta();
        const Key k = key_of(first);
        const uint16_t cap = span_cap(k.fc);

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
            const Key kj = key_of(d);
            if (kj.device != k.device || kj.fc != k.fc || kj.rung != k.rung) break;

            // Sorted by register, so anything at or below the current end merely
            // overlaps and costs nothing to absorb.
            const uint16_t gap = (kj.reg > end) ? (uint16_t)(kj.reg - end) : 0;
            if (gap > _merge_gap) break;

            const uint16_t next_end = (row_end(d) > end) ? row_end(d) : end;
            if ((uint16_t)(next_end - b.reg) > cap) break;

            end = next_end;
            b.row_count++;
            ++j;
        }

        b.regs = (uint16_t)(end - b.reg);
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

uint32_t Planner::sweep_us() const {
    uint32_t total = 0;
    for (uint16_t i = 0; i < _block_count; ++i) {
        const Block& b = _blocks[i];
        if (!is_read(b.fc)) continue;
        total += txn_us(_baud, _turnaround_us, b.fc, b.regs);
    }
    return total;
}

uint32_t Planner::duty_ppm() const {
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

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
