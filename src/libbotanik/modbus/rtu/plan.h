#ifndef MODBUS_RTU_PLAN_H
#define MODBUS_RTU_PLAN_H

/**
 * @file modbus/rtu/plan.h
 * @brief What the bus costs, and the blocks the variable table turns into.
 *
 * Transactions are the scarce resource and registers are nearly free: at 19200
 * with a 10 ms turnaround a transaction costs about 19.4 ms before it carries
 * anything, and each register adds about 1.1 ms. So read in bulk, and merge
 * across a gap up to the point where the skipped registers cost a transaction's
 * worth of bytes (merge_gap()). The static_asserts at the bottom pin this
 * arithmetic to the figures published in @ref dev_modbus.
 *
 * A poll period comes from the row's own staleness timeout: a group polled
 * slower than a third of its @c timeout_ms flaps between valid and stale
 * forever, so the period is @c timeout_ms/3 floored onto a short fixed ladder.
 * The ladder is what bounds the plan at devices x function codes x rungs.
 *
 * Everything here is integer, which keeps it `constexpr` and keeps the M4's
 * missing double FPU out of the discussion.
 */

#include <cstdint>

#include "libbotanik/config/modbus.h"
#include "libbotanik/config/scheduler.h"
#include "libbotanik/modbus/rtu/device.h"
#include "libbotanik/modbus/wire.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Vars { class Bank; } }

namespace Botanik { namespace Modbus {

    /** @name Bus cost
     *
     *  All in microseconds, all integer, all single expressions. 11 bits per
     *  character throughout: start + 8 data + parity + stop, and a no-parity
     *  link spends the same eleventh bit on a second stop bit.
     *  @{ */

    /** @brief One character's time on the wire. */
    constexpr uint32_t tchar_us(uint32_t baud) {
        return baud ? (uint32_t)(11000000u / baud) : 0;
    }

    /** @brief The inter-frame silence that delimits a frame.
     *
     *  3.5 characters, except that the spec pins it at 1.75 ms above 19200
     *  rather than letting it shrink with the baud rate. Note that this firmware
     *  never MEASURES it (see framing.h); it is here because it is time the bus
     *  spends, and the planner is costing the bus. */
    constexpr uint32_t t35_us(uint32_t baud) {
        return (baud > 19200u) ? 1750u : (baud ? (uint32_t)(38500000u / baud) : 0);
    }

    /** @brief What a transaction costs before it has carried any payload:
     *  the silence, the slave's turnaround, and the 13 bytes of header a read
     *  spends (8 of request, 5 of response overhead). */
    constexpr uint32_t fixed_us(uint32_t baud, uint32_t turnaround_us) {
        return t35_us(baud) + turnaround_us + 13u * tchar_us(baud);
    }

    /** @brief What one whole transaction costs: the silence, the turnaround, and
     *  every byte of both frames.
     *
     *  Sizes come from wire.h rather than being restated, so the cost model and
     *  the codec cannot disagree about how long a frame is. */
    constexpr uint32_t txn_us(uint32_t baud, uint32_t turnaround_us,
                              uint8_t fc, uint16_t qty) {
        return t35_us(baud) + turnaround_us +
               (uint32_t)(request_len(fc, qty) + response_len(fc, qty)) *
                   tchar_us(baud);
    }

    /** @brief How many unwanted registers are worth reading rather than issuing
     *  a second transaction.
     *
     *  Break-even: a skipped register costs two bytes in the response, so the
     *  gap pays for itself up to `fixed / (2 x tchar)`. 17 at 19200, 68 at
     *  115200 - which is why the faster link merges so much more aggressively. */
    constexpr uint16_t merge_gap(uint32_t baud, uint32_t turnaround_us) {
        return (uint16_t)((fixed_us(baud, turnaround_us) + tchar_us(baud)) /
                          (2u * tchar_us(baud)));
    }

    /** @}
     *  @name Poll ladder
     *  @{ */

    /** @brief The rungs a poll period is floored onto. */
    enum Ladder : uint8_t { L_200MS, L_1S, L_5S, L_10S, L_60S, L_COUNT };

    /** @brief Rung @p l as a period. */
    constexpr uint32_t ladder_ms(uint8_t l) {
        return (l == L_60S) ? 60000u
             : (l == L_10S) ? 10000u
             : (l == L_5S)  ?  5000u
             : (l == L_1S)  ?  1000u
                            :   200u;
    }

    /** @brief The period a row's declared staleness timeout implies.
     *
     *  A third of it, which is the relationship the budget doc fixes: poll any
     *  slower and the reading expires between reads, so the row spends its life
     *  flapping between valid and stale. */
    constexpr uint32_t period_of(uint32_t timeout_ms) { return timeout_ms / 3u; }

