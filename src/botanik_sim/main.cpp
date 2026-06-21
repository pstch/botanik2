/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a thin harness around a site firmware.
 *
 * Selects the gh_mlb41 site, owns the PLC shared region as local statics (on OPTA
 * it comes from HAL::get_plc_input()/get_plc_output()), applies the simulator-only
 * timing dilation, and drives the firmware's loop(). The mock PLC coprocessor is
 * the site's emulator (sites/gh_mlb41/sim.h); everything else lives in the
 * BotanikFirmware base and the GreenhouseMLB41 subclass.
 */

#include "libbotanik.h"

// The site is selected at configure time (-DBOTANIK_SITE); CMake points
// BOTANIK_SITE_HEADER at its site.h, which exposes BotanikSite + the payload +
// the sim-only mock-PLC entry (start_site_emulator / plc_dilated_timeout).
#include BOTANIK_SITE_HEADER

#include <csignal>   // std::signal
#include <cstring>   // std::strncpy - seeding the mock PLC config block

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

// Copy a hostname / dotted-IPv4 literal into a PLC config address buffer (sim only:
// the mock PLC stands in for the coprocessor that publishes these). Always NUL-
// terminates within Config::PLC_HOST_STR_LEN.
static void sim_set_host(Botanik::PlcIO::HostStr_t dst, const char* s) {
    std::strncpy(dst, s, Botanik::Config::PLC_HOST_STR_LEN - 1);
    dst[Botanik::Config::PLC_HOST_STR_LEN - 1] = '\0';
}

int main(int argc, char** argv) {
    using namespace Botanik;

    // Console baud: drives the UART and sizes the terminal's cooperative-TX gates.
    // Deliberately low (not Config::CONSOLE_BAUD) so the sim exercises the TX pacing at
    // a slow, Minitel-like rate; the OPTA firmware uses the config default.
    static const uint32_t CONSOLE_BAUD = 4800;

    // Stash argv so HAL::reset() can re-exec the process with the same arguments.
    HAL::set_reboot_args(argc, argv);

    // Raw terminal before any output. Graceful shutdown on Ctrl+C / SIGTERM: flip
    // the run flag so the loop breaks into HAL::shutdown() (same path as "exit").
    // SIGSEGV stays with the HAL handler to restore the terminal on a crash.
    HAL::setup_raw_console();
    std::signal(SIGINT,  botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGTERM, botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)

    // Shared PLC memory - both the firmware's handler and the emulator point at the
    // same region. On OPTA this comes from HAL::get_plc_input()/get_plc_output().
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    // Mock PLC config block: on hardware the coprocessor publishes the network / NTP /
    // MQTT settings; in the sim we seed the shared region once so the future
    // components have realistic config to act on. Per-block serials start at 1 so a
    // consumer latched at 0 applies them once. See plcio/config.h.
    {
        PlcIO::PlcOutputConfig_t& cfg = shared_out.config;
        cfg.net.serial  = 1; cfg.net.dhcp = 1;            // DHCP; static fields stay empty
        sim_set_host(cfg.net.dns1, "192.168.1.1");
        cfg.ntp.serial  = 1; cfg.ntp.enabled = 1; cfg.ntp.poll_s = 64;
        sim_set_host(cfg.ntp.server1, "pool.ntp.org");
        cfg.mqtt.serial = 1; cfg.mqtt.enabled = 1; cfg.mqtt.port = 1883; cfg.mqtt.keepalive_s = 60;
        sim_set_host(cfg.mqtt.broker, "test.mosquitto.org");
        cfg.geo.serial = 1;                               // example site: Paris, UTC+1
        cfg.geo.latitude_e7  =  488566000;                //  48.8566 N
        cfg.geo.longitude_e7 =   23522000;                //   2.3522 E
        cfg.geo.tz_offset_min = 60;
    }

    // The selected site firmware: wires the library around its payload/pages/alarms.
    BotanikSite greenhouse(&shared_in, &shared_out, CONSOLE_BAUD, /*cols=*/80, /*rows=*/24);

    // Simulator-only timing dilation: grow the PLC link + HW-watchdog timeouts by
    // the time-scale factor so jitter under a high BOTANIK_TIME_SCALE can't false-
    // trip them. The emulator mirrors the link dilation in its own thread.
    greenhouse.plc_handler().set_heartbeat_ms(plc_dilated_timeout(Config::PLC_HEARTBEAT_MS));
    greenhouse.plc_handler().set_timeout_ms  (plc_dilated_timeout(Config::PLC_TIMEOUT_MS));
    greenhouse.plc_handler().set_watchdog_ms (plc_dilated_timeout(Config::PLC_WATCHDOG_MS));
    greenhouse.set_hw_watchdog_ms(plc_dilated_timeout(Config::HW_WATCHDOG_MS));

    greenhouse.setup();

    // Mock PLC coprocessor: spawns the emulator thread, then locks process memory
    // and makes the main loop realtime - so it must run after setup()'s allocations
    // (see start_site_emulator for the start-order constraint).
    start_site_emulator(&shared_in, &shared_out, &greenhouse.logger());

    // Main loop: drive the firmware until a shutdown signal flips the run flag.
    while (botanik_running) greenhouse.loop();

    // HAL::shutdown() runs the reset hook (joins the emulator thread) and exits
    // through atexit, restoring the console. Report 128 + signo for the supervisor.
    HAL::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
