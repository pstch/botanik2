// REVIEW_35: DONE
/**
 * @file env.cpp
 * @brief HAL::Env - process-environment reads.
 *
 * Host-only in practice: getenv() is a platform call, so it belongs here rather
 * than in utils/. The OPTA build has no environment and always yields the
 * fallback.
 */
#include "platform.h"

#include "../utils/text.h"   // parse_uint

#ifdef TARGET_SIMULATOR
#  include <cstdlib>         // getenv
#endif

namespace Botanik { namespace HAL { namespace Env {

    uint32_t u32(const char* name, uint32_t fallback) {
#ifdef TARGET_SIMULATOR
        const char* v = std::getenv(name);
        if (v == nullptr || *v == '\0') return fallback;
        uint16_t len = 0;
        while (v[len] != '\0') len++;
        uint32_t n;
        return Utils::parse_uint(v, len, &n) ? n : fallback;
#else
        (void)name;
        return fallback;
#endif
    }

    uint8_t u8(const char* name, uint8_t fallback) {
        // Sentinel-free: read as u32 and reject what doesn't fit a byte. 256 is
        // out of range, not a wrap to 0.
        uint32_t n = u32(name, fallback);
        return n > 255 ? fallback : (uint8_t)n;
    }

}}}
