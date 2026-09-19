/**
 * @file env.cpp
 * @brief HAL::Env - process-environment reads.
 *
 * Host-only in practice: getenv() is a platform call, so it belongs here rather
 * than in utils/. The OPTA build has no environment and always yields the
 * fallback.
 */
#include "libbotanik/hal/platform.h"

#ifdef TARGET_SIMULATOR
#  include <cstdlib>                    // getenv
#endif

namespace Botanik { namespace HAL { namespace Env {

#ifdef TARGET_SIMULATOR
    namespace {
        /** @brief A whole NUL-terminated decimal into @p out, false on empty,
         *  a non-digit, trailing junk or overflow.
         *
         *  Its own, not Utils::parse_uint(): `utils/` is built ON the HAL -
         *  BufferedSerial, uptime and the assert trap all reach for it - so
         *  taking a text helper from there would be the platform layer
         *  depending on what depends on it. Eight lines is the cheaper of the
         *  two, and the contract wanted here is Env's own: anything that is not
         *  a plain number yields the fallback. */
        bool _decimal(const char* v, uint32_t* out) {
            if (v == nullptr || *v == '\0') return false;
            uint32_t n = 0;
            for (; *v != '\0'; v++) {
                if (*v < '0' || *v > '9') return false;
                const uint32_t d = (uint32_t)(*v - '0');
                if (n > (0xFFFFFFFFu - d) / 10u) return false;   // would wrap
                n = n * 10u + d;
            }
            *out = n;
            return true;
        }
    }
#endif

    const char* str(const char* name, const char* fallback) {
#ifdef TARGET_SIMULATOR
        const char* v = std::getenv(name);
        return (v == nullptr || *v == '\0') ? fallback : v;
#else
        (void)name;
        return fallback;
#endif
    }

    uint32_t u32(const char* name, uint32_t fallback) {
#ifdef TARGET_SIMULATOR
        uint32_t n;
        return _decimal(std::getenv(name), &n) ? n : fallback;
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
