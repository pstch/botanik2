#ifndef MODBUS_RTU_IMAGE_H
#define MODBUS_RTU_IMAGE_H

/**
 * @file modbus/rtu/image.h
 * @brief Where the bus meets the bank: a response becomes readings, and demands
 *        become a frame.
 *
 * The scan never touches the wire. A sweep of this plant is hundreds of
 * milliseconds - tens of beats - so the textbook cycle-boundary I/O is out of
 * reach: the bus runs on its own clock and maintains a PROCESS IMAGE the cycle
 * reads. Vars::Bank already is that image and XP_MODBUS already marks the rows
 * the driver owns, so there is nothing here to build, only two directions to
 * move values in.
 *
 * Free functions rather than a class: there is no state to keep. What has been
 * read lives in the bank, and what is owed to the field is a bit on the row
 * that owes it.
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

    /**
     * @brief Owe every row of @p b to the field again, without changing what
     *        any of them demands.
     *
     * For a device that went away and came back: the dirty mark means "the wire
     * has not carried this yet", and it was cleared when the write landed, on a
     * device that has since been through its own power-on defaults. A level
     * driven scan never disagrees on its own, because the demand has not moved.
     * Re-marking is not a re-decision: the value stands, only its delivery is
     * owed again.
     */
    void reassert_block(Vars::Bank& io, const Planner& p, const Block& b);

    /**
     * @brief Invalidate every row of @p b with @p q, because the read failed.
     *
     * Left alone a row keeps Q_GOOD and its last value until expire_stale()
     * catches up - and a row declared with timeout_ms == 0 is skipped by
     * expire_stale entirely, so it would read good, with a value from before the
     * device died, for ever.
     *
     * @p q is the diagnosis: Q_REFUSED when the device answered and declined
     * (its register map is not the one we hold), Q_STALE when nothing came back.
     */
    void invalidate_block(Vars::Bank& io, const Planner& p, const Block& b,
                          Vars::Quality q);

    /**
     * @brief Publish @p r's payload into the rows of @p b.
     *
     * Each row knows where it sits: its register minus the block's is its offset
     * into the payload. Digital rows fold in F_INVERT here, the one place
     * active-low wiring is dealt with, so everything above reads true = asserted.
     *
     * Every row is CHECKED before it is published, not after: a shorted 4-20 mA
     * loop reads full scale, which is a perfectly well-formed number. A
     * calibrated row (`SCOPE_AI`) is judged against the band in the PARAMETER
     * table, a direct row by `Vars::value_implausible()` against the band on the
     * descriptor. Both at commit, since `Bank::reject_implausible()` runs at
     * background rate and is only the backstop for rows off this bus.
     *
     * @return how many rows were published as good.
     */
    uint16_t commit_read(Vars::Bank& io, const Planner& p, const Block& b,
                         const Response& r, uint64_t now_ms);

    /** @} */

    /** @name Outbound: demands become a frame
     *  @{ */

    /** @brief Whether any row of @p b still owes its value to the field. */
    bool block_dirty(const Vars::Bank& io, const Planner& p, const Block& b);

    /**
     * @brief Lay @p b's current demands into @p out, ready to be framed.
     *
     * Registers big-endian, coils packed LSB-first, both in the layout the
     * matching write function expects. Registers the block spans but no row
     * claims are written as zero: they were read across for cost reasons, and
     * writing them is the price of the same merge.
     *
     * Reads through Var::raw(), NOT the underlying value, so an operator's
     * override on an actuator reaches the wire, which is what `control disable`
     * is for.
     *
     * @return bytes written, or 0 if @p cap is too small.
     */
    uint16_t collect_write(const Vars::Bank& io, const Planner& p, const Block& b,
                           uint8_t* out, uint16_t cap);

    /**
     * @brief Clear the dirty marks on the rows of @p b whose demand is still
     * what @p sent carried.
     *
     * Only after the slave has confirmed: clearing on send would lose a demand
     * to a frame that never arrived.
     *
     * And only PER ROW: the two ends of a transaction are several beats apart,
     * so a program that moved a demand in between has marked dirty a value this
     * frame never carried, and clearing the block wholesale would drop it until
     * the rewrite interval.
     *
     * @p sent is the payload collect_write() produced, which is still sitting in
     * the caller's frame buffer, so nothing has to be copied to compare against.
     */
    void clear_dirty(Vars::Bank& io, const Planner& p, const Block& b,
                     const uint8_t* sent, uint16_t sent_len);

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_IMAGE_H
