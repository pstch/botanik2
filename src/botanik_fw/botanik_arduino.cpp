// REVIEW_35: TODO
/**
 * @file botanik_arduino.cpp
 * @brief Implements the C-callable hooks the Axel PLC runtime drives.
 *
 * The firmware is a function-local static (firmware.h), first constructed on
 * botanik_setup() - deferred until after the Arduino core's
 * init()/initVariant(), when the UARTs and PLC shared-memory region are valid
 * to bind to.
 */

#include "botanik_fw/botanik_arduino.h"
#include "botanik_fw/firmware.h"

extern "C" void botanik_setup(void) {
    Botanik::Firmware::instance().setup();   // first access constructs + boots
}

extern "C" void botanik_step(void) {
    Botanik::Firmware::instance().loop();
}
