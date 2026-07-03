// REVIEW_35: TODO
/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a thin harness around a site firmware.
 *
 * Selects the site, owns the PLC shared region as local statics (on OPTA it
 * comes from HAL::PlcIO::input()/get_plc_output()), applies the sim-only
 * timing dilation, and drives loop(). The mock PLC coprocessor is the site's
 * emulator (sim.h).
 */

#include "libbotanik.h"

// Site selected at configure time (-DBOTANIK_SITE); libbotanik_site.h derives
// BOTANIK_SITE_HEADER -> its site.h, exposing BotanikSite + payload + the
// sim-only mock-PLC entry.
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER

// generic PLC emulator engine (host stand-in for the OPTA coprocessor)
#include "botanik_sim/emulator.h"

#if BOTANIK_FEATURE_NETWORK
// in-process mock NTP/MQTT responder (loopback)
#include "botanik_sim/network.h"
#endif

#include <csignal>
#include <cstring>   // std::strncpy - seeding the mock PLC config block

// The scheduler's stack-usage scan paints the free stack and only works if
// locals live on the real stack. ASAN's use-after-return check (default on)
// relocates locals to a heap "fake stack", so the scan reports a constant
// STACK_SCAN_LIMIT. Turn that one sub-check off for the sim (rest of ASAN+UBSAN
// stay on); OPTA is uninstrumented.
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
extern "C" const char* __asan_default_options() {  // NOLINT(bugprone-reserved-identifier): required ASan API symbol name
    return "detect_stack_use_after_return=0";
}
#endif

// Graceful-shutdown plumbing (process lifecycle is the harness's job, not the
// site's): the handler clears the run flag so the loop breaks into
// HAL::System::shutdown() rather than being killed mid-loop. sig_atomic_t is the
// async-signal-safe flag type.
static volatile sig_atomic_t botanik_running = 1;
static volatile sig_atomic_t botanik_shutdown_signo = 0;

// SIGINT/SIGTERM handler: record the signal, clear the run flag.
// NOLINT: async-signal-safe (assigns only volatile sig_atomic_t).
extern "C" void botanik_sim_shutdown(int signo) {   // NOLINT(bugprone-signal-handler)
    botanik_shutdown_signo = signo;
    botanik_running = 0;
}

// SIGUSR1 handler: request a hot-restart. Only sets a flag; HAL::System::reset() runs
// from the main loop so teardown happens in normal context, not the async
// handler.
// NOLINT: async-signal-safe (assigns only volatile sig_atomic_t).
static volatile sig_atomic_t botanik_reexec = 0;
extern "C" void botanik_sim_reexec(int) {   // NOLINT(bugprone-signal-handler)
    botanik_reexec = 1;
}


int main(int argc, char** argv) {
    using namespace Botanik;

    // Deliberately low (not Config::CONSOLE_BAUD) so the sim exercises the TX
    // pacing at a slow, Minitel-like rate; the OPTA firmware uses the config
    // default.
    static const uint32_t CONSOLE_BAUD = 4800;

    // Stash argv so HAL::System::reset() can re-exec the process with the same args.
    HAL::System::set_reboot_args(argc, argv);

    // Raw terminal before any output; SIGSEGV stays with the HAL handler to
    // restore the terminal on a crash.
    HAL::Serial::setup_raw_console();
    std::signal(SIGINT,  botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGTERM, botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGUSR1, botanik_sim_reexec);     // NOLINT: hot-restart from a rebuilt binary
    // Console/socket writes to a closed reader EPIPE (write() returns 0, byte
    // stays queued) instead of killing us with SIGPIPE. Hit routinely once the
    // console consumer disconnects while we're emitting fast (high dilation).
    std::signal(SIGPIPE, SIG_IGN);                // NOLINT(bugprone-signal-handler)

#if BOTANIK_FEATURE_PLC
    // Shared PLC memory - firmware handler and emulator point at the same
    // region. On OPTA this comes from HAL::PlcIO::input()/get_plc_output().
    static PlcIO::PlcInput<PlcInputData_t>   shared_in  {};
    static PlcIO::PlcOutput<PlcOutputData_t> shared_out {};

    // The PLC config block (network/NTP/MQTT/geo) is published by the emulator
    // (the coprocessor's role on hardware) via fill_greenhouse_config, below.

    BotanikSite greenhouse(&shared_in, &shared_out);
    greenhouse.set_console_baud(CONSOLE_BAUD);

    // Sim-only timing dilation: grow the PLC link + HW-watchdog timeouts by the
    // time-scale factor so jitter under a high BOTANIK_TIME_SCALE can't
    // false-trip them.
    greenhouse.plc_handler().set_heartbeat_ms(plc_dilated_timeout(Config::PLC_HEARTBEAT_MS));
    greenhouse.plc_handler().set_timeout_ms  (plc_dilated_timeout(Config::PLC_TIMEOUT_MS));
    greenhouse.plc_handler().set_watchdog_ms (plc_dilated_timeout(Config::PLC_WATCHDOG_MS));
    greenhouse.set_hw_watchdog_ms(plc_dilated_timeout(Config::HW_WATCHDOG_MS));
#else
    // No PLC: a single-core firmware with no shared region or mock coprocessor.
    BotanikSite greenhouse;
    greenhouse.set_console_baud(CONSOLE_BAUD);
    greenhouse.set_hw_watchdog_ms(plc_dilated_timeout(Config::HW_WATCHDOG_MS));
#endif

    greenhouse.setup();

#if BOTANIK_FEATURE_NETWORK
    // Mock NTP/MQTT responder on loopback. Spawned before we lock process memory
    // (mlockall MCL_FUTURE), like any other thread we create.
    Network::start_mock_netd(&greenhouse.logger());
#endif

#if BOTANIK_FEATURE_PLC
    // Mock PLC coprocessor: spawn the emulator thread animating the greenhouse
    // payload (on OPTA the real coprocessor plays this role). Spawn before
    // locking memory: lock_process_memory() arms mlockall(MCL_FUTURE), which
    // locks every later mmap including a new thread's stack, so spawning
    // afterwards trips RLIMIT_MEMLOCK with EAGAIN (silently, in lean RELEASE
    // builds). Must run after setup()'s allocations.
    PlcIO::start_emulator_thread(&shared_in, &shared_out,
                                 &fill_greenhouse_output,
                                 &fill_greenhouse_config,
                                 &greenhouse.logger());
    // Lock memory (once), then make the main loop realtime on its own core -
    // separate from the emulator's, mirroring the OPTA's two cores.
    Scheduler::lock_process_memory(&greenhouse.logger());
    Scheduler::make_thread_realtime(Config::SIM_MAIN_CPU_CORE,
                                    &greenhouse.logger());
#endif

    // Drive the firmware until a shutdown signal flips the run flag. SIGUSR1
    // (sent by a build after relinking) re-execs at this safe point between
    // iterations.
    while (botanik_running) {
        greenhouse.loop();
        if (botanik_reexec) HAL::System::reset();
    }

    // HAL::System::shutdown() runs the reset hook (joins the emulator thread) and
    // restores the console. Report 128 + signo for the supervisor.
    HAL::System::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
