/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a generic harness around a site.
 *
 * The equivalent of an Arduino sketch: it wires the library components together
 * around the gh_mlb41 site (payload + pages + alert table, under src/sites/). On
 * OPTA this plumbing lives in the .ino sketch and the PLC shared memory comes from
 * HAL::get_plc_input()/get_plc_output(); here the simulator owns it as local
 * statics and animates it from the site's mock PLC (sites/gh_mlb41/sim.cpp).
 */

#include "libbotanik.h"
#include "libbotanik_asserts.h"      // BOTANIK_ASSERT + set_assert_logger
#include "libbotanik/alerts/alerts.h"
#include "libbotanik/alerts/notifier.h"
#include "libbotanik/alerts/reader.h"

#include "sites/gh_mlb41/payload.h"
#include "sites/gh_mlb41/pages.h"
#include "sites/gh_mlb41/alerts.h"
#include "sites/gh_mlb41/sim.h"

#include <csignal>   // std::signal

// The scheduler's stack-usage scan paints the free stack and binary-searches it
// for the high-water mark, which only works if locals live on the real stack.
// ASAN's use-after-return check (default on) relocates locals to a heap "fake
// stack", so the painted sp_marker lands off the real stack and the scan reports
// a constant STACK_SCAN_LIMIT. Turn that one sub-check off for the sim process
// (the rest of ASAN + UBSAN stay on); OPTA runs uninstrumented and is unaffected.
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
extern "C" const char* __asan_default_options() {
    return "detect_stack_use_after_return=0";
}
#endif

// Graceful-shutdown plumbing for the simulator process. Lives in the entry point
// (process lifecycle is the harness's job, never the site's): the run flag is
// cleared by the handler so the main loop breaks into HAL::shutdown() rather than
// being killed mid-loop. sig_atomic_t is the async-signal-safe flag type.
static volatile sig_atomic_t botanik_running = 1;
static volatile sig_atomic_t botanik_shutdown_signo = 0;

// SIGINT/SIGTERM handler: records the signal and clears the run flag. extern "C"
// so it can be installed with std::signal.
// NOLINT: async-signal-safe (assigns only volatile sig_atomic_t).
extern "C" void botanik_sim_shutdown(int signo) {   // NOLINT(bugprone-signal-handler)
    botanik_shutdown_signo = signo;
    botanik_running = 0;
}

