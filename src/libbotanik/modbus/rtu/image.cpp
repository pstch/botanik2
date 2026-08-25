/**
 * @file modbus/rtu/image.cpp
 * @brief @see image.h
 */

#include "libbotanik/modbus/rtu/image.h"

#include <cstring>

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

#include "libbotanik/modbus/rtu/codec.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/vars/params.h"

namespace Botanik { namespace Modbus {

namespace {

    /** @brief Where a row sits inside its block's payload, counted in registers
     *  or in bits according to the block's function code. */
    uint16_t offset_of(const Vars::Descriptor& d, const Block& b) {
        return (uint16_t)(d.bind.reg - b.reg);
    }

    /** @brief Whether a row's wire value spans a register pair. */
    bool is_pair(const Vars::Descriptor& d) { return d.bind.regs == 2; }

    RegOrder order_of(const Vars::Descriptor& d) {
        return (d.flags & Vars::F_WORD_SWAP) ? REG_LO_FIRST : REG_HI_FIRST;
    }

    /** @brief A row's raw wire value as a float, whatever width it arrived in.
     *
     *  Everything analog becomes a float here because that is what the
     *  calibration works in; a direct row keeps its own type and goes through
     *  the integer path instead. */
    float wire_f32(const Vars::Descriptor& d, const uint8_t* payload, uint16_t off) {
        const uint8_t* at = payload + (size_t)off * 2u;
        if (is_pair(d)) {
            if (d.bind.wire == Vars::T_F32) return f32_from_regs(at, order_of(d));
            const uint32_t u = u32_from_regs(at, order_of(d));
            // Through int32_t when the wire type says signed, or a meter
            // reporting -1 W of export reads as 4.29e9.
            return Vars::type_signed(d.bind.wire) ? (float)(int32_t)u : (float)u;
        }
        const uint16_t w = be16(at[0], at[1]);
        return Vars::type_signed(d.bind.wire) ? (float)(int16_t)w : (float)w;
    }

    /** @brief One digital row's demand, as it goes on the wire.
     *
     *  raw(), not the live value: an override on an actuator has to reach the
     *  wire, or `control disable` leaves nothing able to move one. */
    bool row_bit(const Vars::Descriptor& d, Vars::Var h) {
        const bool on = h.as_bool();
        return (d.flags & Vars::F_INVERT) ? !on : on;
    }

    /** @brief A row's demand as the wire wants it. The model does the scaling:
     *  wire() on an output is what WOULD be sent. */
    float wire_value(const Vars::Bank& io, Vars::VarRef r) {
        return io.wire(r).as_f32();
    }

    /** @brief One register row's demand, big-endian into @p at.
     *
     *  An AO's integer comes from its calibrated float, the only place the
     *  calibration exists; every other row's comes from the row itself, since
     *  routing a large u32 or i32 through a float would round it. */
    void row_regs(const Vars::Bank& io, Vars::VarRef r,
                  const Vars::Descriptor& d, Vars::Var h, uint8_t* at) {
        // A float on the wire goes out as one, calibrated or not.
        if (is_pair(d) && d.bind.wire == Vars::T_F32) {
            f32_to_regs(wire_value(io, r), order_of(d), at);
            return;
        }
        // Through int64 rather than straight to unsigned: a negative float
        // converted to uint32_t is undefined, and a calibration that has been
        // mis-trimmed can produce one.
        const int64_t v = (d.scope == Vars::SCOPE_AO)
                              ? (int64_t)wire_value(io, r)
                              : h.as_i64();
        if (is_pair(d)) u32_to_regs((uint32_t)v, order_of(d), at);
        else            wr16(at, (uint16_t)v);
    }

    /** @brief Commit one digital row out of a packed-bit payload. */
    void commit_bit(Vars::Var h, const Vars::Descriptor& d, const Block& b,
                    const uint8_t* payload, uint64_t now_ms) {
        bool on = bit_at(payload, offset_of(d, b));
        // The one place active-low wiring is folded in, so everything above
        // reads true = asserted.
        if (d.flags & Vars::F_INVERT) on = !on;
        h.publish(Vars::from_bool(d.type, on), now_ms);
    }

    /** @brief Hand one analog row's WIRE COUNT to the model, which scales it
     *  and judges the band - both live in the calibration's parameters. */
    bool commit_analog(Vars::Bank& io, Vars::VarRef r, const Vars::Descriptor& d,
                       const Block& b, const uint8_t* payload, uint64_t now_ms) {
        if (Vars::analog_cal(d) == nullptr) return false;
        const float raw = wire_f32(d, payload, offset_of(d, b));
        io.publish_wire(r, Vars::from_f32(d.bind.wire, raw), now_ms);
        return io.var(r).valid();
    }

