// REVIEW_35: TODO
#ifndef CONSOLE_TX_QUEUE_H
#define CONSOLE_TX_QUEUE_H

/**
 * @file tx_queue.h
 * @brief TxScheduler - priority-laned, coalescing, atomic transaction queue for
 *        the console TX path (see docs/TX_QUEUE_DESIGN.md).
 *
 * Renderers submit idempotent paint transactions (a whole row / rule / cell)
 * tagged with a priority lane and an optional coalescing key. A transaction is
 * { base position, content bytes (no leading move), end position }: the content
 * paints from base and the SCHEDULER's drain lets the emitter (the terminal)
 * emit the optimal move from the real cursor to base first - so the relative-
 * move optimization is preserved across transactions. The scheduler owns the
 * priority order + per-tick byte budget + atomicity; the emitter owns the actual
 * bytes. A newer transaction with the same key replaces an older un-emitted one,
 * so superseded updates never reach the wire.
 *
 * This replaces the cooperative TX sharing (per-component tx_below_cold() budgets
 * + per-page paint staging) the single droppable ring forced.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"

namespace Botanik { namespace Terminal {

    /** @brief Priority lanes, drained lowest-index (highest priority) first. */
    enum TxLane {
        TX_INPUT = 0,   ///< keystroke echo, editor reconcile, cursor placement
        TX_STRUCT,      ///< scrolls (DL/IL), headers, rules, page structure
        TX_VALUE,       ///< data-row value updates, telemetry, clock/metrics
        TX_LANE_COUNT
    };

    /** @brief Bytes the scheduler reserves for a transaction's leading move +
     *  attribute reset when sizing it against the budget/ring (the emitter's real
     *  move is usually shorter). A worst-case absolute CUP + SGR. */
    static const uint16_t TX_MOVE_RESERVE = 16;

    /** @brief Emits one transaction: move the real cursor to (@p base_row,
     *  @p base_col) with the minimal sequence, then the content, then leave it at
     *  (@p end_row, @p end_col). The terminal implements this so the move
     *  optimizes against the cursor it tracks. @return bytes emitted, or 0 if the
     *  ring could not hold it (the scheduler leaves it queued). */
    class TxEmitter {
    public:
        virtual ~TxEmitter() {}
        virtual uint16_t emit(uint16_t base_row, uint16_t base_col,
                              const char* content, uint16_t len,
                              uint16_t end_row, uint16_t end_col) = 0;
    };

    /** @brief Priority-laned, coalescing, atomic transaction queue (see the file
     *  header and docs/TX_QUEUE_DESIGN.md). Fixed-pool, no allocation. */
    class TxScheduler {

        /** @name Transaction pool
         *  @{ */
        static const uint8_t NIL = 0xFF;
        struct Slot {
            char     data[Config::TX_QUEUE_SLOT_LEN];   ///< content (no leading move)
            uint16_t len;
            uint16_t base_row, base_col;   ///< where the content paints from
            uint16_t end_row,  end_col;    ///< where it leaves the cursor
            uint32_t key;      ///< 0 = no coalescing; else <owner:16><id:16>
            uint8_t  next;     ///< intrusive link (free list or a lane FIFO)
        };
        Slot     _slot[Config::TX_QUEUE_SLOTS];
        uint8_t  _head[TX_LANE_COUNT];   ///< per-lane FIFO head (NIL = empty)
        uint8_t  _tail[TX_LANE_COUNT];   ///< per-lane FIFO tail
        uint8_t  _free;                  ///< free-list head
        uint32_t _dropped = 0;           ///< transactions dropped on pool overflow
        uint8_t  _phase   = 0;           ///< weighted-rotation cursor for drain()
        /** @} */

    public:
        TxScheduler();

        /** @name Submit / drain / drop
         *  @{ */

        /** @brief Queue a transaction on @p lane painting @p content (bytes, no
         *  leading move) from @p base leaving the cursor at @p end. @p key 0 skips
         *  coalescing; else a queued same-key transaction in the same lane is
         *  replaced in place. @p len is clamped to the slot size. @return false
         *  only if the pool is full and nothing could be evicted; on a full pool
         *  the oldest lowest-priority queued transaction is evicted first (counted
         *  in dropped()). */
        bool submit(uint8_t lane, uint32_t key,
                    uint16_t base_row, uint16_t base_col,
                    uint16_t end_row, uint16_t end_col,
                    const char* content, uint16_t len);

        /** @brief Drain queued transactions through @p emitter, highest lane
         *  first, up to @p budget bytes this tick. A transaction is emitted only
         *  when its content + reserved move fits the remaining budget (atomic);
         *  one that does not stays queued. @p max_budget is the full per-tick
         *  budget: a transaction larger than it can never fit a budget, so it is
         *  emitted as soon as the emitter has room (the oversized escape).
         *  @return bytes emitted this call. */
        uint16_t drain(TxEmitter& emitter, uint16_t budget, uint16_t max_budget);

        /** @brief Drop every queued transaction whose key owner (high 16 bits)
         *  equals @p owner (a page leaving focus). Not counted as a drop. */
        void drop_owner(uint16_t owner);

        /** @brief Drop all queued transactions. */
        void clear();

        /** @} */

        /** @name Introspection (tests / diagnostics)
         *  @{ */
        uint16_t queued() const;
        uint32_t dropped() const { return _dropped; }
        /** @} */

    private:
        uint8_t _alloc();
        void    _release(uint8_t idx);
        void    _lane_append(uint8_t lane, uint8_t idx);
        uint8_t _lane_pop(uint8_t lane);
        uint8_t _lane_find_key(uint8_t lane, uint32_t key) const;
        bool    _evict_lowest();
    };

}}

#endif // CONSOLE_TX_QUEUE_H
