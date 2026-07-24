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
 *   - our loop is alive (we reach watchdog_ok() every beat) -> keep kicking;
 *   - a PLC fault is transient -> keep kicking (the fault is logged + alarmed),
 *     riding it through the PLC_FAULT_GRACE_MS grace window;
 *   - a PLC fault persists past the grace -> withhold the kick, letting the HW
 *     watchdog reset BOTH cores - the deliberate recovery that gives the PLC
 *     image a fresh start.
 *
 * Non-templated: the remote alarm bitset is passed into step() by the firmware.
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/platform.h"
#include "libbotanik/alarms/publisher.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/utils/text.h"

namespace Botanik { namespace Health {

    /** @brief Owns the alarm model + notifier, asserts platform-health alarms,
     *  runs the self-test, and gates the watchdog kick (see file header). */
    class HealthMonitor : public Component {
    private:
        /** @name State
         *  @{ */
        Alarms::MqttAlarmNotifier          _notifier; //< MQTT transition sink
        Alarms::AlarmModel                 _model;
        /** perf metrics (may be null) */
        const Scheduler::SchedulerContext* _sched;
        /** PLC link status + live torn count (may be null) */
        PlcIO::PlcSyncContext*             _plc;
        /** PLC shared region (for POST) */
        const void*                        _in_region;
        const void*                        _out_region;

        /** last overrun count seen (edge detect) */
        uint32_t _xrun_last        = 0;
        /** CPU_XRUN held active until this uptime */
        uint64_t _xrun_until_ms    = 0;
        /** last torn-read count seen (edge detect) */
        uint32_t _rx_torn_last     = 0;
        /** RX_TORN held active until this uptime */
        uint64_t _rx_torn_until_ms = 0;
        /** uptime PLC fault began (0 = not faulted) */
        uint64_t _fault_since_ms   = 0;
        /** cached verdict (see watchdog_ok()) */
        bool     _watchdog_ok      = true;
        bool     _post_ok          = false;
        // BOTANIK_NO_LOAD_ALARMS: hold the CPU_* load alarms cleared for
        // deterministic screen goldens (sim/test only; env_u8 reads 0 on OPTA).
        bool     _no_load_alarms   = Utils::env_u8("BOTANIK_NO_LOAD_ALARMS", 0) != 0;
        /** @} */

