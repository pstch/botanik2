// REVIEW_35: TODO
#ifndef CONSOLE_TX_QUEUE_H
#define CONSOLE_TX_QUEUE_H

/**
 * @file tx_queue.h
 * @brief TxScheduler - priority-laned, coalescing, atomic transaction queue for
 *        the console TX path (see docs/TX_QUEUE_DESIGN.md).
 *
 * Renderers submit self-contained, idempotent byte transactions (a whole row /
 * rule / cell) tagged with a priority lane and an optional coalescing key. The
 * scheduler drains them highest-lane-first into a TxSink within a per-tick byte
 * budget, emitting each transaction atomically (never a partial write); a
 * transaction that does not fit stays queued. A newer transaction with the same
 * key replaces an older un-emitted one, so superseded updates never reach the
 * wire. This replaces the cooperative TX sharing (per-component tx_below_cold()
 * budgets + per-page paint staging) the single droppable ring forced.
 *
 * Phase 1: the scheduler and its unit tests, standalone - not yet wired to the
 * AnsiTerminal emit path or the console loop (that lands per-renderer in the
 * later migration phases, which also delete the old cooperative machinery).
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

    /** @brief Byte sink the scheduler drains into: the TX ring in production, a
     *  capture buffer under test. `write()` is only called with len <= room(). */
    class TxSink {
    public:
        virtual ~TxSink() {}
        /** @brief Bytes that will be accepted right now (for the atomicity check). */
        virtual uint16_t room() const = 0;
        /** @brief Append @p len bytes; the caller guarantees @p len <= room(). */
        virtual void     write(const char* buf, uint16_t len) = 0;
    };

    /** @brief Priority-laned, coalescing, atomic transaction queue (see the file
     *  header and docs/TX_QUEUE_DESIGN.md). Fixed-pool, no allocation. */
    class TxScheduler {

        /** @name Transaction pool
         *  @{ */
        static const uint8_t NIL = 0xFF;
        struct Slot {
            char     data[Config::TX_QUEUE_SLOT_LEN];
            uint16_t len;
            uint32_t key;      ///< 0 = no coalescing; else <owner:16><id:16>
            uint8_t  next;     ///< intrusive link (free list or a lane FIFO)
        };
        Slot     _slot[Config::TX_QUEUE_SLOTS];
        uint8_t  _head[TX_LANE_COUNT];   ///< per-lane FIFO head (NIL = empty)
        uint8_t  _tail[TX_LANE_COUNT];   ///< per-lane FIFO tail
        uint8_t  _free;                  ///< free-list head
        uint32_t _dropped = 0;           ///< transactions dropped on pool overflow
        /** @} */

    public:
        TxScheduler();

        /** @name Submit / drain / drop
         *  @{ */

        /** @brief Queue a transaction on @p lane. @p key 0 skips coalescing; else a
         *  queued transaction with the same key in the same lane is replaced in
         *  place (keeping its position). @p len is clamped to the slot size.
         *  @return false only if the pool is full and nothing could be evicted; on
         *  a full pool the oldest lowest-priority queued transaction is evicted
         *  first (counted in dropped()). */
        bool submit(uint8_t lane, uint32_t key, const char* buf, uint16_t len);

        /** @brief Drain queued transactions into @p sink, highest lane first, up to
         *  @p budget bytes this tick. Each transaction is emitted atomically (only
         *  when it fully fits both the remaining budget and sink.room()); one that
         *  does not fit stays queued. @p max_budget is the full per-tick budget: a
         *  transaction larger than it can never fit a budget, so it is emitted as
         *  soon as sink.room() allows (the oversized escape). */
        void drain(TxSink& sink, uint16_t budget, uint16_t max_budget);

        /** @brief Drop every queued transaction whose key owner (high 16 bits)
         *  equals @p owner (e.g. a page leaving focus). Not counted as a drop. */
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
