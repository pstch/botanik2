/**
 * botanik_fw.ino — reference Arduino OPTA sketch entry point.
 *
 * Mirrors what the Axel PLC IDE's "sketch" window holds: the core's main() calls
 * setup() once then loop() forever, and both just defer to the botanik_setup()/
 * botanik_step() hooks. Those hooks are defined in the project-root entry point
 * (here: botanik_arduino.cpp, alongside the firmware wiring), NOT in libbotanik -
 * the imported library only provides the building blocks via <libbotanik.h>.
 */

#include "botanik_arduino.h"

void setup() {
    botanik_setup();
}

void loop() {
    botanik_step();
}
