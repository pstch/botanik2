/**
 * @file main.cpp
 * @brief OPTA firmware entry point.
 *
 * Hardware counterpart of botanik_sim/main.cpp. The simulator owns the PLC shared
 * memory locally and wires everything up in main(); here BotanikFirmware does that
 * wiring in its constructor (the setup() work) against the coprocessor's memory,
 * and run() drives the main loop. The instance is a function-local static so it is
 * constructed after the Arduino core has run its own init().
 */

#include "botanik_fw/firmware.h"

int main() {
    static Botanik::Firmware::BotanikFirmware firmware;
    firmware.run();   // never returns
    return 0;
}
