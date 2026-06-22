/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a thin harness around a site firmware.
 *
 * Selects the site, owns the PLC shared region as local statics (on OPTA it comes from
 * HAL::get_plc_input()/get_plc_output()), applies the sim-only timing dilation, and
 * drives loop(). The mock PLC coprocessor is the site's emulator (sim.h).
 */

#include "libbotanik.h"

// Site selected at configure time (-DBOTANIK_SITE); CMake points BOTANIK_SITE_HEADER at
// its site.h, exposing BotanikSite + payload + the sim-only mock-PLC entry.
#include BOTANIK_SITE_HEADER

#include <csignal>
#include <cstring>   // std::strncpy - seeding the mock PLC config block

// The scheduler's stack-usage scan paints the free stack and only works if locals live
// on the real stack. ASAN's use-after-return check (default on) relocates locals to a
// heap "fake stack", so the scan reports a constant STACK_SCAN_LIMIT. Turn that one
// sub-check off for the sim (rest of ASAN+UBSAN stay on); OPTA is uninstrumented.
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
extern "C" const char* __asan_default_options() {  // NOLINT(bugprone-reserved-identifier): required ASan API symbol name
    return "detect_stack_use_after_return=0";
}
#endif

// Graceful-shutdown plumbing (process lifecycle is the harness's job, not the site's):
// the handler clears the run flag so the loop breaks into HAL::shutdown() rather than
// being killed mid-loop. sig_atomic_t is the async-signal-safe flag type.
static volatile sig_atomic_t botanik_running = 1;
static volatile sig_atomic_t botanik_shutdown_signo = 0;

// SIGINT/SIGTERM handler: record the signal, clear the run flag.
// NOLINT: async-signal-safe (assigns only volatile sig_atomic_t).
extern "C" void botanik_sim_shutdown(int signo) {   // NOLINT(bugprone-signal-handler)
    botanik_shutdown_signo = signo;
    botanik_running = 0;
}

// SIGUSR1 handler: request a hot-restart. Only sets a flag; HAL::reset() runs from the
// main loop so teardown happens in normal context, not the async handler.
// NOLINT: async-signal-safe (assigns only volatile sig_atomic_t).
static volatile sig_atomic_t botanik_reexec = 0;
extern "C" void botanik_sim_reexec(int) {   // NOLINT(bugprone-signal-handler)
    botanik_reexec = 1;
}

#if BOTANIK_FEATURE_PLC
// Copy a hostname / dotted-IPv4 literal into a PLC config address buffer (sim stands in
// for the coprocessor that publishes these). Always NUL-terminates within the buffer.
static void sim_set_host(Botanik::PlcIO::HostStr_t dst, const char* s) {
    std::strncpy(dst, s, Botanik::Config::PLC_HOST_STR_LEN - 1);
    dst[Botanik::Config::PLC_HOST_STR_LEN - 1] = '\0';
}
#endif

int main(int argc, char** argv) {
    using namespace Botanik;

    // Deliberately low (not Config::CONSOLE_BAUD) so the sim exercises the TX pacing at
    // a slow, Minitel-like rate; the OPTA firmware uses the config default.
    static const uint32_t CONSOLE_BAUD = 4800;

    // Stash argv so HAL::reset() can re-exec the process with the same arguments.
    HAL::set_reboot_args(argc, argv);

    // Raw terminal before any output; SIGSEGV stays with the HAL handler to restore the
    // terminal on a crash.
    HAL::setup_raw_console();
    std::signal(SIGINT,  botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGTERM, botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGUSR1, botanik_sim_reexec);     // NOLINT: hot-restart from a rebuilt binary

#if BOTANIK_FEATURE_PLC
    // Shared PLC memory - firmware handler and emulator point at the same region. On
    // OPTA this comes from HAL::get_plc_input()/get_plc_output().
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    // Mock PLC config block (on hardware the coprocessor publishes these). Per-block
    // serials start at 1 so a consumer latched at 0 applies them once. See plcio/config.h.
    {
        PlcIO::PlcOutputConfig_t& cfg = shared_out.conf;
        cfg.net.serial  = 1; cfg.net.dhcp = 1;            // DHCP; static fields stay empty
        sim_set_host(cfg.net.dns1, "192.168.1.1");
        cfg.ntp.serial  = 1; cfg.ntp.enabled = 1; cfg.ntp.poll_s = 64;
        sim_set_host(cfg.ntp.server1, "pool.ntp.org");
        cfg.mqtt.serial = 1; cfg.mqtt.enabled = 1; cfg.mqtt.port = 1883; cfg.mqtt.keepalive_s = 60;
        sim_set_host(cfg.mqtt.broker, "test.mosquitto.org");
        cfg.geo.serial = 1;                               // example site: Paris (E7 degrees)
        cfg.geo.latitude_e7  =  488566000;                //  48.8566 N
        cfg.geo.longitude_e7 =   23522000;                //   2.3522 E
    }

    BotanikSite greenhouse(&shared_in, &shared_out, CONSOLE_BAUD, /*cols=*/80, /*rows=*/24);

    // Sim-only timing dilation: grow the PLC link + HW-watchdog timeouts by the time-
    // scale factor so jitter under a high BOTANIK_TIME_SCALE can't false-trip them.
    greenhouse.plc_handler().set_heartbeat_ms(plc_dilated_timeout(Config::PLC_HEARTBEAT_MS));
    greenhouse.plc_handler().set_timeout_ms  (plc_dilated_timeout(Config::PLC_TIMEOUT_MS));
    greenhouse.plc_handler().set_watchdog_ms (plc_dilated_timeout(Config::PLC_WATCHDOG_MS));
    greenhouse.set_hw_watchdog_ms(plc_dilated_timeout(Config::HW_WATCHDOG_MS));
#else
    // No PLC: a single-core firmware with no shared region or mock coprocessor.
    BotanikSite greenhouse(CONSOLE_BAUD, /*cols=*/80, /*rows=*/24);
    greenhouse.set_hw_watchdog_ms(plc_dilated_timeout(Config::HW_WATCHDOG_MS));
#endif

    greenhouse.setup();

#if BOTANIK_FEATURE_PLC
    // Mock PLC coprocessor: locks process memory and makes the loop realtime, so it
    // must run after setup()'s allocations (see start_site_emulator for the order).
    start_site_emulator(&shared_in, &shared_out, &greenhouse.logger());
#endif

    // Drive the firmware until a shutdown signal flips the run flag. SIGUSR1 (sent by a
    // build after relinking) re-execs at this safe point between iterations.
    while (botanik_running) {
        greenhouse.loop();
        if (botanik_reexec) HAL::reset();
    }

    // HAL::shutdown() runs the reset hook (joins the emulator thread) and restores the
    // console. Report 128 + signo for the supervisor.
    HAL::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
