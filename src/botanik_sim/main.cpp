// REVIEW_35: TODO
/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a thin harness around a site firmware.
 *
 * Selects the site, applies the sim-only timing dilation, drives loop(), and
 * animates the site's variable tables through its simulated field (sim.h).
 */

#include "libbotanik.h"

// Site selected at configure time (-DBOTANIK_SITE); libbotanik_site.h derives
// BOTANIK_SITE_HEADER -> its site.h, exposing BotanikSite + payload + the
// sim-only mock-PLC entry.
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER


#if BOTANIK_FEATURE_NETWORK
// in-process mock NTP/MQTT responder (loopback)
#include "botanik_sim/network.h"
#endif

#include "libbotanik/hal/platform.h"   // HAL::Env::u32 (BOTANIK_CONSOLE_BAUD)

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
    // default. BOTANIK_CONSOLE_BAUD overrides it for smoketesting - it drives
    // both the sim UART's write throttle and present()'s per-tick byte budget,
    // so raising it makes the console feel fast and lowering it makes the pacing
    // easy to watch. Note this trades away the property BOTANIK_TIME_SCALE
    // preserves: dilation keeps every byte/event ratio identical to a 4800-baud
    // run, a different baud does not.
    // 0 is rejected rather than honoured: it switches the sim UART's throttle off
    // while pinning present()'s budget at its 8-byte floor, which reads as "the
    // console is broken" rather than "the console is fast".
    uint32_t console_baud = HAL::Env::u32("BOTANIK_CONSOLE_BAUD", 4800);
    if (console_baud == 0) console_baud = 4800;
    const uint32_t CONSOLE_BAUD = console_baud;

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

    BotanikSite greenhouse;
    greenhouse.set_console_baud(CONSOLE_BAUD);
    greenhouse.set_hw_watchdog_ms(dilated_timeout(Config::HW_WATCHDOG_MS));

    greenhouse.setup();

#if BOTANIK_FEATURE_NETWORK
    // Mock NTP/MQTT responder on loopback. Spawned before we lock process memory
    // (mlockall MCL_FUTURE), like any other thread we create.
    Network::start_mock_netd(&greenhouse.logger());
#endif

    // Real-time posture for the beat: pages locked in RAM and the loop thread on
    // SCHED_FIFO, pinned to its own core, so the scheduler's jitter figures
    // measure the firmware rather than the host's scheduler. Both warn and carry
    // on without privileges (BOTANIK_NO_RT_WARNING silences that).
    Scheduler::lock_process_memory(&greenhouse.logger());
    Scheduler::make_thread_realtime(Config::SIM_MAIN_CPU_CORE, &greenhouse.logger());


    // Drive the firmware until a shutdown signal flips the run flag. SIGUSR1
    // (sent by a build after relinking) re-execs at this safe point between
    // iterations.
    while (botanik_running) {
        // The simulated field, in place of the mock coprocessor that used to
        // animate a shared region from its own thread: fill the firmware's data
        // struct, then run the beat that reads it.
        Site::simulate(greenhouse.io(), (uint32_t)HAL::Clock::uptime_ms());
        greenhouse.loop();
        if (botanik_reexec) HAL::System::reset();
    }

    // HAL::System::shutdown() runs the reset hook and
    // restores the console. Report 128 + signo for the supervisor.
    HAL::System::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