    /** @brief The rung at or below @p period_ms.
     *
     *  Floored, never rounded: rounding up would poll slower than the timeout
     *  asked for, which is the one direction that breaks. Below the fastest rung
     *  it clamps there - 200 ms is already twenty beats, and a field bus has no
     *  business being asked for more. */
    constexpr Ladder ladder_of(uint32_t period_ms) {
        return (period_ms >= 60000u) ? L_60S
             : (period_ms >= 10000u) ? L_10S
             : (period_ms >=  5000u) ? L_5S
             : (period_ms >=  1000u) ? L_1S
                                     : L_200MS;
    }

    /** @}
     *  @name The plan
     *  @{ */

    /**
     * @brief One transaction's worth of the table: a contiguous run of
     * registers or coils, on one device, issued with one function code.
     *
     * It names its rows rather than holding them: @c first_row and @c row_count
     * are a slice of Planner::row(), which is itself a list of indices into the
     * variable table. So a block costs no copy of anything the bank already has,
     * and committing a response is a walk over that slice.
     */
    struct Block {
        /** @brief How often this block is read. 0 for a write block: those are
         *  driven by what the control programs demanded, not by a clock. */
        uint32_t period_ms;
        /** @brief The earliest this block may go out. For a read it is when the
         *  next poll falls due; for a write it is a floor and nothing more,
         *  normally in the past, raised only by the refusal penalty below. */
        uint64_t due_ms;
        /** @brief When it last went out. Both halves of the write policy come
         *  from this one stamp: a minimum interval between writes to a slave,
         *  and an unconditional rewrite every few seconds that re-drives one
         *  that rebooted and doubles as the keepalive its failsafe watches for. */
        uint64_t last_ms;
        uint16_t reg;         ///< first register or coil
        uint16_t regs;        ///< how many it spans, gaps included
        uint16_t first_row;   ///< where its rows start in Planner::row()
        uint16_t row_count;
        uint8_t  device;      ///< slave address
        uint8_t  fc;          ///< the code it is issued with
        /** @brief Which entry in the DEVICE set this block's slave is, assigned
         *  at plan time.
         *
         *  Health is per device and the client asks about it per block, so
         *  without this every block costs a linear scan of the peers on every
         *  beat. The planner already groups rows by device, so it is free here. */
        uint8_t  peer;
        /** @brief Consecutive refusals of THIS block; 0 once one is accepted.
         *
         *  Per block and not per device, because an exception says the device is
         *  alive and its register map is wrong: its other blocks must keep their
         *  rates, and only the one that cannot be delivered backs off. Writes
         *  only - a refused read already waits for its own poll period. */
        uint8_t  refusals;
    };

    /**
     * @brief Turns the variable table into blocks, once, at bring-up.
     *
     * Nothing here talks to a bus, a clock or a serial port; it reads a
     * Vars::Bank and produces an array. The plan is DERIVED, so there is no
     * second copy of the register map to keep in step and inserting a row
     * re-plans rather than renumbering anything.
     *
     * Overflow is counted, not logged (this layer has no logger). The owner
     * reports rows_dropped() and rows_unscanned() at bring-up; both are zero on
     * a sane table, and neither is allowed to be silent.
     */
    /** @} */

    class Planner {

        /** @name State
         *  @{ */

        Block    _blocks[Config::MODBUS_MAX_BLOCKS];
        /** @brief Variable-table indices of every XP_MODBUS row, sorted into
         *  the order the blocks are cut from. */
        uint16_t _rows[Config::MODBUS_MAX_ROWS];
        uint16_t _row_count      = 0;
        uint16_t _block_count    = 0;
        /** @brief Borrowed, flash-resident, may be null. @see device.h */
        const DeviceTable* _devices = nullptr;
        uint16_t _rows_dropped   = 0;   ///< more XP_MODBUS rows than _rows holds
        /** @brief Rows that will never be scanned: the plan ran out of blocks,
         *  or a block could not be made legal for its device. Counted in ROWS,
         *  which is what an operator can go and look for. */
        uint16_t _rows_unscanned = 0;
        /** @brief The distinct unit addresses the plan talks to, in the order
         *  they were first seen. Bounded by the block table, not by a separate
         *  cap: a device with no block is a device nobody speaks to. */
        uint8_t  _devices_seen[Config::MODBUS_MAX_BLOCKS];
        uint8_t  _device_count = 0;
        uint16_t _merge_gap      = 0;
        uint32_t _baud           = 0;
        uint32_t _turnaround_us  = 0;

        /** @} */

    public:
        /** @name Planning
         *  @{ */

        /** @brief Read @p io and build the plan. Discards any previous one.
         *
         *  @p devices is what each unit on the segment will actually answer -
         *  its request ceiling and its address alignment (see device.h). Null
         *  plans every unit as generic_model(), which is the protocol's own
         *  limits: correct, and no tighter than it has to be. */
        void plan(const Vars::Bank& io, uint32_t baud, uint32_t turnaround_ms,
                  const DeviceTable* devices = nullptr);

        /** @brief Spread the blocks' first activations, so a ladder full of
         *  one-second groups does not fire them all on the same beat. Same rule
         *  the task registry and the scan cycle use. */
        void seed(uint64_t now_ms);

