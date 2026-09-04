#ifndef LIBBOTANIK_HEALTH_HEALTH_H
#define LIBBOTANIK_HEALTH_HEALTH_H

/**
 * @file health.h
 * @brief HealthMonitor - the coordinator the alarm flow runs through.
 *
 * Owns the alarm model + MQTT notifier; each beat it asserts the generic
 * platform-health alarms from the scheduler metrics, runs a small self-test,
 * and advances the model.
 *
 * It also produces the watchdog verdict, which is simply "the loop is alive":
 * reaching watchdog_ok() every beat is the only thing that keeps the kick
 * going. Control-side fault escalation belongs to the scan cycle, not here.
 */

#include <cstdint>
#include "libbotanik/vars/alarm_rows.h"
#include <cstdio>

#include "libbotanik/config/alarms.h"
#include "libbotanik/config/identity.h"
#include "libbotanik/config/scheduler.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/notifier.h"
#include "libbotanik/alarms/publisher.h"
#include "libbotanik/hal/storage.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/scheduler/scheduler.h"

namespace Botanik { namespace Tasks { struct Tick; } }
namespace Botanik { namespace Network { class NetworkHandler; class NtpClient; } }
namespace Botanik { namespace Plc { namespace Runtime { class Plant; } } }
// Forward-declared, not included: health/ raises and releases the system hold,
// and needs no more of plc/ than the type of the pointer it is handed.

namespace Botanik { namespace Health {

    /** @brief Owns the alarm model + notifier, asserts platform-health alarms,
     *  runs the self-test, and gates the watchdog kick (see file header). */
    class HealthMonitor : public Component {
    public:
        /** @brief This monitor, then the alarm model - the order they ran in
         *  when this stepped it directly. Nothing steps anything now: both are
         *  registry entries, so both are attributable and both have a rate. */
        void register_tasks() override;


        /** @brief The two network components whose faults are platform alarms.
         *  Borrowed, optional, and set by the composition root: this class
         *  raises the row, and each component knows its own state.
         *  @note Not a dependency of the monitor's own work - a build with no
         *  network simply never sets them. */
        void set_network(const Network::NetworkHandler* net) { _net = net; }
        void set_ntp(const Network::NtpClient* ntp) { _ntp = ntp; }

        /** @brief The scan roster, borrowed. Read only for its running set:
         *  a program leaving or joining the scan churns the quality of every
         *  row it drives, and that is settling rather than news.
         *  @see _quality_sweep */
        void set_plant(const Plc::Runtime::Plant* plant) { _plant = plant; }
    public:
        /** @brief How the unit is being taken out of service.
         *
         *  Both END the same way and DIFFER only in what happens once they get
         *  there, because getting there is the part that matters: every program
         *  drives its own actuators to their safe positions and says when it has
         *  arrived. A window travels for fifteen seconds and they move one at a
         *  time, so "stop now" and "stop safely" are a minute apart. */
        enum class Stop : uint8_t {
            NONE = 0,
            REBOOT,   ///< ...then stop kicking, and let the watchdog reset us
            HALT      ///< ...then stop the cycle, and keep kicking
        };

    private:
        static void _latch_task(Component* c, const Tasks::Tick& t);

        /** @brief Latch a requested ending, then carry this beat's verdict to
         *  control authority and the authority to the programs.
         *
         *  Here rather than in the composition root because everything it needs
         *  is already here: the verdict is this class's, the hold is on the
         *  Bank it already writes alarms into, and the gate is on the registry
         *  every Component is built with. It also raises SCAN_LATE, which is a
         *  platform alarm like the six others this binds.
         *  @note The ending is latched in every build; everything after it
         *  needs the bank. */
        void _latch_control(uint64_t now_ms);
        /** @brief Missed control activations at the last check; the alarm is
         *  the DELTA, so a scan that catches up clears it. */
        uint32_t _missed_said = 0;
        /** @brief The row carrying "a program is missing its period". */
        Vars::VarRef _scan_late = { Vars::VAR_NONE };

