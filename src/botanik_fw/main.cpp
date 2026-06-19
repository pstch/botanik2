/**
 * @file main.cpp
 * @brief OPTA firmware entry point (standalone build).
 *
 * Hardware counterpart of botanik_sim/main.cpp. The instance is a function-local
 * static (see firmware.h), constructed after the Arduino core has run its own
 * init(); setup() does the boot wiring and run() drives the loop forever. The Axel
 * PLC IDE uses the botanik_setup()/botanik_step() hooks instead of this main().
 */

#include "botanik_fw/firmware.h"

int main() {
    Botanik::Firmware::instance().setup();   // first access constructs, then boots
    Botanik::Firmware::instance().run();     // never returns
    return 0;
}