        /** @} */

        /** @name The result
         *  @{ */

        /** @brief How many distinct devices the plan addresses; peer indices run
         *  below this. */
        uint8_t      device_count() const { return _device_count; }
        /** @brief The unit address of peer @p i. */
        uint8_t      device_at(uint8_t i) const {
            return (i < _device_count) ? _devices_seen[i] : (uint8_t)0;
        }

        uint16_t     block_count() const { return _block_count; }
        const Block& block(uint16_t i) const { return _blocks[i]; }
        /** @brief Mutable, for the owner to advance a block's due time. */
        Block&       block(uint16_t i) { return _blocks[i]; }
        /** @brief Variable-table index of slot @p i, which a block's
         *  [first_row, first_row + row_count) addresses. */
        uint16_t     row(uint16_t i) const { return _rows[i]; }
        uint16_t     row_count() const { return _row_count; }

        /** @} */

        /** @name Cost, and what did not fit
         *  @{ */

        /** @brief Share of the bus the read plan occupies, in parts per million,
         *  at the planned periods. Counts the SCHEDULING as well as the wire:
         *  the client runs one transaction at a time and advances it from the
         *  beat, so a transaction occupies the bus for at least one beat however
         *  short the frame is. Reporting the 3.6 ms of a two-register read in a
         *  10 ms beat would promise room that is not there. */
        uint32_t duty_ppm() const;

        /** @brief The same, counting only time on the wire. What the budget
         *  document's tables are computed in. */
        uint32_t wire_duty_ppm() const;

        uint16_t rows_dropped() const   { return _rows_dropped; }
        uint16_t rows_unscanned() const { return _rows_unscanned; }

        /** @} */

    private:
        /** @brief Gather the XP_MODBUS rows out of @p io into _rows. */
        void _collect(const Vars::Bank& io);
        /** @brief Sort _rows by (device, function code, rung, register). */
        void _sort(const Vars::Bank& io);
        /** @brief Cut the sorted rows into blocks. */
        void _cut(const Vars::Bank& io);
    };

    /** @name Pinned against the manual
     *
     *  The figures @ref dev_modbus publishes, checked at compile time so the
     *  planner and the doc cannot drift apart.
     *  @{ */

    static_assert(tchar_us(19200) == 572,  "one character at 19200 is 0.573 ms");
    static_assert(tchar_us(115200) == 95,  "one character at 115200 is 0.0955 ms");
    static_assert(t35_us(19200) == 2005,   "t3.5 at 19200 is 2.005 ms");
    static_assert(t35_us(115200) == 1750,  "t3.5 is spec-fixed at 1.75 ms above 19200");

    static_assert(fixed_us(19200, 10000) == 19441,  "19.45 ms fixed per transaction");
    static_assert(fixed_us(115200, 10000) == 12985, "12.99 ms fixed per transaction");

    static_assert(merge_gap(19200, 10000) == 17, "17 registers of gap at 19200");
    static_assert(merge_gap(115200, 10000) == 68, "68 registers of gap at 115200");

    // The byte counts the doc's cost table is built on. These come from wire.h,
    // so they also check that the codec and the cost model measure the same
    // frames.
    static_assert(request_len(FC_READ_HOLDING, 1) +
                  response_len(FC_READ_HOLDING, 1) == 15, "1 register is 15 bytes");
    static_assert(request_len(FC_READ_HOLDING, 2) +
                  response_len(FC_READ_HOLDING, 2) == 17, "2 registers are 17 bytes");
    static_assert(request_len(FC_READ_HOLDING, 10) +
                  response_len(FC_READ_HOLDING, 10) == 33, "10 registers are 33 bytes");
    static_assert(request_len(FC_READ_HOLDING, 125) +
                  response_len(FC_READ_HOLDING, 125) == 263, "125 registers are 263 bytes");
    static_assert(request_len(FC_WRITE_REGISTERS, 32) +
                  response_len(FC_WRITE_REGISTERS, 32) == 81, "32 written are 81 bytes");

    // And the resulting transaction times, to the tenth of a millisecond the
    // doc quotes.
    static_assert(txn_us(19200, 10000, FC_READ_HOLDING, 2) == 21729,   "21.7 ms");
    static_assert(txn_us(19200, 10000, FC_READ_HOLDING, 10) == 30881,  "30.9 ms");
    static_assert(txn_us(19200, 10000, FC_READ_HOLDING, 125) == 162441, "162.4 ms");
    static_assert(txn_us(115200, 10000, FC_READ_HOLDING, 125) == 36735, "36.9 ms");

    // The rung a 5 s timeout lands on, which is what every MI row on the
    // reference site declares: 1666 ms floors to the one-second rung.
    static_assert(period_of(5000) == 1666, "a 5 s timeout wants a 1.67 s period");
    static_assert(ladder_of(period_of(5000)) == L_1S, "which floors to the 1 s rung");

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_PLAN_H
