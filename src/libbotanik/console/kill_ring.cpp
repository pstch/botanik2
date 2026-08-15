// REVIEW_35: TODO
/**
 * @file kill_ring.cpp
 * @brief KillRing - the console's one clipboard.
 */
#include "libbotanik/console/kill_ring.h"

#include <cstring>

namespace Botanik { namespace Terminal {

    uint16_t KillRing::set(const char* buf, uint16_t len) {
        if (buf == nullptr) len = 0;
        if (len > Config::KILL_RING_SIZE) len = Config::KILL_RING_SIZE;
        if (len > 0) memcpy(_buf, buf, len);
        _len = len;
        return _len;
    }

}}
