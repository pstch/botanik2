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
        /** @brief The platform-health alarms, named by their POSITION in
         *  Alarms::PLATFORM_ALARMS. That table is the one list of them; this
         *  enum only says which row of it each assertion below drives.
         *
         *  @warning The order is the table's, so reordering platform.cpp means
         *  reordering this. That is the price of having one list instead of two,
         *  and it is the cheaper side: the second list was a hand-copied set of
         *  the same six names, and it had already drifted - POST checked five of
         *  the six, silently never verifying FLASH_FLT. */
        enum PlatformAlarm {
            PA_RESET = 0, PA_XRUN, PA_LOAD, PA_STACK, PA_JITTER, PA_FLASH, PA_COUNT
        };

        /** @name State
         *  @{ */
#if BOTANIK_FEATURE_ALARMS_PUBLISH
        Alarms::MqttAlarmNotifier          _notifier; ///< MQTT transition sink
#endif
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

        /** @brief Where each row of Alarms::PLATFORM_ALARMS sits in the merged
         *  condition table, resolved once by on_setup(); -1 if it is absent.
         *
         *  These used to be looked up by name on every activation. The name form
         *  is a linear strcmp scan of the whole table, and every platform name
         *  starts with "alarm_", so each comparison matched six bytes before
         *  diverging: six calls cost 159 comparisons a beat to re-resolve six
         *  names that cannot change (@ref dev_tasks).
         *
         *  Sized on the RESERVATION rather than on PA_COUNT, so a seventh
         *  platform alarm added to the table is resolved and POST-checked
         *  without touching this - only ASSERTING it needs a PA_* and a line in
         *  _assert_platform_alarms(). */
        int16_t  _ix[Config::PLATFORM_ALARMS_RESERVED] = {};
        /** @} */

    public:
        /** @brief Build the notifier + model. The model finds the site's alarms
         *  by scanning @p io for SCOPE_A rows and appends the platform-health
         *  ones after them; there is no alarm table to pass. */
        HealthMonitor(Logs::Logger* logger,
                      const Scheduler::SchedulerContext* sched,
                      const Vars::Bank* io = nullptr)
            : Component(logger, "HealthMonitor"),
#if BOTANIK_FEATURE_ALARMS_PUBLISH
              _notifier(Config::MQTT_TOPIC_PREFIX),
#endif
              _model(io, logger,
                     Alarms::PLATFORM_ALARMS, Alarms::PLATFORM_ALARM_COUNT),
              _sched(sched) {
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
            // Every platform alarm must resolve in the merged table, or _set()
            // would silently no-op (guards the alarm-namespace wiring). Reads
            // the indices on_setup() just resolved, over the same table it
            // resolved them from - there is no second list to drift.
            for (uint8_t i = 0; i < _platform_count(); i++)
                if (_ix[i] < 0) {
                    ok = false;
                    _post_fail("alarm missing", Alarms::PLATFORM_ALARMS[i].code);
                }
            // ...and the enum below has to still index that table. A seventh
            // alarm is fine (it resolves, it just is not asserted); a table
            // SHORTER than the enum means PA_* are pointing at the wrong rows.
            if (_platform_count() < PA_COUNT) {
                ok = false;
                _post_fail("platform alarm table", "shorter than PlatformAlarm");
            }
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
            if (_logger) _logger->critical(Logs::HEALTH, "POST: %s %s", what, detail);
        }

        /** @brief Resolve EVERY platform alarm to an index, once. The model's
         *  table is built at construction and never changes, so a name here is a
         *  constant - it just was not being treated as one.
         *
         *  Driven off Alarms::PLATFORM_ALARMS itself, so "every one" is
         *  structural rather than a list someone has to remember to extend. */
        void _resolve_platform_alarms() {
            for (uint8_t i = 0; i < Config::PLATFORM_ALARMS_RESERVED; i++)
                _ix[i] = -1;
            for (uint8_t i = 0; i < _platform_count(); i++)
                _ix[i] = (int16_t)_model.index_of(Alarms::PLATFORM_ALARMS[i].name);
        }

        /** @brief Platform alarms actually declared, clamped to what _ix holds.
         *  The reservation is asserted in platform.cpp; the clamp is so a build
         *  that got past it cannot walk off the array here. */
        static uint8_t _platform_count() {
            const uint8_t n = Alarms::PLATFORM_ALARM_COUNT;
            return n < Config::PLATFORM_ALARMS_RESERVED
                     ? n : (uint8_t)Config::PLATFORM_ALARMS_RESERVED;
        }

        /** @brief set_local() by resolved index, skipping an absent code the way
         *  the name form's no-op did - generic firmware must not require a site
         *  to declare every platform alarm. */
        void _set(int16_t ix, bool on) {
            if (ix >= 0) _model.set_local((uint8_t)ix, on);
        }

        /** @brief Raise/clear the platform alarms from the scheduler metrics.
         *  SYS_RST holds for HEALTH_RESET_WINDOW_MS; CPU_LOAD / CPU_JITT /
         *  STACK are level-driven; CPU_XRUN held for a window after an
         *  overrun. */
        void _assert_platform_alarms(uint64_t now) {
            _set(_ix[PA_RESET], now < Config::HEALTH_RESET_WINDOW_MS);
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
