#ifndef LIBBOTANIK_HEALTH_HEALTH_H
#define LIBBOTANIK_HEALTH_HEALTH_H

/**
 * @file health.h
 * @brief HealthMonitor - the coordinator the alarm flow runs through.
 *
 * Owns the alarm model + MQTT notifier; each beat it asserts the generic
 * platform-health alarms from the scheduler metrics, runs a small self-test, and
 * advances the model. It also produces the watchdog verdict - the one principled
 * "should we keep kicking" input:
 *
 *   - our loop is alive (we reach watchdog_ok() every beat) -> keep kicking;
 *   - a PLC fault is transient -> keep kicking (the fault is logged + alarmed),
 *     riding it through the PLC_FAULT_GRACE_MS grace window;
 *   - a PLC fault persists past the grace -> withhold the kick, letting the HW
 *     watchdog reset BOTH cores - the deliberate recovery that gives the PLC image
 *     a fresh start.
 *
 * Non-templated: the remote alarm bitset is passed into step() by the firmware.
 */

#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/alarms/alarms.h"        // AlarmModel, AlarmCondition
#include "libbotanik/alarms/platform.h"      // PLATFORM_ALARMS
#include "libbotanik/alarms/notifier.h"      // MqttAlarmNotifier
#include "libbotanik/logs/logger.h"
#include "libbotanik/scheduler/scheduler.h"  // SchedulerContext
#include "libbotanik/plcio/sync.h"           // PlcSyncContext
#include "libbotanik/utils/text.h"           // env_u8 (BOTANIK_NO_LOAD_ALARMS)

namespace Botanik { namespace Health {

    /** @brief Owns the alarm model + notifier, asserts platform-health alarms, runs
     *  the self-test, and gates the watchdog kick (see file header). */
    class HealthMonitor : public Component {
    private:
        /** @name State
         *  @{ */
        Alarms::MqttAlarmNotifier          _mqtt;
        Alarms::AlarmModel                 _model;
        Logs::Logger*                      _logger;   // log sink (may be null)
        const Scheduler::SchedulerContext* _sched;    // perf metrics (may be null)
        PlcIO::PlcSyncContext*             _plc;       // PLC link status (may be null)
        const void*                        _in_region; // PLC shared region (for POST)
        const void*                        _out_region;

        uint32_t _xrun_last      = 0;   // last overrun count seen (edge detect)
        uint64_t _xrun_until_ms  = 0;   // CPU_XRUN held active until this uptime
        uint32_t _rx_torn_last   = 0;   // last PLC torn-read count seen (edge detect)
        uint64_t _rx_torn_until_ms = 0; // RX_TORN held active until this uptime
        uint64_t _fault_since_ms = 0;   // uptime the PLC fault began (0 = not faulted)
        bool     _watchdog_ok    = true; // cached verdict (see watchdog_ok())
        bool     _post_ok        = false;
        bool     _posted         = false; // POST has run (fires on the first step())
        // BOTANIK_NO_LOAD_ALARMS: hold the CPU_* load alarms cleared (sim/test only,
        // for deterministic screen goldens). env_u8 reads 0 on OPTA, so it is off there.
        bool     _no_load_alarms = Utils::env_u8("BOTANIK_NO_LOAD_ALARMS", 0) != 0;
        /** @} */

    public:
        /** @brief Build the notifier + model around the site PLC alarm table (the
         *  generic platform-health alarms are appended by the model). */
        HealthMonitor(const Alarms::AlarmCondition* table, uint8_t count,
                      Logs::Logger* logger,
                      const Scheduler::SchedulerContext* sched,
                      PlcIO::PlcSyncContext* plc,
                      const void* in_region, const void* out_region)
            : _mqtt(Config::MQTT_TOPIC_PREFIX),
              _model(table, count, logger, &_mqtt,
                     Alarms::PLATFORM_ALARMS, Alarms::PLATFORM_ALARM_COUNT),
              _logger(logger), _sched(sched), _plc(plc),
              _in_region(in_region), _out_region(out_region) {}

        /** @name Queries
         *  @{ */
        /** @brief The alarm model (the terminal / AlarmsPage / shell bind to this). */
        Alarms::AlarmModel& model()      { return _model; }
        /** @brief Result of the boot POST (run on the first step()). */
        bool                post_ok()  const { return _post_ok; }
        /** @brief Whether the HW watchdog should be kicked this beat (see file header). */
        bool                watchdog_ok() const { return _watchdog_ok; }
        /** @} */

        /** @brief Per beat: on the first beat run POST (now that the sinks are
         *  registered), then assert the platform-health alarms, update the watchdog
         *  verdict, and advance the model with the (already-fresh) remote bitset.
         *  @p now is the current uptime in ms. */
        void step(uint64_t remote_raw, uint32_t rx_torn, uint64_t now) {
            announce_step(_logger, "HealthMonitor");
            if (!_posted) { _posted = true; _post(); }
            _assert_platform_alarms(now);
#if BOTANIK_FEATURE_PLC
            // Runtime self-test: a rising cross-core torn-read count raises RX_TORN.
            // Should never fire - the PLC handshake upholds mutual exclusion - so it is
            // a loud latched diagnostic if the contract is ever violated. (Without the
            // PLC feature rx_torn is always 0, so this is gated out entirely.)
            _held_event("RX_TORN", rx_torn, _rx_torn_last, _rx_torn_until_ms, now);
#else
            (void)rx_torn;
#endif
            _update_watchdog(now);
            _model.step(remote_raw, now);
        }

