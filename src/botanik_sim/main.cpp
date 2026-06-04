// Botanik simulator entry point.
//
// This file is the equivalent of an Arduino sketch: it defines the site-specific
// PLC payload types and wires the library components together. On OPTA, this
// plumbing lives in the .ino sketch; here it lives in main().
//
// The PLC shared memory is allocated as local statics (simulator owns it). On OPTA,
// typed pointers are obtained via HAL::get_plc_input() / HAL::get_plc_output() and
// validated against the PLC IDE's generated types with a static_assert.

#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "libbotanik.h"
#include "libbotanik/pages/shell.h"
#include "libbotanik/pages/logs.h"

// ---------------------------------------------------------------------------
// Site-specific PLC payload types.
// On OPTA: define these in the sketch to match the PLC IDE's struct layout,
// then add:
//   static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(PlcIO::PlcInput<PlcInputData_t>));
//   static_assert(alignof(PLCSharedVarsOutput_t) == alignof(PlcIO::PlcOutput<PlcOutputData_t>));
// ---------------------------------------------------------------------------
struct PlcInputData_t  {};   // no site-specific data beyond the library log fields
struct PlcOutputData_t {};   // no site-specific data beyond the library log fields

// ---------------------------------------------------------------------------
// Emulator thread — simulates the PLC coprocessor running independently.
// Owns its own timestamp, PlcEmulator, and PlcLogWriter.
// ---------------------------------------------------------------------------
struct EmulatorArgs {
    Botanik::PlcIO::PlcInput<PlcInputData_t>*   shared_in;
    Botanik::PlcIO::PlcOutput<PlcOutputData_t>* shared_out;
};

static void* emulator_thread(void* arg) {
    using namespace Botanik;
    EmulatorArgs* args = static_cast<EmulatorArgs*>(arg);

    uint32_t emu_timestamp_ms = 0;
    PlcIO::PlcEmulator<PlcInputData_t, PlcOutputData_t> plc_emulator(
        args->shared_in, args->shared_out, &emu_timestamp_ms);
    PlcIO::PlcLogWriter plc_log_writer(plc_emulator.context(), &emu_timestamp_ms);

    uint32_t emu_tick = 0;
    while (true) {
        emu_timestamp_ms = (uint32_t)HAL::uptime_ms();
        plc_emulator.step();
        // Emit a periodic heartbeat log, simulating the PLC coprocessor.
        if ((++emu_tick % 500) == 0) {
            char buf[40];
            snprintf(buf, sizeof(buf), "periodic log #%lu", (unsigned long)(emu_tick / 500));
            plc_log_writer.push_log(PlcIO::LOG_INFO, PlcIO::PLC_IO, buf);
        }
        usleep(Config::TASK_PERIOD_MS * 1000);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Terminal raw-mode setup/restore.
// ---------------------------------------------------------------------------
static struct termios _saved_termios;
static void _restore_termios() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &_saved_termios); }
static void _signal_handler(int) { _restore_termios(); exit(0); }

int main() {
    using namespace Botanik;

    // Raw terminal: disable line buffering and echo, non-blocking reads.
    tcgetattr(STDIN_FILENO, &_saved_termios);
    atexit(_restore_termios);
    signal(SIGINT,  _signal_handler);
    signal(SIGTERM, _signal_handler);
    signal(SIGSEGV, _signal_handler);
    struct termios raw = _saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    HAL::get_serial(HAL::CONSOLE)->begin(900);

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
    Logs::Logger          logger;
    Logs::SerialLogWriter monitor(monitor_serial);
    logger.add_writer(&monitor);

    // Scheduler (constructed before terminal so we can pass its context)
    Scheduler::LoopScheduler scheduler(Config::TASK_PERIOD_MS, &logger);

    // PLC sync layer (handler + log reader on main thread)
    PlcIO::PlcSyncHandler<PlcInputData_t, PlcOutputData_t> plc_handler(
        &shared_in, &shared_out, &timestamp_ms, &logger);
    PlcIO::PlcLogReader plc_log_reader(plc_handler.context(), &logger);

    // Terminal UI
    Terminal::AnsiTerminal terminal(console_serial, 80, 24, &timestamp_ms, &logger,
                                    scheduler.context(), plc_handler.context());

    Pages::ShellPage shell_page(terminal);
    Pages::LogsPage  logs_page(terminal);
    terminal.add_page(&shell_page);
    terminal.add_page(&logs_page);
    logger.add_writer(&shell_page);
    logger.add_writer(&logs_page);
    terminal.switch_to(0);

    // Emulator thread (PlcEmulator + PlcLogWriter, simulates independent PLC coprocessor)
    EmulatorArgs emu_args { &shared_in, &shared_out };
    pthread_t emu_thread;
    pthread_create(&emu_thread, nullptr, emulator_thread, &emu_args);
    pthread_detach(emu_thread);

    while (true) {
        timestamp_ms = (uint32_t)HAL::uptime_ms();

        if (!scheduler.tick()) continue;
        scheduler.paint_stack();

        console_serial.step();
        monitor_serial.step();

        logger.step();
        terminal.step();

        plc_handler.step();
        plc_log_reader.step();
    }
}
