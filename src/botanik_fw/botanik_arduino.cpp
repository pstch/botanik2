/**
 * @file botanik_arduino.cpp
 * @brief Implements the C-callable hooks the Axel PLC runtime drives.
 *
 * The firmware lives in a function-local static, first constructed when
 * botanik_setup() is called - deferred until after the Arduino core's
 * init()/initVariant() (only then are the UARTs and PLC shared-memory region valid
 * to bind to), while staying in static storage (no heap) for the whole run. This
 * mirrors the old botanik_fw.ino, which can't ship inside a library.
 */

#include "botanik_fw/botanik_arduino.h"
#include "botanik_fw/firmware.h"

using Botanik::Firmware::BotanikFirmware;

static BotanikFirmware& firmware() {
    static BotanikFirmware fw;
    return fw;
}

extern "C" void botanik_setup(void) {
    firmware();   // force first-call construction now
}

extern "C" void botanik_step(void) {
    firmware().step();
}