        /** @name Subsystem alarms
         *
         *  Platform conditions like the six above, but read off a component
         *  rather than off the scheduler's metrics. Bound by name, and written
         *  every beat - the row's own dwell decides what annunciates.
         *  @{ */

        Vars::VarRef _net_fault  = { Vars::VAR_NONE };
        Vars::VarRef _ntp_late   = { Vars::VAR_NONE };
        Vars::VarRef _log_flood  = { Vars::VAR_NONE };

        const Network::NetworkHandler* _net = nullptr;
        const Network::NtpClient*      _ntp = nullptr;
        /** @brief The logger's drop count at the previous beat: a flood is that
         *  number MOVING, not its size. */
        uint32_t _dropped_seen = 0;

        /** @} */

#if BOTANIK_FEATURE_VARS && BOTANIK_FEATURE_LOGGING
        /** @brief The three subsystem alarms, off the components that own the
         *  conditions. @see set_network */
        void _subsystem_alarms(Vars::Bank* io);

        /** @brief Whether a change is still settling rather than news: the
         *  bring-up window, or the one armed by a change of the scan roster.
         *  @see Config::HEALTH_QUIET_MS */
        bool _quiet(uint64_t now_ms) const {
            return now_ms < Config::HEALTH_QUIET_MS || now_ms < _hush_until_ms;
        }

        /** @brief One bounded pass of the validity sweep.
         *
         *  A row going invalid is how a dead instrument announces itself, and
         *  only the rows carrying a timeout ALARM say so today. Swept rather
         *  than hooked into the Bank: the model has no logger and should not
         *  acquire one to say what a supervisor can see from outside.
         *
         *  A SLICE per beat, because the table runs to several hundred rows and
         *  this sits beside the control scan. */
        void _quality_sweep(uint64_t now_ms);

        /** @brief One bit per row: was it valid last time this looked. */
        uint8_t  _was_valid[(Config::VAR_MAX + 7) / 8] = {};
        /** @brief Where the next slice starts. */
        uint16_t _quality_at = 0;
        /** @brief Whether a first full pass has been made. Until it has, the
         *  bits say nothing and a change is not a change - otherwise bring-up
         *  logs the whole table. */
        bool     _quality_seeded = false;
        /** @brief Uptime ms the armed hush runs to (0 = none). Declared ahead
         *  of the words beside it: 8-aligned, so the pair costs no padding. */
        uint64_t _hush_until_ms = 0;
        /** @brief The running set at the last check; a change arms the hush. */
        uint32_t _scan_mask     = 0;
#endif

        /** @brief The scan roster, borrowed and optional. @see set_plant */
        const Plc::Runtime::Plant* _plant = nullptr;

        /** @name State
         *  @{ */
#if BOTANIK_FEATURE_ALARMS_PUBLISH
        Alarms::MqttAlarmNotifier          _notifier; ///< MQTT transition sink
#endif
#if BOTANIK_FEATURE_VARS
        /** @brief The site's SCOPE_A rows as an alarm source. Declared BEFORE
         *  _model, which is constructed with its address. */
        Vars::BankAlarms                        _alarm_rows;
#endif
        Alarms::AlarmController                 _model;
        /** perf metrics (may be null) */
        const Scheduler::SchedulerContext* _sched;
        /** @brief The variable bank the alarm rows live in (may be null).
         *  Non-const: an alarm row is forceable from the ALARMS page, and the
         *  force lands on the bank through BankAlarms. */
        Vars::Bank*                        _io;

        /** last overrun count seen (edge detect) */
        uint32_t _xrun_last        = 0;
        /** cached verdict (see watchdog_ok()) */
        bool     _watchdog_ok      = true;
        /** latched by request_stop(); never cleared */
        Stop     _stop             = Stop::NONE;
        /** whether the retained rows have been written for this ending */
        bool     _stopped          = false;
        bool     _post_ok          = false;
        // BOTANIK_NO_LOAD_ALARMS: hold the CPU_* load alarms cleared for
        // deterministic screen goldens (sim/test only; reads 0 on OPTA).
        // Default ON under a virtual clock, where the beat's work takes no
        // simulated time at all and the duty figure they watch is not a
        // measurement of anything.
        bool     _no_load_alarms   =
            HAL::Env::u8("BOTANIK_NO_LOAD_ALARMS",
                         HAL::Clock::is_virtual() ? 1 : 0) != 0;