    private:
        /** @brief Power-on self-test, run once on the first step() (by which point the
         *  sinks are registered, so its verdict is actually logged). Validates cheap
         *  structural invariants over the stored PLC regions and logs the result. A
         *  failure is loud (CRITICAL) but does NOT withhold the watchdog. */
        void _post() {
            bool ok = true;
            // The platform codes the firmware asserts must resolve in the merged table,
            // or set_local() would silently no-op (guards the alarm-namespace wiring).
            static const char* const codes[] =
                { "SYS_RST", "CPU_LOAD", "CPU_JITT", "STACK", "CPU_XRUN", "RX_TORN" };
            for (const char* code : codes)
                if (_model.index_of(code) < 0) { ok = false; _post_fail("alarm code missing", code); }
#if BOTANIK_FEATURE_PLC
            // The PLC shared region must be present and 32-byte aligned (cache/DMA).
            // (Without the PLC feature there is no region - the firmware passes null.)
            if (_in_region == nullptr || _out_region == nullptr) {
                ok = false; _post_fail("PLC region null", "");
            } else if (((uintptr_t)_in_region % 32u) != 0 || ((uintptr_t)_out_region % 32u) != 0) {
                ok = false; _post_fail("PLC region misaligned", "");
            }
#endif
            // A logger with no writers would silently swallow everything.
            if (_logger == nullptr || !_logger->has_writers()) ok = false;
            _post_ok = ok;
            if (_logger) {
                if (ok) _logger->info(PlcIO::HEALTH, "POST ok (site %s)", Config::SITE_NAME);
                else    _logger->critical(PlcIO::HEALTH, "POST failed (site %s)", Config::SITE_NAME);
            }
        }

        /** @brief Log a POST failure line (CRITICAL, HEALTH topic). */
        void _post_fail(const char* what, const char* detail) {
            if (_logger) _logger->critical(PlcIO::HEALTH, "POST: %s %s", what, detail);
        }

        /** @brief Raise/clear the generic platform-health alarms from the scheduler
         *  metrics. SYS_RST holds for the first five minutes after boot; CPU_LOAD /
         *  CPU_JITT / STACK are level-driven; CPU_XRUN is held for a window after each
         *  overrun (a proper latched state set in the source, not a one-beat pulse). */
        void _assert_platform_alarms(uint64_t now) {
            _model.set_local("SYS_RST", now < 300000);
            if (_sched == nullptr) return;
            const uint32_t period_us = Config::TASK_PERIOD_MS * 1000u;
            // The CPU_* load alarms fire at host-timing-dependent moments; under
            // BOTANIK_NO_LOAD_ALARMS hold them cleared so the active set is stable
            // for screen goldens. SYS_RST and STACK are unaffected.
            const bool load = !_no_load_alarms;
            _model.set_local("CPU_LOAD", load &&
                (uint64_t)_sched->cpu_loop_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_LOAD_PCT);
            _model.set_local("CPU_JITT", load &&
                (uint64_t)_sched->cpu_jitter_us_recent * 100u
                    > (uint64_t)period_us * Config::ALARM_CPU_JITTER_PCT);
            _model.set_local("STACK",
                _sched->stack_usage * 100.0f > (float)Config::ALARM_STACK_PCT);
            if (load) _held_event("CPU_XRUN", _sched->overruns, _xrun_last, _xrun_until_ms, now);
            else      _model.set_local("CPU_XRUN", false);
        }

        /** @brief Hold a momentary "event" alarm active for ALARM_EVENT_HOLD_MS after
         *  its @p counter ticks (a proper latched state in the source, not a one-beat
         *  pulse that would depend on the table's latch_s). */
        void _held_event(const char* code, uint32_t counter, uint32_t& last,
                         uint64_t& until_ms, uint64_t now) {
            if (counter != last) { last = counter; until_ms = now + Config::ALARM_EVENT_HOLD_MS; }
            _model.set_local(code, now < until_ms);
        }

        /** @brief Timed-escalation watchdog verdict: keep kicking while healthy and
         *  through a transient PLC fault; withhold (and log the reboot, once) only once
         *  a fault persists past PLC_FAULT_GRACE_MS. */
        void _update_watchdog(uint64_t now) {
            if (_plc == nullptr || !_plc->faulted()) {
                _fault_since_ms = 0;
                _watchdog_ok    = true;
                return;
            }
            if (_fault_since_ms == 0) _fault_since_ms = now;   // fault edge
            bool within_grace = (now - _fault_since_ms) <= Config::PLC_FAULT_GRACE_MS;
            if (_watchdog_ok && !within_grace && _logger)      // kicking -> withholding edge
                _logger->critical(PlcIO::PLC_IO,
                    "PLC fault persisted, withholding watchdog kick (reboot imminent)");
            _watchdog_ok = within_grace;
        }
    };

}}  // namespace Botanik::Health

#endif // LIBBOTANIK_HEALTH_HEALTH_H
