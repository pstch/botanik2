/**
 * @file vars/alarm_rows.cpp
 * @brief @see alarm_rows.h
 */
#include "libbotanik/vars/alarm_rows.h"
#include "libbotanik_features.h"

#if BOTANIK_FEATURE_VARS

#include <cstring>

#include "libbotanik/utils/assert.h"

namespace Botanik { namespace Vars {

    void BankAlarms::bind(Bank* io, Persist* persist) {
        _io = io;
        _persist = persist;
        _count = 0;
        _declared = 0;
        if (io == nullptr) return;
        // Runs to the END of the table rather than stopping at the last slot:
        // count() reports _declared, so the controller can see a tail it could
        // not take. _count is what the slots actually hold.
        for (uint16_t i = 0; i < io->count(); i++) {
            if (io->var(i).meta().kind != K_ALARM) continue;
            _declared++;
            if (_count >= Config::ALARM_SLOTS) continue;
            _row[_count++] = i;
        }
        BOTANIK_ASSERT(_declared <= Config::ALARM_SLOTS);
    }

    const char* BankAlarms::name(uint16_t i) const {
        return _io->var(_row[i]).meta().name;
    }
    const char* BankAlarms::desc(uint16_t i) const {
        return _io->var(_row[i]).meta().desc;
    }
    const char* BankAlarms::notes(uint16_t i) const {
        return _io->var(_row[i]).meta().notes;
    }
    Alarms::AlarmSpec BankAlarms::spec(uint16_t i) const {
        return alarm_spec(_io->var(_row[i]).meta());
    }
    bool BankAlarms::raw(uint16_t i) const {
        return _io->var(_row[i]).as_bool();
    }
    bool BankAlarms::forced(uint16_t i) const {
        return _io->var(_row[i]).override().present();
    }

    bool BankAlarms::force(uint16_t i, bool on) {
        if (_io == nullptr || i >= _count) return false;
        _io->var(_row[i]).override_set(Scalar::B(on));
        return true;
    }

    bool BankAlarms::force_clear(uint16_t i) {
        if (_io == nullptr || i >= _count) return false;
        Var h = _io->var(_row[i]);
        if (!h.override().present()) return false;
        // Through Persist: it owns the store, and a release leaving the stored
        // copy behind would come back at the next boot.
        if (_persist != nullptr) _persist->override_clear(h);
        else                     h.override_clear();
        return true;
    }

    bool BankAlarms::force_save(uint16_t i) {
        if (_io == nullptr || i >= _count) return false;
        return _persist != nullptr &&
               _persist->save_override(_io->var(_row[i])) == SET_OK;
    }

    int32_t BankAlarms::find(const char* name, uint16_t len) const {
        if (_io == nullptr || name == nullptr) return -1;
        // Through the bank's name index, then the rows - which are sorted,
        // since the scan ran the table in order.
        const uint16_t r = _io->find(name, len);
        if (r == VAR_NONE) return -1;
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)((lo + hi) / 2);
            if (_row[mid] == r) return (int32_t)mid;
            if (_row[mid] < r) lo = (uint16_t)(mid + 1);
            else                           hi = mid;
        }
        return -1;
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
