/**
 * @file image.cpp
 * @brief @see image.h
 */

#include "image.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

#include "codec.h"
#include "../vars/model.h"
#include "../vars/params.h"    // Vars::analog_values - an AI row's live calibration

namespace Botanik { namespace Modbus {

namespace {

    /** @brief Where a row sits inside its block's payload, counted in registers
     *  or in bits according to the block's function code. */
    uint16_t offset_of(const Vars::Variable& d, const Block& b) {
        return (uint16_t)(d.bind.reg - b.reg);
    }

    /** @brief Whether a row's wire value spans a register pair. */
    bool is_pair(const Vars::Variable& d) { return d.bind.regs == 2; }

    RegOrder order_of(const Vars::Variable& d) {
        return (d.flags & Vars::F_WORD_SWAP) ? REG_LO_FIRST : REG_HI_FIRST;
    }

    /** @brief A row's raw wire value as a float, whatever width it arrived in.
     *
     *  Everything analog becomes a float here because that is what the
     *  calibration works in; a direct row keeps its own type and goes through
     *  the integer path instead. */
    float wire_f32(const Vars::Variable& d, const uint8_t* payload, uint16_t off) {
        const uint8_t* at = payload + (size_t)off * 2u;
        if (is_pair(d)) {
            if (d.bind.wire == Vars::T_F32) return f32_from_regs(at, order_of(d));
            return (float)u32_from_regs(at, order_of(d));
        }
        const uint16_t w = be16(at[0], at[1]);
        return (d.bind.wire == Vars::T_I16) ? (float)(int16_t)w : (float)w;
    }

    /** @brief Commit one digital row out of a packed-bit payload. */
    void commit_bit(Vars::Var h, const Vars::Variable& d, const Block& b,
                    const uint8_t* payload, uint64_t now_ms) {
        bool on = bit_at(payload, offset_of(d, b));
        // The one place active-low wiring is folded in, so everything above
        // reads true = asserted.
        if (d.flags & Vars::F_INVERT) on = !on;
        h.publish(Vars::from_bool(d.type, on), now_ms);
    }

    /** @brief Commit one analog row: convert, then judge. */
    bool commit_analog(Vars::Bank& io, Vars::Var h, const Vars::Variable& d,
                       const Block& b, const uint8_t* payload, uint64_t now_ms) {
        const Vars::AnalogCal* cal = Vars::analog_cal(d);
        if (cal == nullptr) return false;
        const Vars::AnalogValues v = Vars::analog_values(&io, cal);
        const float eng = v.to_eng(wire_f32(d, payload, offset_of(d, b)));
        if (!v.plausible(eng)) {
            // Well-formed and not believable. Publishing it would hand a control
            // program a rail voltage as a measurement; saying WHY is what lets
            // an operator tell a dead sensor from a quiet one.
            h.invalidate(Vars::Q_IMPLAUSIBLE);
            return false;
        }
        h.publish(Vars::from_f32(d.type, eng), now_ms);
        return true;
    }