int main(int argc, char** argv) {
    using namespace Botanik;

    // Setup code
    // -----------------------------------------------------------------------

    // Stash argv so HAL::reset() can re-exec the process with the same arguments.
    HAL::set_reboot_args(argc, argv);

    // Raw terminal + console serial. One baud, used for the UART and to size the
    // terminal's cooperative-TX depth gates (see AnsiTerminal).
    static const uint32_t CONSOLE_BAUD = 4800;
    HAL::setup_raw_console();
    HAL::get_serial(HAL::CONSOLE)->begin(CONSOLE_BAUD);

    // Graceful shutdown on Ctrl+C / SIGTERM: flip the run flag so the main loop
    // breaks into HAL::shutdown() (same exit path as the "exit" command). SIGSEGV
    // stays with the HAL handler to restore the terminal on a crash.
    std::signal(SIGINT,  botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGTERM, botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)

    // Shared PLC memory - both handler and emulator point at the same region.
    // On OPTA: PlcIO::PlcInput<PlcInputData_t>* plcin =
    //              static_cast<PlcIO::PlcInput<PlcInputData_t>*>(HAL::get_plc_input());
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    // Serial I/O
    Utils::BufferedSerial console_serial(HAL::get_serial(HAL::CONSOLE));
    Utils::BufferedSerial monitor_serial(HAL::get_serial(HAL::MONITOR));

    // Logging. The monitor sink drops output when stderr is a terminal, so the
    // diagnostic log can't garble the console UI; redirect stderr to capture it.
    Logs::Logger           logger;
    Logs::MonitorLogWriter monitor(monitor_serial);
    set_assert_logger(&logger);   // route BOTANIK_ASSERT failures to the log

    // Scheduler (constructed before the PLC layer and terminal so they can share
    // its published loop timestamp via the pointer below). It also measures and
    // publishes loop performance (CPU load, jitter, stack usage) on its context.
    Scheduler::LoopScheduler scheduler(Config::TASK_PERIOD_MS, &logger);
    uint32_t* timestamp_ms = &scheduler.context()->timestamp_ms;

    // PLC sync layer (handler + log reader on main thread)
    using SyncHandler = PlcIO::PlcSyncHandler<PlcInputData_t, PlcOutputData_t>;
    SyncHandler         plc_handler(&shared_in, &shared_out, timestamp_ms, &logger);
    // Dilate the link timeouts, mirrored by the emulator side in its thread.
    plc_handler.set_heartbeat_ms(plc_dilated_timeout(Config::PLC_HEARTBEAT_MS));
    plc_handler.set_timeout_ms  (plc_dilated_timeout(Config::PLC_TIMEOUT_MS));
    plc_handler.set_watchdog_ms (plc_dilated_timeout(Config::PLC_WATCHDOG_MS));
    PlcIO::PlcLogReader plc_log_reader(plc_handler.context(), &logger);

    // Alert model: merges the PLC alert bitset with local runtime alerts, raises
    // ALERT-topic logs + (draft) MQTT notifications, and feeds the terminal alert
    // bar / alerts page. The notifier is draft plumbing (no transport wired yet).
    Alerts::MqttAlertNotifier alert_mqtt("botanik/greenhouse");
    Alerts::AlertModel        alert_model(g_alerts, Alerts::ALERT_BITS, &logger, &alert_mqtt);
    const PlcOutputData_t*    plc_out = plc_handler.output_data();
    Alerts::AlertReader<PlcOutputData_t> alert_reader(&alert_model, plc_out);

    // Terminal UI - bundle the subsystem status contexts the terminal reads.
    SystemContext sys_ctx(scheduler.context(), plc_handler.context());

    // Initialize terminal and pages. The terminal takes the alert model so it can
    // render the always-visible bottom alert bar (and reserve its row) on every page.
    Terminal::AnsiTerminal terminal(console_serial, 80, 24, timestamp_ms, CONSOLE_BAUD,
                                    sys_ctx, &alert_model);
    Pages::ShellPage shell_page(terminal);
    Pages::LogsPage  logs_page(terminal);
    GreenhouseHomePage   home_page(terminal, plc_handler.output_data());
    GreenhouseStatusPage status_page(terminal, plc_handler.output_data());
    Pages::AlertsPage    alerts_page(terminal, &alert_model);

    // Register logging sinks
    logger.add_writer(&monitor);
    logger.add_writer(&shell_page);
    logger.add_writer(&logs_page);
    logger.add_writer(&home_page);
    // Report the console's TX-full drops (shell output that didn't fit the ring).
    logger.watch_tx(&console_serial);

    // Emulator thread (drives the mock greenhouse payload). Also locks process
    // memory and makes the main loop + emulator realtime on separate cores; see
    // start_greenhouse_emulator for the start-order constraint.
    start_greenhouse_emulator(&shared_in, &shared_out, &logger);

    // Register terminal pages
    terminal.add_page(&home_page);
    terminal.add_page(&shell_page);
    terminal.add_page(&logs_page);
    terminal.add_page(&status_page);
    terminal.add_page(&alerts_page);

    // Select the terminal type before the first render. Swap to setup_minitel()
    // when driving a real Minitel 1B+/2 over the DIN serial port.
    terminal.setup_vt102();
    // Both targets boot on the home page (no page-index persistence: a reset comes
    // back default, matching hardware where RAM is cleared).
    terminal.switch_to(0);

    // Show boot banner
    logger.info(PlcIO::BOOT, "Botanik v%s on %s",
                Config::FIRMWARE_VERSION, HAL::hardware_description());

    // Arm the hardware watchdog. The main loop kicks it every healthy tick; once
    // the PLC link faults we stop kicking so the board reboots HW_WATCHDOG_MS later.
    HAL::watchdog_start(plc_dilated_timeout(Config::HW_WATCHDOG_MS));

    // Main loop code
    // -----------------------------------------------------------------------

    // Per-tick cycle contract (the OPTA firmware loop mirrors this order; see
    // docs/CONCURRENCY.md). The order is load-bearing - inputs are ingested before
    // processing, and the TX rings are flushed only after everything has queued its
    // output for the beat:
    //   tick -> receive -> plc_handler -> plc_log_reader -> logger -> terminal
    //        -> monitor.drain -> transmit -> watchdog_kick
    while (true) {
        // tick() republishes the loop "now" when it releases a beat (so the steps
        // below share one timestamp) and paints the free stack for the usage scan.
        if (!scheduler.tick()) continue;

        console_serial.receive();
        monitor_serial.receive();

        plc_handler.step();
        plc_log_reader.step();

        // Read + analyze alerts after the PLC payload is fresh and before the
        // logger drains, so any alarm START/END lands this beat. SYS_RST is a
        // local alert, asserted for the first five minutes after boot.
        uint64_t now_ms = HAL::uptime_ms();
        alert_model.set_local(Alerts::SYS_RST, now_ms < 300000);
        alert_reader.step(now_ms);

        logger.step();

        terminal.step();
        monitor.drain();

        console_serial.transmit();
        monitor_serial.transmit();

        // only kick the watchdog if the PLC is not faulted
        if (!plc_handler.context()->faulted()) HAL::watchdog_kick();

        if (!botanik_running) break;
    }

    // Reached only via the run flag (SIGINT/SIGTERM). HAL::shutdown() runs the
    // reset hook (joins the emulator thread) and exits through atexit, restoring
    // the console. Report 128 + signo so a supervisor sees the signal-driven exit.
    HAL::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
