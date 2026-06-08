/**
 * @file main.cpp
 * @brief Botanik simulator entry point.
 *
 * This file is the equivalent of an Arduino sketch: it defines the site-specific
 * PLC payload types and wires the library components together. On OPTA, this
 * plumbing lives in the .ino sketch; here it lives in main().
 *
 * The PLC shared memory is allocated as local statics (simulator owns it). On OPTA,
 * typed pointers are obtained via HAL::get_plc_input() / HAL::get_plc_output() and
 * validated against the PLC IDE's generated types with a static_assert.
 */

#include "libbotanik.h"

#include <csignal>   // signal, sig_atomic_t

// ---------------------------------------------------------------------------
// Site-specific PLC payload types.
// On OPTA: define these in the sketch to match the PLC IDE's struct layout,
// then add:
//   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<PlcInputData_t>));
//   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<PlcOutputData_t>));
// ---------------------------------------------------------------------------
struct PlcInputData_t  {};   // no site-specific data beyond the library log fields
struct PlcOutputData_t {};   // no site-specific data beyond the library log fields

// Cleared by the shutdown signal handler so the main loop breaks and main()
// returns — running atexit (terminal restore) and any sanitizer exit-time checks,
// instead of the process being killed mid-loop. sig_atomic_t is the signal-safe
// flag type.
static volatile sig_atomic_t botanik_running = 1;

extern "C" void botanik_sim_shutdown(int) { botanik_running = 0; }

int main() {
    using namespace Botanik;

    // Setup code
    // -----------------------------------------------------------------------

    // Raw terminal + console serial.
    HAL::setup_raw_console();
    HAL::get_serial(HAL::CONSOLE)->begin(9800);

    // Graceful shutdown on Ctrl+C / SIGTERM: flip the run flag so the main loop
    // breaks and main() returns cleanly. Overrides the HAL handler for these two
    // signals; SIGSEGV stays with the HAL handler to restore the terminal on a crash.
    std::signal(SIGINT,  botanik_sim_shutdown);
    std::signal(SIGTERM, botanik_sim_shutdown);

    // Shared PLC memory — both handler and emulator point at the same region.
    // On OPTA: PlcIO::PlcInput<PlcInputData_t>* plcin =
    //              static_cast<PlcIO::PlcInput<PlcInputData_t>*>(HAL::get_plc_input());
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    // TODO: move the storage of the "global" timestamp_ms to SchedulerContext
    uint32_t timestamp_ms = 0;

    // Serial I/O
    Utils::BufferedSerial console_serial(HAL::get_serial(HAL::CONSOLE));
    Utils::BufferedSerial monitor_serial(HAL::get_serial(HAL::MONITOR));

    // Logging
    Logs::Logger          logger;
    Logs::SerialLogWriter monitor(monitor_serial);

    // Scheduler (constructed before terminal so we can pass its context)
    Scheduler::LoopScheduler scheduler(Config::TASK_PERIOD_MS, &logger);

    // PLC sync layer (handler + log reader on main thread)
    using SyncHandler = PlcIO::PlcSyncHandler<PlcInputData_t, PlcOutputData_t>;
    SyncHandler         plc_handler(&shared_in, &shared_out, &timestamp_ms, &logger);
    PlcIO::PlcLogReader plc_log_reader(plc_handler.context(), &logger);

    // Terminal UI
    Terminal::AnsiTerminal terminal(console_serial, 80, 24, &timestamp_ms, &logger,
                                    scheduler.context(), plc_handler.context());
    Pages::ShellPage shell_page(terminal);
    Pages::LogsPage  logs_page(terminal);

    // Register logging sinks
    logger.add_writer(&monitor);
    logger.add_writer(&shell_page);
    logger.add_writer(&logs_page);

    // Register terminal pages
    terminal.add_page(&shell_page);
    terminal.add_page(&logs_page);
    terminal.switch_to(0);

    // Show boot banner
    logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                Config::FIRMWARE_VERSION, HAL::hardware_description());

    // Emulator thread
    PlcIO::start_emulator_thread(&shared_in, &shared_out);

    // Main loop code
    // -----------------------------------------------------------------------

    while (true) {
        // TODO: move to SchedulerContext
        timestamp_ms = (uint32_t)HAL::uptime_ms();

        if (!scheduler.tick()) continue;
        scheduler.paint_stack();

        console_serial.step();
        monitor_serial.step();

        logger.step();
        terminal.step();

        plc_handler.step();
        plc_log_reader.step();

        if (!botanik_running) break;
    }
    return 0;
}
