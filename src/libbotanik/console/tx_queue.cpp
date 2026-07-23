// REVIEW_35: TODO
/**
 * @file tx_queue.cpp
 * @brief TxScheduler implementation (see tx_queue.h + docs/TX_QUEUE_DESIGN.md).
 */
#include "libbotanik/console/tx_queue.h"

namespace Botanik { namespace Terminal {

    // Slot indices are uint8_t with 0xFF reserved as the NIL link.
    static_assert(Config::TX_QUEUE_SLOTS < 255, "TX_QUEUE_SLOTS must fit a uint8_t index (< 255)");

    TxScheduler::TxScheduler() { clear(); }

    void TxScheduler::clear() {
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++) { _head[l] = NIL; _tail[l] = NIL; _starve[l] = 0; }
        // Chain every slot onto the free list.
        for (uint16_t i = 0; i < Config::TX_QUEUE_SLOTS; i++)
            _slot[i].next = (uint8_t)(i + 1 < Config::TX_QUEUE_SLOTS ? i + 1 : NIL);
        _free = (Config::TX_QUEUE_SLOTS > 0) ? 0 : NIL;
    }

    uint8_t TxScheduler::_alloc() {
        if (_free == NIL) return NIL;
        uint8_t idx = _free;
        _free = _slot[idx].next;
        return idx;
    }

    void TxScheduler::_release(uint8_t idx) {
        _slot[idx].next = _free;
        _free = idx;
    }

    void TxScheduler::_lane_append(uint8_t lane, uint8_t idx) {
        _slot[idx].next = NIL;
        if (_tail[lane] == NIL) { _head[lane] = idx; _tail[lane] = idx; }
        else { _slot[_tail[lane]].next = idx; _tail[lane] = idx; }
    }

    uint8_t TxScheduler::_lane_pop(uint8_t lane) {
        uint8_t idx = _head[lane];
        if (idx == NIL) return NIL;
        _head[lane] = _slot[idx].next;
        if (_head[lane] == NIL) _tail[lane] = NIL;
        return idx;
    }

    uint8_t TxScheduler::_lane_find_key(uint8_t lane, uint32_t key) const {
        for (uint8_t i = _head[lane]; i != NIL; i = _slot[i].next)
            if (_slot[i].key == key) return i;
        return NIL;
    }

    bool TxScheduler::_evict_lowest() {
        // Evict the oldest (head) transaction of the lowest-priority non-empty
        // lane, so a submit under pressure never displaces higher-priority work.
        for (int l = TX_LANE_COUNT - 1; l >= 0; l--) {
            if (_head[l] != NIL) {
                _release(_lane_pop((uint8_t)l));
                _dropped++;
                return true;
            }
        }
        return false;
    }

    bool TxScheduler::submit(uint8_t lane, uint32_t key,
                             uint16_t base_row, uint16_t base_col,
                             uint16_t end_row, uint16_t end_col,
                             const char* content, uint16_t len) {
        if (lane >= TX_LANE_COUNT) return false;
        if (len > Config::TX_QUEUE_SLOT_LEN) len = (uint16_t)Config::TX_QUEUE_SLOT_LEN;

        // Coalesce: reuse a same-key queued transaction's slot in place (keeping
        // its position); else allocate (evicting the lowest priority if full).
        uint8_t idx = (key != 0) ? _lane_find_key(lane, key) : NIL;
        if (idx == NIL) {
            idx = _alloc();
            if (idx == NIL) {
                if (!_evict_lowest()) return false;
                idx = _alloc();
                if (idx == NIL) return false;
            }
            _lane_append(lane, idx);
        }
        _slot[idx].key      = key;
        _slot[idx].len      = len;
        _slot[idx].base_row = base_row;
        _slot[idx].base_col = base_col;
        _slot[idx].end_row  = end_row;
        _slot[idx].end_col  = end_col;
        memcpy(_slot[idx].data, content, len);
        return true;
    }

