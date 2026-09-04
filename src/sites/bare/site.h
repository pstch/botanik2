#ifndef BARE_SITE_H
#define BARE_SITE_H

/**
 * @file site.h
 * @brief HAND-WRITTEN - the bare site: a name, and nothing else.
 *
 * The minimum a site can be. It declares no variables, no programs, no commands
 * and no simulated field, which is the point: what builds here is libbotanik
 * with every optional feature compiled out, so the seam that makes them optional
 * is exercised by something rather than asserted by a comment.
 *
 * Same surface as any other site, so a harness never names it:
 * `Site::firmware()` and `Site::simulate()`.
 */

#include <cstdint>

#include "libbotanik.h"

namespace Site {

/** @brief No commands, no home fields, no devices. Field order matches
 *  Botanik::SiteDescriptor. */
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    nullptr, 0,                      // site shell commands
    nullptr, 0,                      // home-screen readings
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    nullptr,                         // what each unit answers
#endif
    nullptr, nullptr, 0, 0, 0,       // the variable table, and there is none
};

#if defined(TARGET_SIMULATOR) && BOTANIK_FEATURE_VARS
/** @brief No field to animate: this site declares no rows. */
inline void simulate(Botanik::Vars::Bank& io, uint32_t now_ms) {
    (void)io; (void)now_ms;
}
#endif

/** @brief The firmware, bound to this site. A FUNCTION-LOCAL static: on the
 *  OPTA it must be constructed after the Arduino core's init() has brought up
 *  the UARTs, and a file-scope object would run before it. No subclass - this
 *  site adds nothing to the base, which is the point of it. */
inline Botanik::BotanikFirmware& firmware() {
    static Botanik::BotanikFirmware fw(g_site);
    return fw;
}

}  // namespace Site



#endif // BARE_SITE_H