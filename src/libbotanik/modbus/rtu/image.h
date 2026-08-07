#ifndef MODBUS_RTU_IMAGE_H
#define MODBUS_RTU_IMAGE_H

/**
 * @file modbus/rtu/image.h
 * @brief Where the bus meets the bank: a response becomes readings, and demands
 *        become a frame.
 *
 * @section img_image The scan never touches the wire
 *
 * The textbook PLC cycle reads its inputs, executes, and writes its outputs,
 * once per cycle, with I/O synchronous at the cycle boundary. That works when
 * the I/O is backplane-local and costs microseconds. A sweep of this plant is
 * hundreds of milliseconds - tens of beats - so it cannot be done at the top of
 * one.
 *
 * So the bus runs on its own clock and maintains a PROCESS IMAGE, and the cycle
 * reads the image. Vars::Bank already is that image, and XP_MODBUS already marks
 * the rows the driver owns, so there is nothing here to build: only two
 * directions to move values in.
 *
 * Free functions rather than a class, because there is no state to keep. What
 * has been read lives in the bank, and what is owed to the field is a bit on the
 * row that owes it.
 */

#include <cstdint>

#include "libbotanik/modbus/rtu/plan.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Vars { class Bank; } }

namespace Botanik { namespace Modbus {

    /** @name Inbound: a response becomes readings
     *  @{ */

    /**
     * @brief Publish @p r's payload into the rows of @p b.
     *
     * Each row knows where it sits: its register minus the block's is its offset
     * into the payload. Digital rows fold in F_INVERT here, which is the one
     * place active-low wiring is dealt with, so everything above reads
     * true = asserted.
     *
     * Analog rows are converted through their calibration and then CHECKED
     * against their band, because Bank::reject_implausible() deliberately skips
     * them - their band lives in the parameter table, which the bank has no route
     * to. A shorted 4-20 mA loop reads full scale, which is a perfectly
     * well-formed number, and this is what stops it reaching a control program as
     * a measurement.
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
     * override on an actuator reaches the wire. Driving one by hand with the
     * programs stopped is the entire point of `control disable`.
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
     * And only PER ROW, because the two ends of a transaction are several beats
     * apart. collect_write() runs when the frame is built and this runs when the
     * echo comes back; a program that moves a demand in between has set the
     * dirty mark on a value this frame never carried, and clearing the block
     * wholesale would drop it until the rewrite interval - five seconds of a
     * stale command on an actuator.
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
