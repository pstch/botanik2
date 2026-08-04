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

#include <cstdint>

#include "../../libbotanik_config.h"
#include "../alarms/alarms.h"
#include "../alarms/platform.h"
#include "../alarms/publisher.h"
#include "../logs/logger.h"
#include "../hal/storage.h"   // fault_count() -> FLASH_FLT
#include "../scheduler/scheduler.h"
#include "../utils/text.h"

namespace Botanik { namespace Health {

    /** @brief Owns the alarm model + notifier, asserts platform-health alarms,
     *  runs the self-test, and gates the watchdog kick (see file header). */
    class HealthMonitor : public Component {
    private:
        /** @name State
         *  @{ */
        Alarms::MqttAlarmNotifier          _notifier; ///< MQTT transition sink
        Alarms::AlarmModel                 _model;
        /** perf metrics (may be null) */
        const Scheduler::SchedulerContext* _sched;

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
        /** @} */

    public:
        /** @brief Build the notifier + model. The model finds the site's alarms
         *  by scanning @p io for SCOPE_A rows and appends the platform-health
         *  ones after them; there is no alarm table to pass. */
        HealthMonitor(Logs::Logger* logger,
                      const Scheduler::SchedulerContext* sched,
                      const Vars::Bank* io = nullptr)
            : Component(logger, "HealthMonitor"),
              _notifier(Config::MQTT_TOPIC_PREFIX),
              _model(io, logger,
                     Alarms::PLATFORM_ALARMS, Alarms::PLATFORM_ALARM_COUNT),
              _sched(sched) {
            _model.add_notifier(&_notifier);   // the built-in MQTT sink
        }

        /** @name Queries
         *  @{ */
        /** @brief The alarm model (terminal / AlarmsPage / shell bind to
         *  this). */
        Alarms::AlarmModel& model()      { return _model; }
        /** @brief Wire the alarm notifier's transport (the firmware passes the
         *  MQTT client once it exists). Until then the notifier is a no-op. */
        void set_alarm_publisher(Alarms::Publisher* pub) {
            _notifier.set_publisher(pub);
        }
        /** @brief Whether the HW watchdog should be kicked this beat (see
         *  file header). */
        bool                watchdog_ok() const { return _watchdog_ok; }
        /** @brief Healthy while the watchdog verdict holds and POST passed. */
        bool                healthy() const override { return _watchdog_ok && _post_ok; }
        /** @} */

    protected:
        /** @brief POST runs once on the first step() (sinks now registered). */
        void on_setup(uint64_t now) override { (void)now; _post(); }

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
            // The platform codes the firmware asserts must resolve in the
            // merged table, or set_local() would silently no-op (guards the
            // alarm-namespace wiring).
            static const char* const codes[] =
                { "alarm_system_reset", "alarm_cpu_load", "alarm_cpu_jitter", "alarm_stack", "alarm_cpu_xrun" };
            for (const char* name : codes)
                if (_model.index_of(name) < 0) {
                    ok = false; _post_fail("alarm missing", name);
                }
            // A logger with no writers would silently swallow everything.
            if (_logger == nullptr || !_logger->has_writers()) ok = false;
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
            if (_logger) _logger->critical(Logs::HEALTH, "POST: %s %s", what, detail);
        }

        /** @brief Raise/clear the platform alarms from the scheduler metrics.
         *  SYS_RST holds for the first five minutes; CPU_LOAD / CPU_JITT /
         *  STACK are level-driven; CPU_XRUN held for a window after an
         *  overrun. */
        void _assert_platform_alarms(uint64_t now) {
            _model.set_local("alarm_system_reset", now < 300000);
            // Sticky by construction: the counter only ever rises, so once a
            // write has been refused the alarm stays up until a reboot. That is
            // the honest shape - a store that will not take writes does not
            // recover on its own, and the damage (settings reverting) is only
            // visible long after the fact.
            _model.set_local("alarm_flash_error", HAL::Storage::fault_count() > 0);
            if (_sched == nullptr) return;
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            // Under BOTANIK_NO_LOAD_ALARMS hold the CPU_* load alarms cleared
            // so the active set is stable for screen goldens (SYS_RST / STACK
            // unaffected).
            const bool load = !_no_load_alarms;
            _model.set_local("alarm_cpu_load", load &&
                (uint64_t)_sched->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_LOAD_PCT);
            _model.set_local("alarm_cpu_jitter", load &&
                (uint64_t)_sched->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_JITTER_PCT);
            _model.set_local("alarm_stack",
                _sched->stack_usage * 100.0f > (float)Config::ALARM_STACK_PCT);
            if (load) _held_event("alarm_cpu_xrun", _sched->overruns, _xrun_last,
                                  _xrun_until_ms, now);
            else      _model.set_local("alarm_cpu_xrun", false);
        }

        /** @brief Hold a momentary "event" alarm active for ALARM_EVENT_HOLD_MS
         *  after its @p counter ticks (latched in the source, not via the
         *  table's off_delay_s). */
        void _held_event(const char* name, uint32_t counter, uint32_t& last,
                         uint64_t& until_ms, uint64_t now) {
            if (counter != last) {
                last = counter;
                until_ms = now + Config::ALARM_EVENT_HOLD_MS;
            }
            _model.set_local(name, now < until_ms);
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
