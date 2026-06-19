/**
 * @file botanik_arduino.cpp
 * @brief Implements the C-callable hooks the Axel PLC runtime drives.
 *
 * The firmware lives in a function-local static (see firmware.h), first constructed
 * when botanik_setup() is called - deferred until after the Arduino core's
 * init()/initVariant() (only then are the UARTs and PLC shared-memory region valid
 * to bind to), while staying in static storage (no heap) for the whole run. This
 * mirrors the old botanik_fw.ino, which can't ship inside a library.
 */

#include "botanik_fw/botanik_arduino.h"
#include "botanik_fw/firmware.h"

extern "C" void botanik_setup(void) {
    Botanik::Firmware::instance().setup();   // first access constructs, then boots
}

extern "C" void botanik_step(void) {
    Botanik::Firmware::instance().loop();
}
