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
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++) { _head[l] = NIL; _tail[l] = NIL; }
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

        // Strict priority order: INPUT, then STRUCT, then VALUE. A higher lane
        // always drains before a lower one within a tick, so keystroke echo
        // precedes structure, and structure (a page's static template, a scroll)
        // precedes the values that fill it - never the reverse, which would
        // scatter the initial draw or paint a value onto a row a later structural
        // clear then wipes. To keep a sustained flood of a higher lane from fully
        // starving a lower one, each lane holds back a small reserve of the budget
        // for the lower lanes when any of them has queued work (so e.g. the clock
        // still ticks during a full-screen structural repaint). The reserve is
        // capped at half the budget so the current lane always makes progress too,
        // and it stays small enough that a low baud (tiny budget) still drains.
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++) {
            bool lower_work = false;
            for (uint8_t j = (uint8_t)(l + 1); j < TX_LANE_COUNT; j++)
                if (_head[j] != NIL) lower_work = true;
            uint16_t reserve = 0;
            if (lower_work) {
                reserve = TX_LANE_RESERVE;
                if (reserve > (uint16_t)(budget / 2)) reserve = (uint16_t)(budget / 2);
            }
            uint16_t cap = (budget > reserve) ? (uint16_t)(budget - reserve) : 0;
            while (_head[l] != NIL) {
                const Slot& s = _slot[_head[l]];
                uint16_t need = (uint16_t)(s.len + TX_MOVE_RESERVE);
                bool oversized = need > max_budget;   // can never fit a budget
                // Oversized emits only while there is room below the floor (cap>0),
                // so it makes one step of progress without flooding past the
                // latency floor. A fitting transaction just needs to fit the cap.
                if (oversized ? (cap == 0) : (need > cap)) break;
                uint16_t used = emitter.emit(s.base_row, s.base_col, s.data, s.len,
                                             s.end_row, s.end_col);
                if (used == 0) break;                 // ring full: wait
                sent   = (uint16_t)(sent + used);
                budget = (used < budget) ? (uint16_t)(budget - used) : 0;
                cap    = (used < cap)    ? (uint16_t)(cap - used)    : 0;
                _release(_lane_pop(l));
                if (oversized || cap == 0) break;
            }
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
