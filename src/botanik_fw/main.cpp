/**
 * @file main.cpp
 * @brief OPTA firmware entry point (standalone build).
 *
 * Hardware counterpart of botanik_sim/main.cpp. The Axel PLC IDE uses the
 * botanik_setup()/botanik_step() hooks instead of this main().
 */

#include "botanik_fw/firmware.h"

int main() {
    Botanik::Firmware::instance().setup();   // first access constructs, then boots
    Botanik::Firmware::instance().run();     // never returns
    return 0;
}
