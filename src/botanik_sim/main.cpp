// Botanik simulator entry point.
//
// This file is the equivalent of an Arduino sketch: it defines the site-specific
// PLC payload types and wires the library components together. On OPTA, this
// plumbing lives in the .ino sketch; here it lives in main().
//
// The PLC shared memory is allocated as local statics (simulator owns it). On OPTA,
// typed pointers are obtained via HAL::get_plc_input() / HAL::get_plc_output() and
// validated against the PLC IDE's generated types with a static_assert.

#include "libbotanik/hal/platform.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/logs/writer.h"
#include "libbotanik/plcio/vars.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/plcio/emulator.h"
#include "libbotanik/plcio/logs.h"
#include "libbotanik/utils/serial.h"

// ---------------------------------------------------------------------------
// Site-specific PLC payload types.
// On OPTA: define these in the sketch to match the PLC IDE's struct layout,
// then add:
//   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<PlcInputData_t>));
//   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<PlcOutputData_t>));
// ---------------------------------------------------------------------------
struct PlcInputData_t  {};   // no site-specific data beyond the library log fields
struct PlcOutputData_t {};   // no site-specific data beyond the library log fields

int main() {
    using namespace Botanik;

    // Shared PLC memory — both handler and emulator point at the same region.
    // On OPTA: PlcIO::PlcInput<PlcInputData_t>* plcin =
    //              static_cast<PlcIO::PlcInput<PlcInputData_t>*>(HAL::get_plc_input());
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    uint32_t timestamp_ms = 0;

    // Serial I/O
    Utils::BufferedSerial console_serial(HAL::get_serial(HAL::CONSOLE));
    Utils::BufferedSerial monitor_serial(HAL::get_serial(HAL::MONITOR));

    // Logging
    Logs::Logger         logger;
    Logs::SerialLogWriter monitor(monitor_serial);
    logger.add_writer(&monitor);

    // PLC sync layer
    PlcIO::PlcSyncHandler<PlcInputData_t, PlcOutputData_t> plc_handler(
        &shared_in, &shared_out, &timestamp_ms);
    PlcIO::PlcEmulator<PlcInputData_t, PlcOutputData_t> plc_emulator(
        &shared_in, &shared_out, &timestamp_ms);
    PlcIO::PlcLogWriter  plc_log_writer(plc_emulator.context(), &timestamp_ms);
    PlcIO::PlcLogReader  plc_log_reader(plc_handler.context(), &logger, &timestamp_ms);

    Scheduler::LoopScheduler scheduler;

    while (true) {
        timestamp_ms = (uint32_t)HAL::uptime_ms();

        if (!scheduler.tick()) continue;

        console_serial.step();
        monitor_serial.step();

        plc_handler.step();
        plc_emulator.step();
        plc_log_reader.step();
    }
}
