// REVIEW_35: TODO
#ifndef BOTANIK_FW_BOTANIK_ARDUINO_H
#define BOTANIK_FW_BOTANIK_ARDUINO_H

/**
 * @file botanik_arduino.h
 * @brief C-callable entry points for the Axel PLC IDE.
 *
 * The Axel IDE owns the sketch and only accepts imported Arduino libraries, so
 * botanik can't ship its own .ino. It exposes two hooks: botanik_setup() once,
 * then botanik_step() every cycle. extern "C" keeps the symbols reachable from
 * generated code.
 *
 * NOTE: the hook names are placeholders, pending confirmation of the exact Axel
 * hook.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Construct the firmware. Call once, after the core's init() ran. */
void botanik_setup(void);

/** @brief One main-loop iteration. Call every PLC cycle. */
void botanik_step(void);

#ifdef __cplusplus
}
#endif

#endif // BOTANIK_FW_BOTANIK_ARDUINO_H
