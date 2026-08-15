/**
 * @file mbed_hooks.cpp
 * @brief Platform hooks libmbed.a expects the surrounding project to provide.
 *
 * The prebuilt libmbed.a is compiled against a small set of symbols the Arduino
 * core defines, and we link Mbed without that core. They are the *only* thing we
 * owe it. The reference is the core's own arm_hal_random.c.
 */

#include <cstdint>

#include "mbed.h"

extern "C" {

/** @brief Seed source for Mbed's randLIB (nanostack libservice).
 *
 * Must resolve even though nothing in this firmware uses randLIB: --whole-archive
 * on libmbed brings randLIB.o along.
 */
uint32_t arm_random_seed_get(void) {
    // A monotonic counter, which is no worse than the core's own version (a
    // constant unless mbedTLS's hardware entropy backend is configured) and at
    // least differs between boots. Nothing here is security-bearing.
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            mbed::LowPowerClock::now().time_since_epoch()).count());
}

/** @brief Companion init hook; nothing to do, as in the core's version. */
void arm_random_module_init(void) {}

}  // extern "C"
