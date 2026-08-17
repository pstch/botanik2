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

    void BankAlarms::bind(const Bank* io) {
        _io = io;
        _count = 0;
        _declared = 0;
        if (io == nullptr) return;
        // Runs to the END of the table rather than stopping at the last slot:
        // _count saturates, so only _declared can tell a table that filled the
        // slots exactly from one that lost a tail.
        for (uint16_t i = 0; i < io->count(); i++) {
            if (io->var(i).meta()->scope != SCOPE_A) continue;
            _declared++;
            if (_count >= Config::ALARM_SLOTS) continue;
            _row[_count++].index = i;
        }
        BOTANIK_ASSERT(_declared <= Config::ALARM_SLOTS);
    }

    const char* BankAlarms::name(uint16_t i) const {
        return _io->var(_row[i]).meta()->name;
    }
    const char* BankAlarms::desc(uint16_t i) const {
        return _io->var(_row[i]).meta()->desc;
    }
    const char* BankAlarms::notes(uint16_t i) const {
        return _io->var(_row[i]).meta()->notes;
    }
    Alarms::AlarmSpec BankAlarms::spec(uint16_t i) const {
        return alarm_spec(*_io->var(_row[i]).meta());
    }
    bool BankAlarms::raw(uint16_t i) const {
        return _io->var(_row[i]).b();
    }
    bool BankAlarms::forced(uint16_t i) const {
        return _io->var(_row[i]).override().present();
    }

    int32_t BankAlarms::find(const char* name, uint16_t len) const {
        if (_io == nullptr || name == nullptr) return -1;
        // Through the bank's name index, then the rows - which are sorted,
        // since the scan ran the table in order.
        const VarRef r = _io->find(name, len);
        if (r.index == VAR_NONE) return -1;
        uint16_t lo = 0, hi = _count;
        while (lo < hi) {
            const uint16_t mid = (uint16_t)((lo + hi) / 2);
            if (_row[mid].index == r.index) return (int32_t)mid;
            if (_row[mid].index < r.index) lo = (uint16_t)(mid + 1);
            else                           hi = mid;
        }
        return -1;
    }

}}  // namespace Botanik::Vars

#endif // BOTANIK_FEATURE_VARS
