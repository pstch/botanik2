/**
 * botanik_fw.ino — Arduino OPTA sketch entry point.
 *
 * Deliberately thin: the Mbed Arduino core's main() calls setup() once then loop()
 * forever, so all we do here is build the firmware and pump it. The real wiring is
 * in firmware.h / firmware.cpp.
 *
 * The firmware lives in a function-local static, first constructed when setup()
 * calls firmware(). That defers construction until *after* the core's
 * init()/initVariant() — only then are the UARTs and PLC shared-memory region
 * valid to bind to — while keeping it in static storage (no heap), for the whole
 * program lifetime.
 */

#include "firmware.h"

using Botanik::Firmware::BotanikFirmware;

static BotanikFirmware& firmware() {
    static BotanikFirmware fw;
    return fw;
}

void setup() {
    firmware();   // force first-call construction now, inside setup()
}

void loop() {
    firmware().step();
}