    uint16_t TxScheduler::drain(TxEmitter& emitter, uint16_t budget, uint16_t max_budget) {
        uint16_t sent = 0;

        // Bounded-starvation strict priority. Normally the lanes drain in strict
        // order - INPUT, then STRUCT, then VALUE - so keystroke echo precedes
        // structure, and structure (a page's static template, a scroll) precedes
        // the values that fill it; the initial draw is clean, never scattered. No
        // lane is capped below its transaction, so a mid-sized structural row can
        // never be stuck behind a reserve. To stop a sustained higher-lane flood
        // (e.g. continuous table churn) from freezing a lower one, a lane that has
        // been skipped with queued work for TX_STARVE_LIMIT consecutive drains is
        // promoted to the front of THIS drain - so the clock keeps ticking, at a
        // bounded worst-case latency, without reordering on a normal drain.
        uint8_t order[TX_LANE_COUNT];
        for (uint8_t i = 0; i < TX_LANE_COUNT; i++) order[i] = i;
        uint8_t lead = NIL;
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++)
            if (_head[l] != NIL && _starve[l] >= TX_STARVE_LIMIT) { lead = l; break; }
        if (lead != NIL && lead != 0) {
            for (uint8_t i = lead; i > 0; i--) order[i] = order[(uint8_t)(i - 1)];
            order[0] = lead;
        }

        bool emitted[TX_LANE_COUNT] = { false };
        for (uint8_t oi = 0; oi < TX_LANE_COUNT; oi++) {
            uint8_t l = order[oi];
            // The promoted (starved) lane force-emits its head: it has waited
            // TX_STARVE_LIMIT drains, and its transaction may be larger than the
            // room below the floor (which a higher-lane flood keeps near zero), so
            // a plain budget check would never let it through. One forced emit
            // per promotion overshoots the floor by a single transaction - bounded,
            // and only while a lane is actually starving.
            bool force = (oi == 0 && lead != NIL);
            while (_head[l] != NIL) {
                const Slot& s = _slot[_head[l]];
                uint16_t need = (uint16_t)(s.len + TX_MOVE_RESERVE);
                bool oversized = need > max_budget;   // can never fit a budget
                // Oversized emits while the ring is still shallow (budget>0), one
                // step at a time; a fitting transaction just needs the budget. The
                // forced head bypasses the budget check (emit() still gates on the
                // physical ring room), so a starved lane always makes progress.
                if (!force && (oversized ? (budget == 0) : (need > budget))) break;
                uint16_t used = emitter.emit(s.base_row, s.base_col, s.data, s.len,
                                             s.end_row, s.end_col);
                if (used == 0) break;                 // ring full: wait
                sent   = (uint16_t)(sent + used);
                budget = (used < budget) ? (uint16_t)(budget - used) : 0;
                emitted[l] = true;
                _release(_lane_pop(l));
                if (oversized || (!force && budget == 0)) break;
                force = false;   // only the first (starved) transaction is forced
            }
        }

        // A lane that had work but emitted nothing this drain got starved; one
        // that emitted (or is now empty) is caught up. TX_STARVE_LIMIT such drains
        // promotes it to the front next time.
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++) {
            if (emitted[l] || _head[l] == NIL) _starve[l] = 0;
            else if (_starve[l] < 0xFFFF)      _starve[l]++;
        }
        return sent;
    }

    void TxScheduler::drop_owner(uint16_t owner) {
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++) {
            uint8_t prev = NIL, i = _head[l];
            while (i != NIL) {
                uint8_t next = _slot[i].next;
                if ((uint16_t)(_slot[i].key >> 16) == owner) {
                    if (prev == NIL) _head[l] = next; else _slot[prev].next = next;
                    if (_tail[l] == i) _tail[l] = prev;
                    _release(i);
                } else {
                    prev = i;
                }
                i = next;
            }
        }
    }

    void TxScheduler::drop_key(uint8_t lane, uint32_t key) {
        if (lane >= TX_LANE_COUNT || key == 0) return;
        uint8_t prev = NIL, i = _head[lane];
        while (i != NIL) {
            uint8_t next = _slot[i].next;
            if (_slot[i].key == key) {
                if (prev == NIL) _head[lane] = next; else _slot[prev].next = next;
                if (_tail[lane] == i) _tail[lane] = prev;
                _release(i);
                return;   // keys are unique within a lane
            }
            prev = i;
            i = next;
        }
    }

    bool TxScheduler::has_key(uint8_t lane, uint32_t key) const {
        if (lane >= TX_LANE_COUNT || key == 0) return false;
        return _lane_find_key(lane, key) != NIL;
    }

    uint16_t TxScheduler::queued() const {
        uint16_t n = 0;
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++)
            for (uint8_t i = _head[l]; i != NIL; i = _slot[i].next) n++;
        return n;
    }

}}
