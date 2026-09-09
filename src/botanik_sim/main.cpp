/**
 * @file main.cpp
 * @brief Botanik simulator entry point - a thin harness around a site firmware.
 *
 * Selects the site, drives loop(), and
 * animates the site's variable tables through its simulated field (sim.h).
 */

#include <csignal>

#include "libbotanik.h"

#include "libbotanik/hal/platform.h"

// Site selected at configure time (-DBOTANIK_SITE).
#include "libbotanik_site.h"
#include BOTANIK_SITE_HEADER            // Site::firmware() + Site::simulate

#if BOTANIK_FEATURE_NETWORK
#include "botanik_sim/fieldbus.h"
#include "botanik_sim/mbcheck.h"
#include "botanik_sim/network.h"
#endif


#include "botanik_sim/clockctl.h"

// The scheduler's stack-usage scan paints the free stack, so it needs locals on
// the real stack. ASan's use-after-return check relocates them to a heap "fake
// stack" and the scan then reports a constant STACK_SCAN_LIMIT, so turn that one
// sub-check off; the rest of ASan+UBSan stay on.
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
extern "C" const char* __asan_default_options() {  // NOLINT(bugprone-reserved-identifier): required ASan API symbol name
    return "detect_stack_use_after_return=0";
}
#endif

// Graceful shutdown: the handler clears the run flag so the loop breaks into
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

// SIGUSR1 handler: request a hot-restart. Only sets a flag; HAL::System::reset()
// runs from the main loop, so teardown happens in normal context.
// NOLINT: async-signal-safe (assigns only volatile sig_atomic_t).
static volatile sig_atomic_t botanik_reexec = 0;
extern "C" void botanik_sim_reexec(int) {   // NOLINT(bugprone-signal-handler)
    botanik_reexec = 1;
}