    public:
        /** @brief The alarms this class raises, in the order PLATFORM_NAMES
         *  spells them. An index rather than a name at the call site, because
         *  the two builds keep the boolean in different places. */
        enum PlatAlarm {
            PA_RESET = 0, PA_XRUN, PA_LOAD, PA_STACK, PA_JITTER, PA_FLASH,
            PA_COUNT
        };
    private:
        /** @brief The row each one is, by name. A name matching no row is
         *  counted by the bank, logged, and failed by POST. */
        static const char* const PLATFORM_NAMES[PA_COUNT];
#if !BOTANIK_FEATURE_VARS
        /** @brief What the variable table carries for these six when there IS
         *  one. Kept beside PLATFORM_NAMES, in the same enum order. */
        static const char* const    PLATFORM_DESCS[PA_COUNT];
        static const Alarms::AlarmSpec PLATFORM_SPECS[PA_COUNT];
#endif

#if BOTANIK_FEATURE_VARS
        /** @brief Bound once in on_setup() rather than looked up per beat. */
        Vars::VarRef _a[PA_COUNT];
#else
        /** @brief Where the six live with no table to hold them, and the source
         *  that lets the model read them. Per-instance because the source
         *  borrows pointers INTO this object. */
        bool                     _a_state[PA_COUNT] = { false, false, false, false, false, false };
        Alarms::StaticAlarm      _a_rows[PA_COUNT];
        Alarms::StaticAlarmTable _alarm_rows;
#endif
        /** @brief Cleared by _bind() when a name resolves to nothing. */
        bool         _bound_ok = true;

        /** @} */

        /** @name The thresholds those alarms fire at
         *
         *  Parameters, not constants: what counts as too much load, jitter or
         *  stack is a property of the INSTALLATION, and the number you want to
         *  move is the one a unit is nuisance-alarming on - at 3am, over a
         *  terminal, without a toolchain. `Config::ALARM_*_PCT` are the factory
         *  defaults behind them.
         *
         *  Read live off the bound row each beat rather than cached on a commit
         *  serial: it is one indexed load against a threshold comparison that
         *  was already happening, and there is nothing to re-initialise when it
         *  moves. Bound beside the alarms they threshold, and unbound they read
         *  the bank's inert sink, so _pct() falls back to the Config value.
         *  @{ */
#if BOTANIK_FEATURE_VARS
        Vars::VarRef _p_load_pct   = { Vars::VAR_NONE };
        Vars::VarRef _p_jitter_pct = { Vars::VAR_NONE };
        Vars::VarRef _p_stack_pct  = { Vars::VAR_NONE };
#else
        // No rows to point at; _pct() ignores these and answers the default.
        static const int _p_load_pct = 0, _p_jitter_pct = 0, _p_stack_pct = 0;
#endif
        /** @} */

    public:
        /** @brief Build the notifier + model. Either way the model is handed
         *  `_alarm_rows`: with a variable table that is a BankAlarms over the
         *  SCOPE_A rows, without one a StaticAlarmTable over the six this
         *  object declares itself. */
        HealthMonitor(const SetupContext& ctx,
                      const Scheduler::SchedulerContext* sched,
                      Vars::Bank* io = nullptr)
            : Component(ctx, "HealthMonitor"),
#if BOTANIK_FEATURE_ALARMS_PUBLISH
              _notifier(Config::MQTT_TOPIC_PREFIX),
#endif
              _model(ctx, &_alarm_rows),
              _sched(sched), _io(io) {
#if !BOTANIK_FEATURE_VARS
            // With no table to declare them in, the six platform alarms are
            // this object's own: each row points its `raw` at the bool _raise()
            // writes. Built here rather than in flash because that pointer is
            // per-instance.
            for (uint8_t i = 0; i < PA_COUNT; i++) {
                _a_rows[i].name  = PLATFORM_NAMES[i];
                _a_rows[i].desc  = PLATFORM_DESCS[i];
                _a_rows[i].notes = "";
                _a_rows[i].spec  = PLATFORM_SPECS[i];
                _a_rows[i].raw   = &_a_state[i];
            }
            _alarm_rows = Alarms::StaticAlarmTable(_a_rows, PA_COUNT);
#endif
#if BOTANIK_FEATURE_ALARMS_PUBLISH
            _model.add_notifier(&_notifier);   // the built-in MQTT sink
#endif
        }

