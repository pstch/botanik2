/**
 * @file firmware.cpp
 * @brief Constructs the OPTA firmware singleton against the coprocessor's PLC
 *        shared memory (see firmware.h).
 *
 * On OPTA, verify the site payload matches the PLC IDE's generated types by adding,
 * once those types are available to the integrator's entry file:
 *   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<PlcInputData_t>));
 *   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<PlcOutputData_t>));
 */

#include "botanik_fw/firmware.h"

#include "libbotanik.h"   // HAL, PlcIO

namespace Botanik { namespace Firmware {

// USB-CDC / UART console baud. The diagnostic monitor (Serial1) runs at the same
// rate. Plain constant for now; promote to libbotanik_config.h if a site needs it.
static const unsigned long CONSOLE_BAUD = 115200;

::BotanikSite& instance() {
    // The PLC shared region is owned by the M4 coprocessor; we only bind to it.
    static ::BotanikSite fw(
        static_cast<PlcIO::PlcInput<PlcInputData_t>*  >(HAL::get_plc_input()),
        static_cast<PlcIO::PlcOutput<PlcOutputData_t>*>(HAL::get_plc_output()),
        (uint32_t)CONSOLE_BAUD, /*cols=*/80, /*rows=*/24);
    return fw;
}

}}  // namespace Botanik::Firmware
