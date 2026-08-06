/**
 * @file modbus/tcp/map.cpp
 * @brief Map: the scan that builds it, the lookup, and the type conversion.
 *
 * The conversion is the part worth reading twice. Every row occupies exactly two
 * registers whatever its type, so the wire form is always 32 bits: a float is
 * its IEEE-754 bits, an unsigned is zero-extended, a signed is sign-extended,
 * and the 64-bit types saturate. Uniform width is what makes an address derivable
 * from a row number, which is what keeps the map stable when a row is inserted.
 */
#include "libbotanik/modbus/tcp/map.h"    // Map - implemented here

#include <cstring>                    // memcpy - moving float bits without aliasing

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

namespace Botanik { namespace Modbus {

    using namespace Vars;

    namespace {

        /** @brief The first of the two registers row @p reg occupies. */
        uint32_t reg_to_addr(uint16_t reg) { return (uint32_t)reg * 2u; }

        /** @brief What a map with no bank hands back: an inert row, so a caller
         *  that ignores count() reads zeros rather than dereferencing null. The
         *  bank has its own sink for the same reason. */
        const Variable g_sink_d = {};
        Value          g_sink_v = {};

        /** @brief 32-bit saturation for the types that do not fit. A truncated
         *  value would read as a plausible small number; a saturated one reads as
         *  "at least this", which is what a rail means everywhere else here. */
        uint32_t sat_u64(uint64_t v) {
            return (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)v;
        }
        uint32_t sat_i64(int64_t v) {
            if (v >  2147483647LL)  return (uint32_t)2147483647L;
            if (v < -2147483648LL)  return (uint32_t)(-2147483647L - 1);
            return (uint32_t)(int32_t)v;
        }

        /** @brief The 32-bit value the register pair at offset @p i of a write
         *  payload carries. High word first, in the ONE place that says so. */
        uint32_t word_at(const uint8_t* data, uint16_t i) {
            return ((uint32_t)rd16(data + i * 2) << 16) | rd16(data + i * 2 + 2);
        }

        /** @brief Whether a master may write this row at all. Not the same
         *  question as whether a VALUE is acceptable - that one is the bank's,
         *  and Bank::check() answers it. */
        bool writable(const Variable& d) {
            return d.role == ROLE_PARAM && (d.flags & F_RDONLY) == 0 &&
                   d.type != T_STR;
        }

    }  // namespace

    bool Map::published(const Variable& d) {
        // WHICH KINDS of row carry a register is a rule about the table, so it
        // lives with the other such rules (vars/access.h) rather than here; a
        // row of one of those kinds with no register is a table defect the
        // site test fails on, and this simply does not serve it.
        return var_mapped(d) && d.bind.reg != 0;
    }

    void Map::build(Vars::Bank* io) {
        if (io != nullptr) _io = io;
        _count = 0; _overflow = 0; _reg_lo = 0; _reg_hi = 0;
        if (_io == nullptr) return;

        for (uint16_t i = 0; i < _io->count(); i++) {
            const Variable& d = *_io->var(i).meta();
            if (!published(d)) continue;
            if (_count >= Config::MODBUS_TCP_MAX_ROWS) { _overflow++; continue; }
            // Insertion sort by register. The table is a couple of hundred rows
            // and this runs once at bring-up, so the simple thing is the right
            // thing; what matters is that the result is ordered, since every
            // lookup binary-searches it.
            uint16_t k = _count;
            while (k > 0 && _io->var(_rows[k - 1]).meta()->bind.reg > d.bind.reg) {
                _rows[k] = _rows[k - 1];
                k--;
            }
            _rows[k] = i;
            _count++;
        }
        if (_count > 0) {
            _reg_lo = _io->var(_rows[0]).meta()->bind.reg;
            _reg_hi = _io->var(_rows[_count - 1]).meta()->bind.reg;
        }
    }

    Var Map::row(uint16_t i) const {
        if (_io == nullptr || i >= _count) return Var(&g_sink_d, &g_sink_v);
        return _io->var(_rows[i]);
    }

    Var Map::row_at(uint16_t reg, bool* found) const {
        const int k = (_io == nullptr) ? -1 : _find(reg);
        if (found != nullptr) *found = (k >= 0);
        return (k < 0) ? Var(&g_sink_d, &g_sink_v) : _io->var(_rows[k]);
    }