    /** @brief The register, mapped 1:1 onto the row's type. */
    Vars::Scalar decode_direct(const Vars::Descriptor& d, const Block& b,
                               const uint8_t* payload) {
        const uint16_t off = offset_of(d, b);
        const uint8_t* at  = payload + (size_t)off * 2u;
        if (d.type == Vars::T_F32)
            return Vars::from_f32(d.type, wire_f32(d, payload, off));
        if (is_pair(d)) {
            const uint32_t u = u32_from_regs(at, order_of(d));
            const int64_t  p = Vars::type_signed(d.bind.wire) ? (int64_t)(int32_t)u
                                                              : (int64_t)u;
            return Vars::from_i64(d.type, p);
        }
        const uint16_t w = be16(at[0], at[1]);
        const int64_t  s = Vars::type_signed(d.bind.wire) ? (int64_t)(int16_t)w
                                                          : (int64_t)w;
        return Vars::from_i64(d.type, s);
    }

    /** @brief Commit one direct row: decode, then judge - the same order a
     *  calibrated row goes through, and for the same reason.
     *  @see commit_read() in image.h */
    bool commit_direct(Vars::Var h, const Vars::Descriptor& d, const Block& b,
                       const uint8_t* payload, uint64_t now_ms) {
        const Vars::Scalar v = decode_direct(d, b, payload);
        if (Vars::value_implausible(d, v)) {
            h.invalidate(Vars::Q_IMPLAUSIBLE);
            return false;
        }
        h.publish(v, now_ms);
        return true;
    }

}  // namespace

void reassert_block(Vars::Bank& io, const Planner& p, const Block& b) {
    for (uint16_t k = 0; k < b.row_count; ++k)
        io.var(p.row((uint16_t)(b.first_row + k))).mark_field_dirty();
}

void invalidate_block(Vars::Bank& io, const Planner& p, const Block& b,
                      Vars::Quality q) {
    for (uint16_t k = 0; k < b.row_count; ++k)
        io.var(p.row((uint16_t)(b.first_row + k))).invalidate(q);
}

uint16_t commit_read(Vars::Bank& io, const Planner& p, const Block& b,
                     const Response& r, uint64_t now_ms) {
    if (r.data == nullptr) return 0;

    uint16_t good = 0;
    for (uint16_t k = 0; k < b.row_count; ++k) {
        const uint16_t idx = p.row((uint16_t)(b.first_row + k));
        Vars::Var h = io.var(idx);
        const Vars::Descriptor& d = *h.meta();
        const uint16_t off = offset_of(d, b);

        // A row outside the payload the slave actually sent. Cannot happen for a
        // plan and a response that agree, which the framer already checked - but
        // it is the one arithmetic that would read past the buffer, so it is
        // guarded rather than assumed.
        if (is_bit(b.fc)) {
            if (off >= (uint16_t)(r.len * 8u)) { h.invalidate(Vars::Q_NO_DATA); continue; }
            commit_bit(h, d, b, r.data, now_ms);
            good++;
            continue;
        }
        const uint16_t span = is_pair(d) ? 2u : 1u;
        if ((uint32_t)(off + span) * 2u > r.len) {
            h.invalidate(Vars::Q_NO_DATA);
            continue;
        }

        if (d.scope == Vars::SCOPE_AI) {
            if (commit_analog(io, Vars::VarRef{idx}, d, b, r.data, now_ms)) good++;
        } else {
            if (commit_direct(h, d, b, r.data, now_ms)) good++;
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
        const Vars::Descriptor& d = *h.meta();
        const uint16_t off = offset_of(d, b);

        if (is_bit(b.fc)) {
            if (row_bit(d, h)) put_bit(out, off, true);
            continue;
        }
        row_regs(io, Vars::VarRef{idx}, d, h, out + (size_t)off * 2u);
    }
    return need;
}

void clear_dirty(Vars::Bank& io, const Planner& p, const Block& b,
                 const uint8_t* sent, uint16_t sent_len) {
    for (uint16_t k = 0; k < b.row_count; ++k) {
        const uint16_t idx = p.row((uint16_t)(b.first_row + k));
        Vars::Var h = io.var(idx);
        const Vars::Descriptor& d = *h.meta();
        const uint16_t off = offset_of(d, b);

        // No payload to compare against: the caller could not say what went out,
        // so clear wholesale rather than never clearing at all.
        if (sent == nullptr) { h.clear_field_dirty(); continue; }

        bool carried = false;
        if (is_bit(b.fc)) {
            if (off / 8u < sent_len)
                carried = bit_at(sent, off) == row_bit(d, h);
        } else {
            const uint16_t at = (uint16_t)(off * 2u);
            const uint16_t w  = is_pair(d) ? 4u : 2u;
            if ((uint32_t)at + w <= sent_len) {
                uint8_t now[4];
                row_regs(io, Vars::VarRef{idx}, d, h, now);
                carried = memcmp(now, sent + at, w) == 0;
            }
        }
        // Still dirty if the demand moved after the frame was built: that value
        // has not been on the wire, and the next transaction owes it.
        if (carried) h.clear_field_dirty();
    }
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
