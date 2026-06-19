#ifndef BOTANIK_FW_BOTANIK_ARDUINO_H
#define BOTANIK_FW_BOTANIK_ARDUINO_H

/**
 * @file botanik_arduino.h
 * @brief C-callable entry points for the Axel PLC IDE.
 *
 * The Axel PLC IDE owns the sketch (setup()/loop()) and only accepts imported
 * Arduino libraries, so botanik can't ship its own .ino. Instead it exposes these
 * two hooks: the PLC-generated program calls botanik_setup() once, then
 * botanik_step() every cycle. extern "C" keeps the symbols stable and reachable
 * from C and from the runtime's generated code.
 *
 * NOTE: botanik_setup / botanik_step are placeholder names, pending confirmation of
 * the exact hook the Axel runtime calls into. Adjust to match.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Construct the firmware. Call once, after the core's init() has run. */
void botanik_setup(void);

/** @brief One main-loop iteration. Call every PLC cycle. */
void botanik_step(void);

#ifdef __cplusplus
}
#endif

#endif // BOTANIK_FW_BOTANIK_ARDUINO_H