    public:
        /** @brief Build the notifier + model around the site PLC alarm table
         *  (the model appends the platform-health alarms). */
        HealthMonitor(const Alarms::AlarmCondition* table, uint8_t count,
                      Logs::Logger* logger,
                      const Scheduler::SchedulerContext* sched,
                      PlcIO::PlcSyncContext* plc,
                      const void* in_region, const void* out_region,
                      const void* alarm_base = nullptr)
            : Component(logger, "HealthMonitor"),
              _notifier(Config::MQTT_TOPIC_PREFIX),
              _model(table, count, logger,
                     Alarms::PLATFORM_ALARMS, Alarms::PLATFORM_ALARM_COUNT,
                     alarm_base),
              _sched(sched), _plc(plc),
              _in_region(in_region), _out_region(out_region) {
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
        /** @brief Register an extra alarm-transition sink beyond the built-in
         *  MQTT notifier (fans out to all; false if the list is full). */
        bool add_alarm_notifier(Alarms::AlarmNotifier* n) {
            return _model.add_notifier(n);
        }
        /** @brief Result of the boot POST (run in setup() on the first step()). */
        bool                post_ok()  const { return _post_ok; }
        /** @brief Whether the HW watchdog should be kicked this beat (see
         *  file header). */
        bool                watchdog_ok() const { return _watchdog_ok; }
        /** @brief Healthy while the watchdog verdict holds and POST passed. */
        bool                healthy() const override { return _watchdog_ok && _post_ok; }
        /** @} */

    protected:
        /** @brief POST runs once on the first step() (sinks now registered). */
        void setup(uint64_t now) override { (void)now; _post(); }

        /** @brief Per beat: assert the platform alarms, update the watchdog
         *  verdict, and advance the model (which reads each site alarm's boolean
         *  from the PLC output payload directly). The torn count is read live
         *  through the stored pointer (null-safe). @p now is uptime in ms. */
        void on_step(uint64_t now) override {
            _assert_platform_alarms(now);
#if BOTANIK_FEATURE_PLC
            // A rising cross-core torn-read count raises RX_TORN: should never
            // fire (the handshake upholds mutual exclusion), so it is a loud
            // latched diagnostic.
            uint32_t rx_torn = _plc ? _plc->torn_count() : 0;
            _held_event("RX_TORN", rx_torn, _rx_torn_last, _rx_torn_until_ms, now);
#endif
            _update_watchdog(now);
            // The model reads each site alarm's boolean from the payload itself.
            _model.step(now);
        }

    private:
        /** @brief Power-on self-test, run once on the first step() (sinks
         *  registered, so the verdict is logged). Validates cheap structural
         *  invariants over the PLC regions. A failure is loud (CRITICAL) but
         *  does NOT withhold the watchdog. */
        void _post() {
            bool ok = true;
            // The platform codes the firmware asserts must resolve in the
            // merged table, or set_local() would silently no-op (guards the
            // alarm-namespace wiring).
            static const char* const codes[] =
                { "SYS_RST", "CPU_LOAD", "CPU_JITT", "STACK", "CPU_XRUN", "RX_TORN" };
            for (const char* code : codes)
                if (_model.index_of(code) < 0) {
                    ok = false; _post_fail("alarm code missing", code);
                }
#if BOTANIK_FEATURE_PLC
            // The PLC shared region must be present and 32-byte aligned (DMA).
            if (_in_region == nullptr || _out_region == nullptr) {
                ok = false; _post_fail("PLC region null", "");
            } else if (((uintptr_t)_in_region % 32u) != 0 ||
                       ((uintptr_t)_out_region % 32u) != 0) {
                ok = false; _post_fail("PLC region misaligned", "");
            }
#endif
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
            _model.set_local("SYS_RST", now < 300000);
            if (_sched == nullptr) return;
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            // Under BOTANIK_NO_LOAD_ALARMS hold the CPU_* load alarms cleared
            // so the active set is stable for screen goldens (SYS_RST / STACK
            // unaffected).
            const bool load = !_no_load_alarms;
            _model.set_local("CPU_LOAD", load &&
                (uint64_t)_sched->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_LOAD_PCT);
            _model.set_local("CPU_JITT", load &&
                (uint64_t)_sched->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_JITTER_PCT);
            _model.set_local("STACK",
                _sched->stack_usage * 100.0f > (float)Config::ALARM_STACK_PCT);
            if (load) _held_event("CPU_XRUN", _sched->overruns, _xrun_last,
                                  _xrun_until_ms, now);
            else      _model.set_local("CPU_XRUN", false);
        }

        /** @brief Hold a momentary "event" alarm active for ALARM_EVENT_HOLD_MS
         *  after its @p counter ticks (latched in the source, not via the
         *  table's off_delay_s). */
        void _held_event(const char* code, uint32_t counter, uint32_t& last,
                         uint64_t& until_ms, uint64_t now) {
            if (counter != last) {
                last = counter;
                until_ms = now + Config::ALARM_EVENT_HOLD_MS;
            }
            _model.set_local(code, now < until_ms);
        }

        /** @brief Timed-escalation watchdog verdict: keep kicking while healthy
         *  and through a transient PLC fault; withhold (log the reboot once)
         *  only once a fault persists past PLC_FAULT_GRACE_MS. */
        void _update_watchdog(uint64_t now) {
            if (_plc == nullptr || !_plc->faulted()) {
                _fault_since_ms = 0;
                _watchdog_ok    = true;
                return;
            }
            if (_fault_since_ms == 0) _fault_since_ms = now;   // fault edge
            bool within_grace =
                (now - _fault_since_ms) <= Config::PLC_FAULT_GRACE_MS;
            if (_watchdog_ok && !within_grace && _logger)   // kicking->withhold
                _logger->critical(Logs::PLC_IO,
                    "PLC fault persisted, withholding watchdog kick (reboot imminent)");
            _watchdog_ok = within_grace;
        }
    };

}}  // namespace Botanik::Health

#endif // LIBBOTANIK_HEALTH_HEALTH_H
