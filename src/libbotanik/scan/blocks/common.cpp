/**
 * @file common.cpp
 * @brief The shared block helpers.
 */

#include "common.h"

namespace Botanik { namespace Scan {

    void accumulate(uint32_t& elapsed, uint32_t limit, uint32_t dt_ms) {
        if (elapsed >= limit) return;
        const uint32_t room = limit - elapsed;
        elapsed += (dt_ms < room) ? dt_ms : room;
    }

    void countdown(uint32_t& remaining, uint32_t dt_ms) {
        remaining = (dt_ms < remaining) ? (remaining - dt_ms) : 0;
    }

    float clampf(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    float seconds(uint32_t dt_ms) {
        return (float)dt_ms / 1000.0f;
    }

}}  // namespace Botanik::Scan