        /** @name Queries
         *  @{ */
        /** @brief The alarm controller (terminal / AlarmsPage / shell bind to
         *  this). */
        Alarms::AlarmController& alarm_model() { return _model; }
#if BOTANIK_FEATURE_ALARMS_PUBLISH
        /** @brief Wire the alarm notifier's transport (the firmware passes the
         *  MQTT client once it exists). Until then the notifier is a no-op. */
        void set_alarm_publisher(Alarms::Publisher* pub) {
            _notifier.set_publisher(pub);
        }
#endif
        /** @brief Whether the HW watchdog should be kicked this beat (see
         *  file header). */
        bool                watchdog_ok() const { return _watchdog_ok; }

        /** @brief Take the unit out of service. A LATCH, not a request that can
         *  be reconsidered - there is no un-reboot - and the stop it starts
         *  runs to completion first.
         *
         *  Deliberately not a HAL::System::reset(): the reset path an
         *  unattended unit takes when it wedges is the one worth exercising by
         *  hand, and it leaves the loop running (logs flush, the screen
         *  repaints) right up to the reset. */
        void request_stop(Stop what) {
            if (_stop == Stop::NONE) _stop = what;
        }
        /** @brief Reboot once every program has parked (the shell's `reboot`). */
        void request_reboot() { request_stop(Stop::REBOOT); }
        /** @brief Whether a reboot is pending - the kick is being withheld. */
        bool reboot_requested() const { return _stop == Stop::REBOOT; }
        Stop stop_requested() const { return _stop; }
        /** @brief Whether the record has been written for this ending. The
         *  LIFECYCLE decides when the laws stop and when the kick may be
         *  withheld; this says only that the store is up to date.
         *  @see Scheduler::RunState */
        bool stopped() const { return _stopped; }
        /** @brief Whether the beat should stop running. A halted unit keeps
         *  kicking: the watchdog says the loop is alive, and it is. The
         *  LIFECYCLE gates when this is acted on - loop() waits for HALT, so
         *  the shutdown hold runs first. */
        bool halted() const { return _stopped && _stop == Stop::HALT; }
        /** @brief Healthy while the watchdog verdict holds and POST passed. */
        bool                healthy() const override { return _watchdog_ok && _post_ok; }
        /** @} */

    protected:
        /** @brief POST runs once on the first step() (sinks now registered). */
        void on_setup(uint64_t now) override {
#if BOTANIK_FEATURE_VARS
            _alarm_rows.bind(_io);
#endif
            // The model's setup takes a slot per alarm the source declares, so
            // it cannot run before the bind above. Driven from here rather than
            // left to registration order, which would make it an ordering trap
            // nothing checks. setup() is idempotent, so the registry's own call
            // is a no-op.
            _model.setup(now);
            _bind_alarms();
            _post();
        }

        /** @brief Per beat: assert the platform alarms, update the watchdog
         *  verdict, and advance the model (which reads each site alarm's boolean
         *  from its variable directly). @p now is uptime in ms. */
        void on_service(uint64_t now) override {
            _assert_platform_alarms(now);
            _update_watchdog(now);
        }

