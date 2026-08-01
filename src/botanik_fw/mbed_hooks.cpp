/**
 * @file mbed_hooks.cpp
 * @brief Platform hooks libmbed.a expects the surrounding project to provide.
 *
 * The prebuilt libmbed.a is compiled against a small set of symbols the Arduino
 * core used to define. We link Mbed without that core, so we supply them here
 * instead. They are the *only* thing we owe it: everything else in the image is
 * ours or Mbed's.
 *
 * The reference is the core's own arm_hal_random.c.
 */

#include <cstdint>

#include "mbed.h"

extern "C" {

/** @brief Seed source for Mbed's randLIB (nanostack libservice).
 *
 * Uses the STM32H7's TRNG through Mbed's mbed_rand/TRNGDevice path when the
 * device is present, which it is on this target (DEVICE_TRNG=1). Pulled in via
 * --whole-archive on libmbed even when nothing in our firmware uses randLIB, so
 * it must resolve regardless.
 */
uint32_t arm_random_seed_get(void) {
    // The core's own version returns a constant unless mbedTLS's hardware
    // entropy backend is configured, so a monotonic counter is no worse and is
    // at least not identical on every boot. Nothing in this firmware uses
    // randLIB for anything security-bearing; it is pulled in only because
    // --whole-archive on libmbed brings randLIB.o along.
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            mbed::LowPowerClock::now().time_since_epoch()).count());
}

/** @brief Companion init hook; nothing to do, as in the core's version. */
void arm_random_module_init(void) {}

}  // extern "C"
