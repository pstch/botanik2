// REVIEW_35: TODO
#ifndef LIBBOTANIK_HEALTH_HEALTH_H
#define LIBBOTANIK_HEALTH_HEALTH_H

/**
 * @file health.h
 * @brief HealthMonitor - the coordinator the alarm flow runs through.
 *
 * Owns the alarm model + MQTT notifier; each beat it asserts the generic
 * platform-health alarms from the scheduler metrics, runs a small self-test,
 * and advances the model. It also produces the watchdog verdict - the one
 * principled "should we keep kicking" input:
 *
 *   - our loop is alive (we reach watchdog_ok() every beat) -> keep kicking.
 *
 * That is the whole verdict today: with no coprocessor link to lose, nothing
 * else can justify letting the hardware watchdog fire. Control-side fault
 * escalation belongs to the scan cycle, not here.
 */

#include <cstdint>                          // uint8_t/int16_t/uint64_t

#include "libbotanik/config/alarms.h"     // Config::ALARM_* / HEALTH_*
#include "libbotanik/config/identity.h"   // Config::SITE_NAME / FIRMWARE_VERSION / MQTT_TOPIC_PREFIX
#include "libbotanik/config/scheduler.h"  // Config::LOOP_* / TASK_* / GROUP_*

#include "libbotanik/alarms/alarms.h"       // Alarms::AlarmController / MqttAlarmNotifier
#include "libbotanik/alarms/platform.h"
#include "libbotanik/alarms/publisher.h"    // Alarms::Publisher
#include "libbotanik/hal/storage.h"         // HAL::Storage::fault_count
#include "libbotanik/logs/logger.h"         // Logs::Logger::has_writers
#include "libbotanik/scheduler/scheduler.h" // Scheduler::SchedulerContext

namespace Botanik { namespace Health {

    /** @brief Owns the alarm model + notifier, asserts platform-health alarms,
     *  runs the self-test, and gates the watchdog kick (see file header). */
    class HealthMonitor : public Component {
    private:
        /** @name State
         *  @{ */
#if BOTANIK_FEATURE_ALARMS_PUBLISH
        Alarms::MqttAlarmNotifier          _notifier; ///< MQTT transition sink
#endif
        Alarms::AlarmController                 _model;
        /** perf metrics (may be null) */
        const Scheduler::SchedulerContext* _sched;
        /** the variable bank the alarm rows live in (may be null) */
        const Vars::Bank*                  _io;

        /** last overrun count seen (edge detect) */
        uint32_t _xrun_last        = 0;
        /** CPU_XRUN held active until this uptime */
        uint64_t _xrun_until_ms    = 0;
        /** cached verdict (see watchdog_ok()) */
        bool     _watchdog_ok      = true;
        bool     _post_ok          = false;
        // BOTANIK_NO_LOAD_ALARMS: hold the CPU_* load alarms cleared for
        // deterministic screen goldens (sim/test only; reads 0 on OPTA).
        bool     _no_load_alarms   = HAL::Env::u8("BOTANIK_NO_LOAD_ALARMS", 0) != 0;

        /** @name The alarms this class raises
         *
         *  A named reference each, bound once in on_setup(). No enum, no index
         *  table and no order to keep in step with anything: the row is found by
         *  the name written beside it in _bind_alarms(), and a name matching no
         *  row is counted by the bank, logged, and failed by POST.
         *
         *  Bound rather than looked up per beat: a name lookup is a search, and
         *  these six names cannot change once the table is built.
         *  @{ */
        Vars::VarRef _a_reset  = { Vars::VAR_NONE };
        Vars::VarRef _a_xrun   = { Vars::VAR_NONE };
        Vars::VarRef _a_load   = { Vars::VAR_NONE };
        Vars::VarRef _a_stack  = { Vars::VAR_NONE };
        Vars::VarRef _a_jitter = { Vars::VAR_NONE };
        Vars::VarRef _a_flash  = { Vars::VAR_NONE };
        /** @brief Cleared by _bind() when a name resolves to nothing. */
        bool         _bound_ok = true;
        /** @} */