    private:
        /** @brief Power-on self-test, run once on the first step() (sinks
         *  registered, so the verdict is logged). Validates cheap structural
         *  invariants. A failure is loud (CRITICAL) but does NOT withhold the
         *  watchdog. */
        void _post() {
            bool ok = true;
            // Every alarm this class raises has to exist in the table, or the
            // raise writes to the inert sink and the fault is never announced.
            if (!_bound_ok) ok = false;
#if BOTANIK_FEATURE_LOGGING
            // A logger with no writers silently swallows everything. Only a
            // defect when logging was asked for: a LOGGING=OFF build has no
            // writers by construction.
            if (_logger == nullptr || !_logger->has_writers()) ok = false;
#endif
            // The model holds ALARM_SLOTS conditions and the table may declare
            // more; the lost tail is never driven, and since the library's rows
            // splice FIRST what goes is the site's own. The BOTANIK_ASSERT in
            // the controller compiles to nothing without DEBUG (every OPTA
            // build), so this is the check that runs on hardware.
            if (_model.declared_count() > _model.count()) {
                ok = false;
                char detail[80];
                snprintf(detail, sizeof(detail),
                         "%u alarms declared, %u driven: raise ALARM_SLOTS",
                         (unsigned)_model.declared_count(),
                         (unsigned)_model.count());
                _post_fail("alarm table overruns the model", detail);
            }
            // Rows that never made it into the bank. A refused registration is
            // the failure mode registration BUYS: with one static table "too
            // many variables" could not happen, and now it can. Every name in
            // the lost segment resolves to the sink, so the subsystem that owns
            // them looks present and does nothing.
            if (_io != nullptr && _io->refused_count() != 0) {
                ok = false;
                char detail[80];
                snprintf(detail, sizeof(detail),
                         "%u rows refused: raise VAR_MAX (or VAR_SEGMENTS)",
                         (unsigned)_io->refused_count());
                _post_fail("the variable table did not fit", detail);
            }
            // Parameters outside the contiguous block: the segments were
            // registered in the wrong order, and the PARAMS page addresses the
            // block 0-based, so what it shows is not what was declared.
            if (_io != nullptr && _io->param_stray_count() != 0) {
                ok = false;
                char detail[80];
                snprintf(detail, sizeof(detail),
                         "%u params outside the block: register params first",
                         (unsigned)_io->param_stray_count());
                _post_fail("the parameter block is not contiguous", detail);
            }
            _post_ok = ok;
            if (_logger) {
                if (ok) _logger->info(Logs::HEALTH, "POST ok (site %s)",
                                      Config::SITE_NAME);
                else    _logger->critical(Logs::HEALTH, "POST failed (site %s)",
                                          Config::SITE_NAME);
            }
        }

        /** @brief Log a POST failure line (CRITICAL, HEALTH topic). */
        void _post_fail(const char* what, const char* detail) {
            if (_logger) _logger->critical(Logs::HEALTH, "POST: %s\n%s", what, detail);
        }

        /** @brief Bind every alarm this class raises, once. Nothing else names
         *  them, so this list IS the list: no second copy to drift from. */
        void _bind_alarms() {
            _bound_ok = true;
#if BOTANIK_FEATURE_VARS
            for (uint8_t i = 0; i < PA_COUNT; i++) _a[i] = _bind(PLATFORM_NAMES[i]);
            // Through find(), not _bind(): these rows are gated on their own
            // feature, and a build without the network is not a build with a
            // missing alarm row.
            if (_io != nullptr) {
                _net_fault = _io->find("alarm_net_fault");
                _ntp_late  = _io->find("alarm_ntp_timeout");
                _log_flood = _io->find("alarm_log_flood");
            }
            // The thresholds go through find() directly, not _bind(): a missing
            // one falls back to its Config default and the monitor still works,
            // where a missing ALARM row means a fault that can never be raised.
            // Failing POST for the two together would say the same word about
            // two different severities.
            if (_io != nullptr) {
                _p_load_pct   = _io->find("sys_cpu_load_pct");
                _p_jitter_pct = _io->find("sys_jitter_pct");
                _p_stack_pct  = _io->find("sys_stack_pct");
            }
#endif
        }

#if BOTANIK_FEATURE_VARS
        /** @brief A threshold's live percent: the parameter, or @p dflt when the
         *  row is absent or reads invalid. */
        uint8_t _pct(Vars::VarRef r, uint8_t dflt) const {
            if (_io == nullptr || r.index == Vars::VAR_NONE) return dflt;
            const Vars::Var h = _io->var(r);
            if (!h.valid()) return dflt;
            const float v = h.as_f32();
            return (v >= 1.0f && v <= 100.0f) ? (uint8_t)v : dflt;
        }
#else
        /** @brief No parameter to read: the build-time value IS the threshold. */
        uint8_t _pct(int, uint8_t dflt) const { return dflt; }
#endif