    /** @brief Commit one direct row: the register, mapped 1:1 onto its type. */
    void commit_direct(Vars::Var h, const Vars::Variable& d, const Block& b,
                       const uint8_t* payload, uint64_t now_ms) {
        const uint16_t off = offset_of(d, b);
        const uint8_t* at  = payload + (size_t)off * 2u;
        if (d.type == Vars::T_F32) {
            h.publish(Vars::from_f32(d.type, wire_f32(d, payload, off)), now_ms);
            return;
        }
        if (is_pair(d)) {
            h.publish(Vars::from_i64(d.type, (int64_t)u32_from_regs(at, order_of(d))),
                      now_ms);
            return;
        }
        const uint16_t w = be16(at[0], at[1]);
        const int64_t  s = (d.bind.wire == Vars::T_I16) ? (int64_t)(int16_t)w
                                                        : (int64_t)w;
        h.publish(Vars::from_i64(d.type, s), now_ms);
    }

}  // namespace

uint16_t commit_read(Vars::Bank& io, const Planner& p, const Block& b,
                     const Response& r, uint64_t now_ms) {
    if (r.data == nullptr) return 0;

    uint16_t good = 0;
    for (uint16_t k = 0; k < b.row_count; ++k) {
        const uint16_t idx = p.row((uint16_t)(b.first_row + k));
        Vars::Var h = io.var(idx);
        const Vars::Variable& d = *h.meta();
        const uint16_t off = offset_of(d, b);

        // A row outside the payload the slave actually sent. Cannot happen for a
        // plan and a response that agree, which the framer already checked - but
        // it is the one arithmetic that would read past the buffer, so it is
        // guarded rather than assumed.
        if (is_bit(b.fc)) {
            if (off >= (uint16_t)(r.len * 8u)) continue;
            commit_bit(h, d, b, r.data, now_ms);
            good++;
            continue;
        }
        const uint16_t span = is_pair(d) ? 2u : 1u;
        if ((uint32_t)(off + span) * 2u > r.len) continue;

        if (d.scope == Vars::SCOPE_AI) {
            if (commit_analog(io, h, d, b, r.data, now_ms)) good++;
        } else {
            commit_direct(h, d, b, r.data, now_ms);
            good++;
        }
    }
    return good;
}

bool block_dirty(const Vars::Bank& io, const Planner& p, const Block& b) {
    for (uint16_t k = 0; k < b.row_count; ++k) {
        if (io.var(p.row((uint16_t)(b.first_row + k))).field_dirty()) return true;
    }
    return false;
}

uint16_t collect_write(const Vars::Bank& io, const Planner& p, const Block& b,
                       uint8_t* out, uint16_t cap) {
    if (out == nullptr) return 0;
    const uint16_t need = is_bit(b.fc) ? bit_bytes(b.regs) : (uint16_t)(b.regs * 2u);
    if (cap < need) return 0;

    // Registers the block spans but no row claims read as zero. They were merged
    // across because reading them was cheaper than a second transaction, and
    // writing them is the same bargain's other half.
    for (uint16_t i = 0; i < need; ++i) out[i] = 0;

    for (uint16_t k = 0; k < b.row_count; ++k) {
        const uint16_t idx = p.row((uint16_t)(b.first_row + k));
        Vars::Var h = io.var(idx);
        const Vars::Variable& d = *h.meta();
        const uint16_t off = offset_of(d, b);

        if (is_bit(b.fc)) {
            // raw(), not the live value: an override on an actuator has to reach
            // the wire, or `control disable` leaves nothing able to move one.
            bool on = h.b();
            if (d.flags & Vars::F_INVERT) on = !on;
            if (on) out[off / 8u] = (uint8_t)(out[off / 8u] | (1u << (off % 8u)));
            continue;
        }

        uint8_t* at = out + (size_t)off * 2u;
        if (d.scope == Vars::SCOPE_AO) {
            const Vars::AnalogCal* cal = Vars::analog_cal(d);
            const float raw = (cal != nullptr)
                                  ? Vars::analog_values(&io, cal).to_raw(h.value())
                                  : h.value();
            if (is_pair(d) && d.bind.wire == Vars::T_F32) {
                f32_to_regs(raw, order_of(d), at);
            } else if (is_pair(d)) {
                u32_to_regs((uint32_t)raw, order_of(d), at);
            } else {
                const uint16_t w = (uint16_t)(int32_t)raw;
                at[0] = hi8(w); at[1] = lo8(w);
            }
            continue;
        }

        if (is_pair(d) && d.bind.wire == Vars::T_F32) {
            f32_to_regs(h.value(), order_of(d), at);
        } else if (is_pair(d)) {
            u32_to_regs((uint32_t)h.i(), order_of(d), at);
        } else {
            const uint16_t w = (uint16_t)h.i();
            at[0] = hi8(w); at[1] = lo8(w);
        }
    }
    return need;
}

void clear_dirty(Vars::Bank& io, const Planner& p, const Block& b) {
    for (uint16_t k = 0; k < b.row_count; ++k) {
        io.var(p.row((uint16_t)(b.first_row + k))).clear_field_dirty();
    }
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
