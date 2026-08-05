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
        /** @brief The platform-health alarms this class drives, in the order the
         *  one code list below declares them. Nothing outside here depends on
         *  the order - the codes are resolved by name - so this is an index, not
         *  a layout. */
        enum PlatformAlarm {
            PA_RESET = 0, PA_FLASH, PA_LOAD, PA_JITTER, PA_STACK, PA_XRUN, PA_COUNT
        };

        /** @brief The one list of platform codes: what on_setup() resolves and
         *  what POST verifies resolved. A function-local static so the header
         *  stays self-contained and every TU shares one copy. */
        static const char* const* _codes() {
            static const char* const c[PA_COUNT] = {
                "alarm_system_reset", "alarm_flash_error", "alarm_cpu_load",
                "alarm_cpu_jitter",   "alarm_stack",       "alarm_cpu_xrun",
            };
            return c;
        }

        /** @name State
         *  @{ */
        Alarms::MqttAlarmNotifier          _notifier; ///< MQTT transition sink
        Alarms::AlarmController                 _model;
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

        /** @brief Where each PA_* code sits in the merged condition table,
         *  resolved once by on_setup(); -1 if the code is absent.
         *
         *  These used to be looked up by name on every activation. set_local()'s
         *  name form is a linear strcmp scan of the whole table, and every code
         *  in it starts with "alarm_", so each comparison matched six bytes
         *  before diverging: six calls cost 159 comparisons a beat to re-resolve
         *  six names that cannot change (@ref dev_tasks).
         *
         *  Indexed by PlatformAlarm off the one code list, rather than six named
         *  members beside a second hand-written list: POST used to carry its own
         *  copy of the names and had already drifted from this one (it checked
         *  five of the six, silently not verifying FLASH_FLT). */
        int16_t  _ix[PA_COUNT] = {};
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
        /** @brief The alarm controller (terminal / AlarmsPage / shell bind to
         *  this). */
        Alarms::AlarmController& alarm_model() { return _model; }
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
        void on_setup(uint64_t now) override {
            (void)now;
            _resolve_platform_alarms();
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
            // Every platform code the firmware asserts must resolve in the
            // merged table, or _set() would silently no-op (guards the
            // alarm-namespace wiring). Reads the indices on_setup() just
            // resolved, so there is nothing here to drift from them.
            for (uint8_t i = 0; i < PA_COUNT; i++)
                if (_ix[i] < 0) { ok = false; _post_fail("alarm missing", _codes()[i]); }
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

        /** @brief Resolve the platform alarm codes to indices, once. The model's
         *  table is built at construction and never changes, so a name here is a
         *  constant - it just was not being treated as one. */
        void _resolve_platform_alarms() {
            for (uint8_t i = 0; i < PA_COUNT; i++)
                _ix[i] = (int16_t)_model.index_of(_codes()[i]);
        }

        /** @brief set_local() by resolved index, skipping an absent code the way
         *  the name form's no-op did - generic firmware must not require a site
         *  to declare every platform alarm. */
        void _set(int16_t ix, bool on) {
            if (ix >= 0) _model.set_local((uint8_t)ix, on);
        }

        /** @brief Raise/clear the platform alarms from the scheduler metrics.
         *  SYS_RST holds for the first five minutes; CPU_LOAD / CPU_JITT /
         *  STACK are level-driven; CPU_XRUN held for a window after an
         *  overrun. */
        void _assert_platform_alarms(uint64_t now) {
            _set(_ix[PA_RESET], now < 300000);
            // Sticky by construction: the counter only ever rises, so once a
            // write has been refused the alarm stays up until a reboot. That is
            // the honest shape - a store that will not take writes does not
            // recover on its own, and the damage (settings reverting) is only
            // visible long after the fact.
            _set(_ix[PA_FLASH], HAL::Storage::fault_count() > 0);
            if (_sched == nullptr) return;
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            // Under BOTANIK_NO_LOAD_ALARMS hold the CPU_* load alarms cleared
            // so the active set is stable for screen goldens (SYS_RST / STACK
            // unaffected).
            const bool load = !_no_load_alarms;
            _set(_ix[PA_LOAD], load &&
                (uint64_t)_sched->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_LOAD_PCT);
            _set(_ix[PA_JITTER], load &&
                (uint64_t)_sched->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_JITTER_PCT);
            _set(_ix[PA_STACK],
                _sched->stack_usage * 100.0f > (float)Config::ALARM_STACK_PCT);
            if (load) _held_event(_ix[PA_XRUN], _sched->overruns, _xrun_last,
                                  _xrun_until_ms, now);
            else      _set(_ix[PA_XRUN], false);
        }

        /** @brief Hold a momentary "event" alarm active for ALARM_EVENT_HOLD_MS
         *  after its @p counter ticks (latched in the source, not via the
         *  table's hold_on_s). */
        void _held_event(int16_t ix, uint32_t counter, uint32_t& last,
                         uint64_t& until_ms, uint64_t now) {
            if (counter != last) {
                last = counter;
                until_ms = now + Config::ALARM_EVENT_HOLD_MS;
            }
            _set(ix, now < until_ms);
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
