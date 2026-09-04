#ifndef MODBUS_RTU_IMAGE_H
#define MODBUS_RTU_IMAGE_H

/**
 * @file modbus/rtu/image.h
 * @brief Where the bus meets the bank: a response becomes readings, and demands
 *        become a frame.
 * @note Free functions: there is no state to keep. What has been read lives in
 *       the bank, and what is owed to the field is a bit on the row that owes
 *       it. See docs/dev/modbus.md.
 */

#include <cstdint>

#include "libbotanik/modbus/rtu/plan.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Vars {
    class Bank;
    // Opaque enum declaration (C++11): the underlying type is fixed, so a
    // diagnosis can cross this interface without model.h coming with it.
    enum Quality : uint8_t;
}}

namespace Botanik { namespace Modbus {

    /** @name Inbound: a response becomes readings
     *  @{ */

    /** @brief Owe every row of @p b to the field again, without changing what any
     *  of them demands. For a device that went away and came back.
     *  @note Not a re-decision: the value stands, only its delivery is owed. */
    void reassert_block(Vars::Bank& io, const Planner& p, const Block& b);

    /** @brief Drop every row of @p b to @p q, because the read failed. Left
     *  alone a row keeps its last value, believed, and one declared with
     *  timeout_ms == 0 is skipped by expire_stale() entirely.
     *  @param q Q_NONE either way: a refusal and a silence leave a reader with
     *         the same nothing, and the RTU master's log line is where they
     *         still differ. */
    void invalidate_block(Vars::Bank& io, const Planner& p, const Block& b,
                          Vars::Quality q);

    /** @brief Publish @p r's payload into the rows of @p b, each row's register
     *  minus the block's being its offset. Digital rows fold in F_INVERT here,
     *  the one place active-low wiring is dealt with.
     *  @note Every row is checked BEFORE publishing: a shorted 4-20 mA loop reads
     *  full scale, which is a perfectly well-formed number.
     *  @return how many rows were published as good. */
    uint16_t commit_read(Vars::Bank& io, const Planner& p, const Block& b,
                         const Response& r, uint64_t now_ms);

    /** @} */

    /** @name Outbound: demands become a frame
     *  @{ */

    /** @brief Whether any row of @p b still owes its value to the field. */
    bool block_dirty(const Vars::Bank& io, const Planner& p, const Block& b);

    /** @brief Lay @p b's current demands into @p out, ready to be framed:
     *  registers big-endian, coils packed LSB-first. Registers the block spans
     *  but no row claims are written as zero.
     *  @note Reads through Var::raw(), so an operator's override on an actuator
     *  reaches the wire.
     *  @return bytes written, or 0 if @p cap is too small. */
    uint16_t collect_write(const Vars::Bank& io, const Planner& p, const Block& b,
                           uint8_t* out, uint16_t cap);

    /** @brief Clear the dirty marks on the rows of @p b whose demand is still what
     *  @p sent carried. Only after the slave confirms, and only PER ROW: the two
     *  ends of a transaction are several beats apart, so a program that moved a
     *  demand in between would lose it until the rewrite interval.
     *  @param sent the payload collect_write() produced, still in the caller's
     *         frame buffer, so nothing has to be copied to compare against. */
    void clear_dirty(Vars::Bank& io, const Planner& p, const Block& b,
                     const uint8_t* sent, uint16_t sent_len);

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_IMAGE_H