    uint16_t Map::_reg_of(uint16_t i) const {
        return (i < _count) ? _io->var(_rows[i]).meta()->bind.reg : (uint16_t)0xFFFF;
    }

    uint16_t Map::_lower_bound(uint16_t reg) const {
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)(lo + (hi - lo) / 2);
            if (_reg_of(mid) < reg) lo = (uint16_t)(mid + 1);
            else                    hi = mid;
        }
        return lo;
    }

    int Map::_find(uint16_t reg) const {
        int lo = 0, hi = (int)_count - 1;
        while (lo <= hi) {
            const int mid = lo + (hi - lo) / 2;
            const uint16_t r = _io->var(_rows[mid]).meta()->bind.reg;
            if (r == reg) return mid;
            if (r <  reg) lo = mid + 1;
            else          hi = mid - 1;
        }
        return -1;
    }

    uint32_t Map::to_wire(const Var& v) {
        const Variable* d = v.meta();
        return (d == nullptr) ? 0u : wire_of(d->type, v.raw());
    }

    uint32_t Map::wire_of(VarType type, Scalar s) {
        switch (type) {
            case T_F32: {
                uint32_t bits = 0;
                const float f = s.f;
                memcpy(&bits, &f, 4);      // not a reinterpret_cast: that aliases
                return bits;
            }
            case T_BOOL: return (s.u != 0) ? 1u : 0u;
            case T_U8: case T_U16: case T_U32: case T_ENUM: return (uint32_t)s.u;
            case T_U64: return sat_u64(s.u);
            case T_I8: case T_I16: case T_I32: return (uint32_t)(int32_t)s.i;
            case T_I64: return sat_i64(s.i);
            case T_STR: default: return 0;  // not mapped; reads as zeros
        }
    }

    Scalar Map::from_wire(VarType type, uint32_t w) {
        switch (type) {
            case T_F32: {
                float f = 0.0f;
                memcpy(&f, &w, 4);
                return Scalar::F(f);
            }
            case T_BOOL: return Scalar::B(w != 0);
            case T_U8: case T_U16: case T_U32: case T_U64: case T_ENUM:
                return Scalar::U((uint64_t)w);
            case T_I8: case T_I16: case T_I32: case T_I64:
                return Scalar::I((int64_t)(int32_t)w);
            case T_STR: default: return Scalar();
        }
    }

    Exception Map::read_registers(uint16_t addr, uint16_t count, uint8_t* out,
                                  size_t cap) const {
        if (out == nullptr || cap < (size_t)count * 2) return EX_SLAVE_FAILURE;
        if (_io == nullptr || _count == 0) return EX_ILLEGAL_ADDRESS;

        const uint32_t first = addr;
        const uint32_t last  = (uint32_t)addr + count - 1;
        // Only a request that misses the map ENTIRELY is refused; a block read
        // straddling a gap is the normal way a master reads.
        if (last < reg_to_addr(_reg_lo) || first > reg_to_addr(_reg_hi) + 1)
            return EX_ILLEGAL_ADDRESS;

        uint16_t k = _lower_bound((uint16_t)(first / 2));   // once, not per register
        uint32_t wire = 0;
        uint16_t wire_reg = 0xFFFF;
        for (uint16_t i = 0; i < count; i++) {
            const uint32_t a = first + i;
            const uint16_t reg = (uint16_t)(a / 2);
            while (k < _count && _reg_of(k) < reg) k++;
            uint16_t word = 0;
            if (_reg_of(k) == reg) {
                // Read the row once and use it for both its registers: the value
                // is an override-aware read, not a field fetch.
                if (wire_reg != reg) { wire = to_wire(_io->var(_rows[k])); wire_reg = reg; }
                // High word first: Modbus is big-endian inside a value as well
                // as inside a register.
                word = (a % 2 == 0) ? (uint16_t)(wire >> 16) : (uint16_t)(wire & 0xFFFF);
            }
            wr16(out + i * 2, word);
        }
        return EX_NONE;
    }

    Exception Map::read_bits(uint16_t addr, uint16_t count, BitTable table,
                             uint8_t* out, size_t cap) const {
        if (out == nullptr || cap < bit_bytes(count)) return EX_SLAVE_FAILURE;
        if (_io == nullptr || _count == 0) return EX_ILLEGAL_ADDRESS;

        const uint32_t first = addr;
        const uint32_t last  = (uint32_t)addr + count - 1;
        if (last < _reg_lo || first > _reg_hi) return EX_ILLEGAL_ADDRESS;

        for (uint16_t i = 0; i < bit_bytes(count); i++) out[i] = 0;
        uint16_t k = _lower_bound((uint16_t)first);
        for (uint16_t i = 0; i < count; i++) {
            const uint16_t reg = (uint16_t)(first + i);
            while (k < _count && _reg_of(k) < reg) k++;
            if (_reg_of(k) != reg) continue;           // a gap reads 0
            const Var v = _io->var(_rows[k]);
            if (table == BITS_QUALITY) {
                put_bit(out, i, v.valid());
            } else {
                // A coil is a BOOLEAN row's value. A float at that number is not
                // a coil, and reporting "non-zero" for it would invent a signal
                // the table never declared.
                put_bit(out, i, v.type() == T_BOOL && v.b());
            }
        }
        return EX_NONE;
    }

    Exception Map::write_registers(uint16_t addr, uint16_t count,
                                   const uint8_t* data, uint16_t* committed) {
        if (committed != nullptr) *committed = 0;
        if (data == nullptr) return EX_SLAVE_FAILURE;
        if (_io == nullptr || _count == 0) return EX_ILLEGAL_ADDRESS;
        // A row is two registers, so a write must start on an even address and
        // cover whole rows. Half a 32-bit value committed as a whole one is a
        // setpoint nobody asked for.
        if ((addr % 2) != 0 || (count % 2) != 0) return EX_ILLEGAL_ADDRESS;

        // Two passes: validate every row first, commit only once all of them
        // pass. A multi-register write that failed halfway would leave the plant
        // on a mixture of the old settings and the new, with an exception saying
        // only that something went wrong - and the band check is exactly the one
        // that fails on the last row of a block a master sent in good faith.
        for (uint16_t i = 0; i < count; i += 2) {
            bool found = false;
            const Var v = row_at((uint16_t)((addr + i) / 2), &found);
            // Two different refusals, and the difference matters to a master: an
            // address it may not write is a map question, a value out of band is
            // a value question.
            if (!found || !writable(*v.meta())) return EX_ILLEGAL_ADDRESS;
            if (_io->check(v, from_wire(v.type(), word_at(data, i))) != SET_OK)
                return EX_ILLEGAL_VALUE;
        }

        for (uint16_t i = 0; i < count; i += 2) {
            Var v = row_at((uint16_t)((addr + i) / 2));
            const uint32_t w = word_at(data, i);
            // Against live(), not raw(): if the row is overridden, raw() is the
            // forced value, and a master writing that same number would skip the
            // commit while the parameter underneath stayed where it was.
            if (w == wire_of(v.type(), v.live())) continue;   // no commit, no wear
            // Cannot fail on validity - the pass above just checked every row -
            // so a non-OK result here is the STORE refusing, which is not a
            // rejected edit: the value is live, and the fault raised its own
            // alarm on the way out.
            (void)_io->set(v, from_wire(v.type(), w));
            if (committed != nullptr) (*committed)++;
        }
        return EX_NONE;
    }

    Exception Map::write_bit(uint16_t addr, bool on, uint16_t* committed) {
        if (committed != nullptr) *committed = 0;
        if (_io == nullptr || _count == 0) return EX_ILLEGAL_ADDRESS;

        bool found = false;
        Var v = row_at(addr, &found);
        const Variable& d = *v.meta();
        // A coil is a BOOLEAN parameter. An alarm publishes as a coil too, and is
        // read-only: acknowledging one is a different block entirely.
        if (!found || !writable(d) || d.type != T_BOOL) return EX_ILLEGAL_ADDRESS;
        if (as_bool(d.type, v.live()) == on) return EX_NONE;   // no-op, as above
        if (_io->set(v, Scalar::B(on)) == SET_INVALID) return EX_ILLEGAL_VALUE;
        if (committed != nullptr) (*committed)++;
        return EX_NONE;
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER
