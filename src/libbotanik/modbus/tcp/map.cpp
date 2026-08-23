/**
 * @file modbus/tcp/map.cpp
 * @brief Map: the scan that builds it, the lookup, and the type conversion.
 *
 * Every row occupies exactly two registers whatever its type, so the wire form
 * is always 32 bits: a float is its IEEE-754 bits, an unsigned is zero-extended,
 * a signed is sign-extended, and the 64-bit types saturate.
 * @see map.h for what an address means.
 */
#include "libbotanik/modbus/tcp/map.h"

#include <cstdio>
#include <cstring>

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

namespace Botanik { namespace Modbus {

    using namespace Vars;

    namespace {

        /** @brief The first of the two registers row @p reg occupies. */
        uint32_t reg_to_addr(uint16_t reg) { return (uint32_t)reg * 2u; }

        /** @brief What a map with no bank hands back: an inert row, so a caller
         *  that ignores count() reads zeros rather than dereferencing null. The
         *  bank has its own sink for the same reason. */
        const Descriptor g_sink_d = {};
        Slot          g_sink_v = {};

        /** @brief 32-bit saturation for the types that do not fit. Truncation
         *  would read as a plausible small number; a rail reads as "at least
         *  this". */
        uint32_t sat_u64(uint64_t v) {
            return (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)v;
        }
        uint32_t sat_i64(int64_t v) {
            if (v >  2147483647LL)  return (uint32_t)2147483647L;
            if (v < -2147483648LL)  return (uint32_t)(-2147483647L - 1);
            return (uint32_t)(int32_t)v;
        }

        /** @brief Whether this row is a dotted quad, which IS served: four
         *  octets fit the 32 bits every row already has. A HOSTNAME does not,
         *  which is the whole reason T_STR is otherwise refused here. */
        bool is_ipv4(const Descriptor& d) {
            return d.type == T_STR &&
                   (d.pay.text.kind == STR_IPV4 || d.pay.text.kind == STR_IPV4_OPT);
        }

        /** @brief The 32-bit value the register pair at offset @p i of a write
         *  payload carries. High word first, in the ONE place that says so. */
        uint32_t word_at(const uint8_t* data, uint16_t i) {
            return ((uint32_t)rd16(data + (size_t)i * 2) << 16)
                   | rd16(data + (size_t)i * 2 + 2);
        }

    }  // namespace

    bool Map::_writable(const Descriptor& d) {
        // Not the same question as whether a VALUE is acceptable - that one is
        // the bank's, and Bank::check() answers it.
        return var_writable(d) && (d.type != T_STR || is_ipv4(d));
    }

    uint32_t Map::ipv4_to_wire(const char* text) {
        // Big-endian, the way an address is written and the way a register pair
        // is read: 192.168.1.10 is 0xC0A8010A. An empty optional row is 0,
        // which is what "unset" means for an address.
        uint32_t out = 0;
        if (text == nullptr) return 0;
        uint16_t octet = 0, seen = 0;
        for (const char* p = text; ; p++) {
            if (*p >= '0' && *p <= '9') {
                octet = (uint16_t)(octet * 10 + (*p - '0'));
                if (octet > 255) return 0;
            } else if (*p == '.' || *p == '\0') {
                out = (out << 8) | (uint8_t)octet;
                octet = 0;
                if (++seen == 4) return (*p == '\0') ? out : 0u;
                if (*p == '\0') return 0;      // fewer than four
            } else {
                return 0;
            }
        }
    }

    void Map::ipv4_from_wire(uint32_t v, char* out, size_t cap) {
        if (out == nullptr || cap == 0) return;
        (void)snprintf(out, cap, "%u.%u.%u.%u",
                       (unsigned)((v >> 24) & 0xFF), (unsigned)((v >> 16) & 0xFF),
                       (unsigned)((v >> 8) & 0xFF), (unsigned)(v & 0xFF));
    }

    bool Map::published(const Descriptor& d) {
        // A text row is not served at all. It carries a register like any other
        // parameter - reserved, so the numbering stays stable if strings are
        // ever packed onto the wire - but a 16-bit register cannot hold a
        // hostname, and MAPPING it would answer 0 for ever while refusing every
        // write. An integrator reading the point list would take that zero for
        // a value; EX_ILLEGAL_ADDRESS says what is true. Set these over the
        // console or the shell.
        // A dotted quad is four bytes, and four bytes is what a row already
        // has. A hostname is not, and MAPPING one would answer 0 for ever while
        // refusing every write - which an integrator reading the point list
        // would take for a value.
        if (d.type == T_STR && !is_ipv4(d)) return false;
        // WHICH KINDS of row carry a register is a rule about the table, so it
        // lives in vars/access.h. Such a row with no register is a table defect
        // the site test fails on, and this simply does not serve it.
        return var_mapped(d) && d.bind.reg != 0;
    }

    void Map::build() {
        // Whatever is queued was ACKNOWLEDGED to a master with EX_NONE, so it is
        // drained rather than discarded: the queue holds bank row indices, which
        // a rebuild does not renumber. Unbounded, uniquely: this runs at
        // bring-up and on a config commit, never in an ordinary beat.
        (void)drain_persists(Config::MODBUS_PERSIST_QUEUE);

        _count = 0; _overflow = 0; _reg_lo = 0; _reg_hi = 0;
        if (_io == nullptr) return;

        for (uint16_t i = 0; i < _io->count(); i++) {
            const Descriptor& d = *_io->var(i).meta();
            if (!published(d)) continue;
            if (_count >= Config::MODBUS_TCP_MAX_ROWS) { _overflow++; continue; }
            // Insertion sort by register: a couple of hundred rows, once at
            // bring-up. What matters is that the result is ordered, since every
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
        // The same search as _lower_bound, asked a stricter question. _reg_of()
        // reads 0xFFFF past the end, so the bound test costs nothing extra and
        // there is no separate end check to get wrong.
        const uint16_t k = _lower_bound(reg);
        return (_reg_of(k) == reg) ? (int)k : -1;
    }

    uint32_t Map::to_wire(const Var& v) const {
        const Descriptor* d = v.meta();
        if (d == nullptr) return 0u;
        if (is_ipv4(*d))
            return (_io != nullptr) ? ipv4_to_wire(_io->text(v)) : 0u;
        return wire_of(d->type, v.value().v);
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
            wr16(out + (size_t)i * 2, word);
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

        // Two passes: validate every row, commit only once all of them pass. A
        // write failing halfway would leave the plant on a mixture of the old
        // settings and the new, with an exception saying only that something
        // went wrong.
        uint16_t changing = 0;
        for (uint16_t i = 0; i < count; i += 2) {
            bool found = false;
            const Var v = row_at((uint16_t)((addr + i) / 2), &found);
            // Two different refusals, and the difference matters to a master: an
            // address it may not write is a map question, a value out of band is
            // a value question.
            if (!found || !_writable(*v.meta())) return EX_ILLEGAL_ADDRESS;
            const uint32_t w = word_at(data, i);
            if (is_ipv4(*v.meta())) {
                // Assembled, then validated by the same rule the console uses:
                // param_check() knows what a dotted quad is, and four octets
                // out of a register pair is only how it arrived.
                char text[16];
                ipv4_from_wire(w, text, sizeof text);
                if (_io->param_check(v, text) != SET_OK) return EX_ILLEGAL_VALUE;
                if (w != to_wire(v)) changing++;
                continue;
            }
            if (_io->param_check(v, from_wire(v.type(), w)) != SET_OK)
                return EX_ILLEGAL_VALUE;
            if (w != wire_of(v.type(), v.live().v)) changing++;
        }

        // Every changed row owes a flash write, and those are paced across
        // beats. If they will not all fit, refuse the request WHOLE with
        // EX_SLAVE_BUSY rather than leave the plant on half the new settings.
        if (changing > persist_room()) return EX_SLAVE_BUSY;

        for (uint16_t i = 0; i < count; i += 2) {
            const int k = _find((uint16_t)((addr + i) / 2));
            if (k < 0) continue;                    // ruled out by the pass above
            Var v = _io->var(_rows[k]);
            const uint32_t w = word_at(data, i);
            if (is_ipv4(*v.meta())) {
                if (w == to_wire(v)) continue;                  // no commit, no wear
                char text[16];
                ipv4_from_wire(w, text, sizeof text);
                (void)_io->param_set(v, text);
                (void)_queue_persist(_rows[k]);
                if (committed != nullptr) (*committed)++;
                continue;
            }
            // Against live(), not raw(): raw() is an override's forced value,
            // and a master writing that same number would skip the commit while
            // the parameter underneath stayed where it was.
            if (w == wire_of(v.type(), v.live().v)) continue;   // no commit, no wear
            // Live immediately, with the durable copy queued for
            // drain_persists(). Cannot fail on validity: the pass above checked
            // every row.
            (void)_io->param_set_scalar(v, from_wire(v.type(), w));
            (void)_queue_persist(_rows[k]);
            if (committed != nullptr) (*committed)++;
        }
        return EX_NONE;
    }

    bool Map::_queue_persist(uint16_t r) {
        if (_pending_count >= Config::MODBUS_PERSIST_QUEUE) return false;
        const uint16_t at = (uint16_t)((_pending_head + _pending_count) %
                                       Config::MODBUS_PERSIST_QUEUE);
        _pending[at] = r;
        _pending_count++;
        return true;
    }

    uint16_t Map::drain_persists(uint16_t n) {
        if (_io == nullptr) return 0;
        uint16_t done = 0;
        while (done < n && _pending_count > 0) {
            const uint16_t r = _pending[_pending_head];
            _pending_head = (uint16_t)((_pending_head + 1) %
                                       Config::MODBUS_PERSIST_QUEUE);
            _pending_count--;
            // A refusal here is the STORE saying no, which is not a rejected
            // edit: the value is already live, and the fault raised its own
            // alarm on the way out.
            (void)_io->param_save(_io->var(r));
            done++;
        }
        return done;
    }

    Exception Map::write_register(uint16_t addr, uint16_t value,
                                  uint16_t* committed) {
        if (committed != nullptr) *committed = 0;
        if (_io == nullptr || _count == 0) return EX_ILLEGAL_ADDRESS;

        bool found = false;
        const Var v = row_at((uint16_t)(addr / 2), &found);
        if (!found || !_writable(*v.meta())) return EX_ILLEGAL_ADDRESS;
        // An ADDRESS is written whole or not at all. Splicing half of one gives
        // a valid dotted quad that nobody asked for - 192.168.1.10 with its top
        // half replaced is a different machine on a different subnet, and it
        // would be committed and persisted before the second half arrived.
        if (is_ipv4(*v.meta())) return EX_ILLEGAL_ADDRESS;

        // Splice into what the row already holds, so the half the master did not
        // address survives. Against live(), not raw(): raw() would be an
        // override's forced value, and the parameter underneath is being edited.
        const uint32_t cur = wire_of(v.type(), v.live().v);
        const uint32_t w = ((addr % 2) == 0)
                               ? (uint32_t)(((uint32_t)value << 16) | (cur & 0xFFFFu))
                               : (uint32_t)((cur & 0xFFFF0000u) | (uint32_t)value);

        uint8_t data[4];
        wr16(data,     (uint16_t)(w >> 16));
        wr16(data + 2, (uint16_t)(w & 0xFFFFu));
        // Back through the pair path, so the band check, the no-op skip, the
        // persist queue and its ceiling are all the same ones.
        return write_registers((uint16_t)(addr & (uint16_t)~1u), 2, data, committed);
    }

    Exception Map::write_bit(uint16_t addr, bool on, uint16_t* committed) {
        if (committed != nullptr) *committed = 0;
        if (_io == nullptr || _count == 0) return EX_ILLEGAL_ADDRESS;

        bool found = false;
        Var v = row_at(addr, &found);
        const Descriptor& d = *v.meta();
        // A coil is a BOOLEAN parameter. An alarm publishes as a coil too, and is
        // read-only: acknowledging one is a different block entirely.
        if (!found || !_writable(d) || d.type != T_BOOL) return EX_ILLEGAL_ADDRESS;
        if (as_bool(d.type, v.live().v) == on) return EX_NONE;   // no-op, as above
        if (persist_room() == 0) return EX_SLAVE_BUSY;
        if (_io->param_set_scalar(v, Scalar::B(on)) == SET_INVALID) return EX_ILLEGAL_VALUE;
        const int k = _find(addr);
        if (k >= 0) (void)_queue_persist(_rows[k]);
        if (committed != nullptr) (*committed)++;
        return EX_NONE;
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER
