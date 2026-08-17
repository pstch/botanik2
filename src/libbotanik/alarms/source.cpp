/**
 * @file alarms/source.cpp
 * @brief @see source.h
 *
 * Not feature-gated: this is the alarm source a build with NO variable model
 * uses, so it is exactly the file that must survive one.
 */
#include "libbotanik/alarms/source.h"

namespace Botanik { namespace Alarms {

    int32_t StaticAlarmTable::find(const char* n, uint16_t len) const {
        if (n == nullptr || _rows == nullptr) return -1;
        for (uint16_t i = 0; i < _count; i++) {
            const char* c = _rows[i].name;
            if (c == nullptr) continue;
            uint16_t k = 0;
            while (k < len && c[k] != '\0' && c[k] == n[k]) k++;
            if (k == len && c[k] == '\0') return (int32_t)i;
        }
        return -1;
    }

}}  // namespace Botanik::Alarms
