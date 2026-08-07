// REVIEW_35: TODO
/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a thin harness around a site firmware.
 *
 * Selects the site, applies the sim-only timing dilation, drives loop(), and
 * animates the site's variable tables through its simulated field (sim.h).
 */

#include <csignal>                      // signal / SIGINT

#include "libbotanik.h"                 // the whole library: BotanikFirmware and everything it owns

#include "libbotanik/hal/platform.h"    // HAL::Env::u32

// Site selected at configure time (-DBOTANIK_SITE); libbotanik_site.h derives
// BOTANIK_SITE_HEADER -> its site.h, exposing BotanikSite + the sim-only
// field simulation entry.
#include "libbotanik_site.h"            // resolves BOTANIK_SITE -> BOTANIK_SITE_HEADER
#include BOTANIK_SITE_HEADER            // BotanikSite + Site::simulate

#if BOTANIK_FEATURE_NETWORK
#include "botanik_sim/fieldbus.h"       // the simulated RTU devices on the other end of the wire
#include "botanik_sim/mbcheck.h"        // the Modbus TCP self-check (sim-only scaffolding)
#include "botanik_sim/network.h"        // the in-process mock NTP/MQTT responder (loopback)
#endif

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

    // The console runs at Config::CONSOLE_BAUD (4800), slow and Minitel-like, so
    // the sim exercises the TX pacing at the rate the hardware sees. There is no
    // runtime override: the terminal derives its per-render byte budget from that
    // constant at construction, so anything set later would speed the wire up
    // without letting the renderer fill it. Use BOTANIK_TIME_SCALE to go faster -
    // dilation keeps every byte/event ratio identical to a real 4800-baud run.

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
    // Real-time posture, and why it is optional.
    //
    // Pinning is what makes the scheduler's jitter figures measure the firmware
    // rather than the host - for ONE sim. sched_setaffinity needs no privileges
    // (unlike SCHED_FIFO, which quietly fails without them), so it always takes
    // effect, and several sims started at once ALL land on the same core while
    // the others idle. That is how a screen-test suite running four of them in
    // parallel became load-sensitive. A harness that wants throughput rather
    // than jitter fidelity sets $BOTANIK_NO_RT and lets the OS place them.
    if (HAL::Env::u32("BOTANIK_NO_RT", 0) == 0) {
        Scheduler::lock_process_memory(&greenhouse.logger());
        // Which core is free is a property of the dev box, not of the site, so it
        // comes from the environment like the other host knobs above. Mirrors the
        // main loop running on one core of the OPTA.
        Scheduler::make_thread_realtime((int)HAL::Env::u32("BOTANIK_SIM_CPU", 0),
                                        &greenhouse.logger());
    }

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER
    // The field devices. Learned from the site's own table: every unit id an
    // XP_MODBUS row names is a box that has to answer.
    Sim::FieldBus fieldbus;
    fieldbus.bind(&greenhouse.io());
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT
    // A master pointed at our own slave: it reads a parameter, writes it
    // something else, reads it back and says so. Nothing is mocked - a real
    // socket, the real map, the real Bank::set() - so it is the answer to
    // "does the slave actually serve?", asked every time the sim runs.
    //
    // Off for the screen tests ($BOTANIK_NO_MBCHECK), which is why it is a knob:
    // the check MUTATES a parameter and its traffic reaches the log tail the
    // home page shows, so leaving it on would put a moving target inside a
    // golden. Same reason, same mechanism as $BOTANIK_NO_LOAD_ALARMS.
    const bool mbcheck_on = HAL::Env::u32("BOTANIK_NO_MBCHECK", 0) == 0;
    Sim::ModbusCheck modbus_check(&greenhouse.io(), &greenhouse.logger());
#endif


    // Drive the firmware until a shutdown signal flips the run flag. SIGUSR1
    // (sent by a build after relinking) re-execs at this safe point between
    // iterations.
    while (botanik_running) {
        // The simulated field, in place of the mock coprocessor that used to
        // animate a shared region from its own thread: fill the firmware's data
        // struct, then run the beat that reads it.
        Site::simulate(greenhouse.io(), (uint32_t)HAL::Clock::uptime_ms());
#if BOTANIK_FEATURE_MODBUS_RTU_SERVER
        // Before the beat: the values this instant produced are on the devices
        // by the time the master's transmit half asks for them.
        fieldbus.step(HAL::Clock::uptime_ms());
#endif
        greenhouse.loop();
#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT
        // After the beat: the answers it reads are the ones the beat just
        // published, which is the whole point of the slave's two-phase split.
        if (mbcheck_on) modbus_check.step(HAL::Clock::uptime_ms());
#endif
        if (botanik_reexec) HAL::System::reset();
    }

    // HAL::System::shutdown() runs the reset hook and
    // restores the console. Report 128 + signo for the supervisor.
    HAL::System::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
