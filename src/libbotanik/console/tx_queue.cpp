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

    bool TxScheduler::submit(uint8_t lane, uint32_t key, const char* buf, uint16_t len) {
        if (lane >= TX_LANE_COUNT) return false;
        if (len > Config::TX_QUEUE_SLOT_LEN) len = (uint16_t)Config::TX_QUEUE_SLOT_LEN;

        // Coalesce: replace a same-key queued transaction in place (keep position).
        if (key != 0) {
            uint8_t idx = _lane_find_key(lane, key);
            if (idx != NIL) {
                memcpy(_slot[idx].data, buf, len);
                _slot[idx].len = len;
                return true;
            }
        }

        uint8_t idx = _alloc();
        if (idx == NIL) {
            if (!_evict_lowest()) return false;
            idx = _alloc();
            if (idx == NIL) return false;
        }
        _slot[idx].key = key;
        _slot[idx].len = len;
        memcpy(_slot[idx].data, buf, len);
        _lane_append(lane, idx);
        return true;
    }

    void TxScheduler::drain(TxSink& sink, uint16_t budget, uint16_t max_budget) {
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++) {
            while (_head[l] != NIL) {
                uint8_t  idx  = _head[l];
                uint16_t len  = _slot[idx].len;
                uint16_t room = sink.room();
                if (len <= budget && len <= room) {
                    sink.write(_slot[idx].data, len);
                    budget = (uint16_t)(budget - len);
                    _release(_lane_pop(l));
                } else if (len > max_budget && len <= room) {
                    // Oversized: can never fit a budget, emit once there is room.
                    sink.write(_slot[idx].data, len);
                    budget = 0;
                    _release(_lane_pop(l));
                    break;
                } else {
                    break;   // atomic: leave queued until it fully fits
                }
            }
            if (budget == 0) break;
        }
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

    uint16_t TxScheduler::queued() const {
        uint16_t n = 0;
        for (uint8_t l = 0; l < TX_LANE_COUNT; l++)
            for (uint8_t i = _head[l]; i != NIL; i = _slot[i].next) n++;
        return n;
    }

}}