        /** @brief Resolve @p name, reporting a miss for POST. A site that left
         *  BOTANIK_PLATFORM_ALARM_VARS out of its table lands here six times,
         *  by name, which is the whole diagnosis. */
        Vars::VarRef _bind(const char* name) {
            Vars::VarRef r = { Vars::VAR_NONE };
            if (_io != nullptr) r = _io->find(name);
            if (r.index == Vars::VAR_NONE) {
                _bound_ok = false;
                _post_fail("alarm row missing", name);
            }
            return r;
        }

        /** @brief Raise or clear @p r. Writing the variable IS raising the
         *  alarm: the model reads the same row on its next step. An unbound one
         *  lands on the bank's inert sink, so this needs no guard. */
        void _raise(PlatAlarm a, bool on) {
#if BOTANIK_FEATURE_VARS
            if (_io != nullptr) _io->var(_a[a]).set(on);
#else
            _a_state[a] = on;
#endif
        }

        /** @brief Raise/clear the platform alarms from the scheduler metrics.
         *  SYS_RST holds for HEALTH_RESET_WINDOW_MS; CPU_LOAD / CPU_JITT /
         *  STACK are level-driven; CPU_XRUN is a one-beat PULSE the table's
         *  own hold_s latches (see below). */
        void _assert_platform_alarms(uint64_t now) {
            _raise(PA_RESET, now < Config::HEALTH_RESET_WINDOW_MS);
            // Sticky by construction: the counter only ever rises, so a refused
            // write holds the alarm up until a reboot. A store that will not
            // take writes does not recover on its own.
            _raise(PA_FLASH, HAL::Storage::fault_count() > 0);
            if (_sched == nullptr) return;
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            // Under BOTANIK_NO_LOAD_ALARMS hold the CPU_* load alarms cleared
            // so the active set is stable for screen goldens (SYS_RST / STACK
            // unaffected).
            const bool load = !_no_load_alarms;
            _raise(PA_LOAD, load &&
                (uint64_t)_sched->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * _pct(_p_load_pct, Config::ALARM_CPU_LOAD_PCT));
            _raise(PA_JITTER, load &&
                (uint64_t)_sched->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * _pct(_p_jitter_pct, Config::ALARM_CPU_JITTER_PCT));
            _raise(PA_STACK,
                _sched->stack_usage * 100.0f > (float)_pct(_p_stack_pct, Config::ALARM_STACK_PCT));
            // A counter, not a level: pulse the row for the one beat the count
            // moved and let the CPU_XRUN row's own hold_s do the latching, like
            // every other alarm. Safe because `health` and `alarms` share a rate
            // period and a phase (HEALTH_PERIOD_MS / PHASE_SERVICE), so the pulse is
            // always sampled - keep them together or it is missed.
            const bool xrun = load && _sched->overruns != _xrun_last;
            _xrun_last = _sched->overruns;
            _raise(PA_XRUN, xrun);
        }

        /** @brief Watchdog verdict: the loop is alive, so constant while
         *  stepping (see the file header) - unless a reboot was asked for, which
         *  is the one case where letting the watchdog fire IS the intent. */
        void _update_watchdog(uint64_t /*now*/) {
            // Only once the record is written. Withholding the kick while the
            // windows are still travelling would reset the unit mid-stroke,
            // which is the one thing the shutdown hold exists to avoid.
            _watchdog_ok = !(_stopped && _stop == Stop::REBOOT);
        }
    };

}}  // namespace Botanik::Health

#endif // LIBBOTANIK_HEALTH_HEALTH_H