    public:
        /** @brief Build the notifier + model. The model finds every alarm by
         *  scanning @p io for SCOPE_A rows - the site's and the library's own
         *  (alarms/platform.h) alike - so there is no alarm table to pass. */
        HealthMonitor(Logs::Logger* logger,
                      const Scheduler::SchedulerContext* sched,
                      const Vars::Bank* io = nullptr)
            : Component(logger, "HealthMonitor"),
#if BOTANIK_FEATURE_ALARMS_PUBLISH
              _notifier(Config::MQTT_TOPIC_PREFIX),
#endif
              _model(io, logger), _sched(sched), _io(io) {
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
        /** @brief Healthy while the watchdog verdict holds and POST passed. */
        bool                healthy() const override { return _watchdog_ok && _post_ok; }
        /** @} */

    protected:
        /** @brief POST runs once on the first step() (sinks now registered). */
        void on_setup(uint64_t now) override {
            (void)now;
            _bind_alarms();
            _post();
        }

        /** @brief Per beat: assert the platform alarms, update the watchdog
         *  verdict, and advance the model (which reads each site alarm's boolean
         *  from its variable directly). @p now is uptime in ms. */
        void on_step(uint64_t now) override {
            _assert_platform_alarms(now);
            _update_watchdog(now);
            // The model reads each site alarm's boolean variable itself.
            _model.step(now);
        }

    private:
        /** @brief Power-on self-test, run once on the first step() (sinks
         *  registered, so the verdict is logged). Validates cheap structural
         *  invariants. A failure is loud (CRITICAL) but does NOT withhold the
         *  watchdog. */
        void _post() {
            bool ok = true;
            // Every alarm this class raises has to exist in the table, or the
            // raise would write to the inert sink and the fault would never be
            // announced. _bind() reported each miss by name as it happened.
            if (!_bound_ok) ok = false;
#if BOTANIK_FEATURE_LOGGING
            // A logger with no writers would silently swallow everything. Only
            // a defect when logging was asked for: a LOGGING=OFF build has no
            // writers BY CONSTRUCTION, and failing POST for it would report the
            // build's own configuration as a fault.
            if (_logger == nullptr || !_logger->has_writers()) ok = false;
#endif
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
         *  them, so this list IS the list - there is no second copy to drift
         *  from, which is what the code table beside the alarm table was. */
        void _bind_alarms() {
            _bound_ok = true;
            _a_reset  = _bind("alarm_system_reset");
            _a_xrun   = _bind("alarm_cpu_xrun");
            _a_load   = _bind("alarm_cpu_load");
            _a_stack  = _bind("alarm_stack");
            _a_jitter = _bind("alarm_cpu_jitter");
            _a_flash  = _bind("alarm_flash_error");
        }

        /** @brief Resolve @p name, reporting a miss for POST. A site that left
         *  BOTANIK_PLATFORM_ALARM_VARS out of its table lands here, six times,
         *  by name - which is the whole diagnosis. */
        Vars::VarRef _bind(const char* name) {
            Vars::VarRef r = { Vars::VAR_NONE };
            if (_io != nullptr) r = _io->find(name);
            if (r.index == Vars::VAR_NONE) {
                _bound_ok = false;
                _post_fail("alarm row missing", name);
            }
            return r;
        }

        /** @brief Raise or clear @p r. Writing the variable IS raising the alarm
         *  now: the model reads the same row on its next step. An unbound one
         *  lands on the bank's inert sink, so this needs no guard of its own -
         *  but POST has already said so out loud. */
        void _raise(Vars::VarRef r, bool on) {
            if (_io != nullptr) _io->var(r).set(on);
        }

        /** @brief Raise/clear the platform alarms from the scheduler metrics.
         *  SYS_RST holds for HEALTH_RESET_WINDOW_MS; CPU_LOAD / CPU_JITT /
         *  STACK are level-driven; CPU_XRUN held for a window after an
         *  overrun. */
        void _assert_platform_alarms(uint64_t now) {
            _raise(_a_reset, now < Config::HEALTH_RESET_WINDOW_MS);
            // Sticky by construction: the counter only ever rises, so once a
            // write has been refused the alarm stays up until a reboot. That is
            // the honest shape - a store that will not take writes does not
            // recover on its own, and the damage (settings reverting) is only
            // visible long after the fact.
            _raise(_a_flash, HAL::Storage::fault_count() > 0);
            if (_sched == nullptr) return;
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            // Under BOTANIK_NO_LOAD_ALARMS hold the CPU_* load alarms cleared
            // so the active set is stable for screen goldens (SYS_RST / STACK
            // unaffected).
            const bool load = !_no_load_alarms;
            _raise(_a_load, load &&
                (uint64_t)_sched->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_LOAD_PCT);
            _raise(_a_jitter, load &&
                (uint64_t)_sched->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_JITTER_PCT);
            _raise(_a_stack,
                _sched->stack_usage * 100.0f > (float)Config::ALARM_STACK_PCT);
            if (load) _held_event(_a_xrun, _sched->overruns, _xrun_last,
                                  _xrun_until_ms, now);
            else      _raise(_a_xrun, false);
        }

        /** @brief Hold a momentary "event" alarm active for ALARM_EVENT_HOLD_MS
         *  after its @p counter ticks (latched in the source, not via the
         *  table's hold_on_s). */
        void _held_event(Vars::VarRef r, uint32_t counter, uint32_t& last,
                         uint64_t& until_ms, uint64_t now) {
            if (counter != last) {
                last = counter;
                until_ms = now + Config::ALARM_EVENT_HOLD_MS;
            }
            _raise(r, now < until_ms);
        }

        /** @brief Watchdog verdict. The timed escalation existed to reboot both
         *  cores when the coprocessor link stayed faulted; with no link to lose,
         *  we kick while the loop is alive. Control-side fault escalation
         *  belongs to the scan cycle, not here.
         *
         *  @note So the verdict is CONSTANT today, and every consumer of a false
         *  one - the firmware loop's fault latch, the safe-state sweep it
         *  triggers, the withheld kick - is unreachable. That is deliberate: the
         *  seam is where a real verdict goes when there is something to judge,
         *  and it is cheaper to keep than to re-derive under whatever fault
         *  motivates it. Do not delete it as dead code without deciding that
         *  question first. */
        void _update_watchdog(uint64_t /*now*/) {
            _watchdog_ok = true;
        }
    };

}}  // namespace Botanik::Health

#endif // LIBBOTANIK_HEALTH_HEALTH_H