int main(int argc, char** argv) {
    using namespace Botanik;

    // The console runs at `console_baud` (4800 by default), so the sim exercises the
    // TX pacing at the rate the hardware sees. No runtime override: the terminal
    // derives its per-render byte budget from that constant at construction, so
    // a later change would speed the wire up without letting the renderer fill it.

    // Stash argv so HAL::System::reset() can re-exec the process with the same args.
    HAL::System::set_reboot_args(argc, argv);

    // Raw terminal before any output; SIGSEGV stays with the HAL handler to
    // restore the terminal on a crash.
    HAL::Serial::setup_raw_console();
    std::signal(SIGINT,  botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGTERM, botanik_sim_shutdown);   // NOLINT(bugprone-signal-handler)
    std::signal(SIGUSR1, botanik_sim_reexec);     // NOLINT: hot-restart from a rebuilt binary
    // A write to a closed reader EPIPEs (write() returns 0, the byte stays
    // queued) instead of killing us with SIGPIPE, which the console consumer
    // disconnecting mid-emit would otherwise do routinely.
    std::signal(SIGPIPE, SIG_IGN);                // NOLINT(bugprone-signal-handler)

    // The site owns the object: a function-local static, so its lifetime rule
    // is stated once rather than in every harness.
    Botanik::BotanikFirmware& greenhouse = Site::firmware();
    greenhouse.set_hw_watchdog_ms(Config::HW_WATCHDOG_MS);

    greenhouse.setup();

#if BOTANIK_FEATURE_NETWORK
    // Mock NTP/MQTT responder on loopback. Binds here so the firmware's first
    // query has somewhere to land; the beat loop below services it.
    Network::start_mock_netd();
#endif

    // Real-time posture for the beat: pages locked in RAM and the loop thread on
    // SCHED_FIFO, pinned to its own core, so the scheduler's jitter figures
    // measure the firmware rather than the host. Both warn and carry on without
    // privileges (BOTANIK_NO_RT_WARNING silences that).
    //
    // It holds for ONE sim: sched_setaffinity needs no privileges, so several
    // sims all land on the same core while the others idle. A harness that wants
    // throughput rather than jitter fidelity sets $BOTANIK_NO_RT.
    if (HAL::Env::u32("BOTANIK_NO_RT", 0) == 0) {
        Scheduler::lock_process_memory();
        // Which core is free is a property of the dev box, not of the site, so
        // it comes from the environment like the other host knobs above.
        Scheduler::make_thread_realtime((int)HAL::Env::u32("BOTANIK_SIM_CPU", 0));
    }

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER
    // The field devices, learned from the site's own table: every unit id an
    // field row names is a box that has to answer.
    Sim::FieldBus fieldbus;
    fieldbus.bind(&greenhouse.io());
    // The animator's readings go into the DEVICES from here, not into the bank
    // rows - so a row shows what the bus delivered, at the rate it delivered it.
    Site::field_ctx()  = &fieldbus;
    Site::field_sink() = [](void* ctx, Vars::Var h, float v) {
        static_cast<Sim::FieldBus*>(ctx)->set_physical(h, v);
    };
    // ...and the same reach for taking a device off the wire, which is the only
    // way to simulate one that has died: a device left holding its last
    // register answers every poll, and the row stays valid.
    Site::field_fault() = [](void* ctx, uint8_t unit, bool absent) {
        static_cast<Sim::FieldBus*>(ctx)->set_fault(
            unit, absent ? Sim::FieldBus::FAULT_ABSENT
                         : Sim::FieldBus::FAULT_NONE);
    };
    if (fieldbus.dropped() != 0) {
        // Fatal, not a warning: a sim missing devices reports them as
        // unanswering, sending the operator to debug a bus that is fine.
        std::fprintf(stderr,
                     "sim: FieldBus holds %u of %u devices - %u dropped. "
                     "Raise MAX_DEVICES in fieldbus.h.\n",
                     (unsigned)fieldbus.device_count(),
                     (unsigned)(fieldbus.device_count() + fieldbus.dropped()),
                     (unsigned)fieldbus.dropped());
        return 1;
    }
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT
    // A master pointed at our own slave: it reads a parameter, writes it
    // something else, reads it back and says so, over a real socket and the real
    // map. Off for the screen tests ($BOTANIK_NO_MBCHECK), since it MUTATES a
    // parameter and its traffic reaches the log tail a golden captures.
    const bool mbcheck_on = HAL::Env::u32("BOTANIK_NO_MBCHECK", 0) == 0;
    Sim::ModbusCheck modbus_check(greenhouse.setup_context(), &greenhouse.io(),
                                  &greenhouse.persist(), &greenhouse.logger());
#endif


    // Drive the firmware until a shutdown signal flips the run flag. SIGUSR1
    // re-execs at this safe point between iterations.

    // Manual clock: the harness owns the passage of time. Nothing advances
    // between commands, so the loop blocks on the control fd rather than
    // spinning against a clock that will not move.
    const bool manual = HAL::_manual_clock() && Sim::ctl_fd() >= 0;
    // Same clock, nobody driving it: the sim moves it itself, one beat per
    // iteration, so the loop lands on the scheduler's own grid rather than
    // arriving wherever tick()'s sleep left the virtual clock.
    const bool fast = HAL::_manual_clock() && !manual;
    int pending_beats = 0;
    if (manual) Sim::ctl_ack();      // ready; also what a re-exec announces itself with

    while (botanik_running) {
        if (manual) {
            if (pending_beats == 0) {
                pending_beats = Sim::ctl_next_command();
                if (pending_beats == 0) break;      // QUIT, or the harness left
            }
            Sim::ctl_advance_one_beat();
            --pending_beats;
        } else if (fast) {
            Sim::ctl_advance_one_beat();
            Sim::fast_pace();
        }
        // The field is serviced ONCE PER BEAT, and loop() says when one ran:
        // unguarded, this would run tens of times per beat, giving a simulated
        // bus quicker than any real one.
        //
        // AFTER the beat, which is what "before the next one" means in a loop:
        // the master's request went out in this beat's output phase, so the
        // device sees it now and its answer is waiting at the next beat's input
        // phase - one turnaround, like a real device.
        if (greenhouse.loop()) {
            Site::simulate(greenhouse.io(), (uint32_t)HAL::Clock::uptime_ms());
#if BOTANIK_FEATURE_NETWORK
            // The mock NTP/MQTT peer, serviced like the field bus: one
            // non-blocking pass per beat, on this thread.
            Network::netd_step();
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_SERVER
            fieldbus.step(HAL::Clock::uptime_ms());
#endif
#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT
            // The answers it reads are the ones the beat just published, which
            // is the whole point of the slave's two-phase split.
            if (mbcheck_on) modbus_check.step(HAL::Clock::uptime_ms());
#endif
        }
        if (manual && pending_beats == 0) Sim::ctl_ack();
        if (botanik_reexec) HAL::System::reset();
    }

    // shutdown() runs the reset hook and restores the console; 128 + signo is
    // the exit code for the supervisor.
    HAL::System::shutdown(128 + (int)botanik_shutdown_signo);
    return 0;
}
