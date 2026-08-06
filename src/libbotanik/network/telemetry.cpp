// REVIEW_35: TODO
/**
 * @file telemetry.cpp
 * @brief MqttTelemetry implementation - the base field set + the publish cycle.
 */
#include "libbotanik/network/telemetry.h" // MqttSender - what this file implements

#include <cstddef>                    // offsetof / size_t - the SchedulerContext field sweep
#include <cstdio>                     // snprintf - builds each topic and payload
#include <cstring>                    // memcpy - assembling the topic path

#include "libbotanik/hal/platform.h"  // HAL::Clock::uptime_ms - paces the sweep
#include "libbotanik/scheduler/scheduler.h" // Scheduler::SchedulerContext - the sys/* metrics published
#include "libbotanik/utils/text.h"    // Utils::format_float / format_int - the payload values

namespace Botanik { namespace Network {

    namespace {
        // Base fields. All but uptime read one SchedulerContext member, so they
        // are one formatter over an offset table rather than eight near-copies.
        // SchedulerContext is standard-layout, so offsetof is well-defined.
        struct SchedField {
            const char* name;
            size_t      off;
            bool        pct;    ///< a 0..1 float shown as a percentage
        };
        const SchedField SCHED[] = {
            { "cpu_load",      offsetof(Scheduler::SchedulerContext, cpu_load_ema),       true  },
            { "cpu_jitter",    offsetof(Scheduler::SchedulerContext, cpu_jitter_ema),     true  },
            { "loop_us",       offsetof(Scheduler::SchedulerContext, cpu_loop_us_recent), false },
            { "ticks_per_sec", offsetof(Scheduler::SchedulerContext, ticks_per_second),   false },
            { "overruns",      offsetof(Scheduler::SchedulerContext, overruns),           false },
            { "stack",         offsetof(Scheduler::SchedulerContext, stack_usage),        true  },
            { "stack_free_b",  offsetof(Scheduler::SchedulerContext, stack_bytes_free),   false },
        };

        // ctx is the SystemContext; a field returns false to skip when its
        // source is absent (a null sub-context).
        bool fmt_uptime(char* buf, size_t cap, void* ctx) {
            (void)ctx;
            Utils::format_int(buf, cap, (long long)(HAL::Clock::uptime_ms() / 1000));
            return true;
        }

        bool fmt_sched(char* buf, size_t cap, void* ctx, const SchedField& f) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            const char* base = reinterpret_cast<const char*>(s->scheduler);
            if (f.pct) {
                float v;
                memcpy(&v, base + f.off, sizeof(v));
                Utils::format_float(buf, cap, v * 100.0f, 1);
            } else {
                uint32_t v;
                memcpy(&v, base + f.off, sizeof(v));
                Utils::format_int(buf, cap, (long long)v);
            }
            return true;
        }
    }  // namespace

    void MqttSender::_publish_base(const SystemContext* sys) {
        char payload[Config::MQTT_PAYLOAD_LEN];
        if (fmt_uptime(payload, sizeof(payload), (void*)sys))
            _emit("system", "uptime_s", payload);
        for (const SchedField& f : SCHED)
            if (fmt_sched(payload, sizeof(payload), (void*)sys, f))
                _emit("system", f.name, payload);
    }

    void MqttSender::step(uint64_t now_ms) {
        const uint32_t period = Config::MQTT_TELEMETRY_PERIOD_MS;
        if (period == 0) return;                       // telemetry disabled
        if (_mqtt == nullptr || !_mqtt->connected()) {
            _cursor = SWEEP_IDLE;                      // a sweep cannot outlive the link
            return;
        }

        // Mid-sweep: hand out the next slice and nothing else. The cadence below
        // cannot restart one while this is true, so a slow sweep delays the next
        // cadence rather than overlapping with it.
        if (_sweeping()) { _publish_scalars(); return; }

        // First call after a (re)connect publishes immediately, then on cadence.
        if (_last_ms != 0 && now_ms - _last_ms < period) return;
        _last_ms = now_ms;

        // The base fields are a fixed handful and they describe the beat itself,
        // so they go out whole, at the head of the sweep.
        _publish_base(_sys);        // library status
        _cursor = 0;
        _publish_scalars();         // site variables, paced from here
    }

    void MqttSender::_publish_scalars() {
        if (_io == nullptr) { _cursor = SWEEP_IDLE; return; }
        char payload[Config::MQTT_PAYLOAD_LEN];
        // Straight down the variable table: what gets published is a property of
        // the row (its scope), and its sub-topic is the prefix of its name. No
        // site table selects them, so a new variable is on the broker as soon as
        // it exists rather than when someone remembers to list it.
        //
        // Resumed from _cursor and capped per activation - the scan of skipped
        // rows is cheap, the publish is not, so the budget counts topics SENT
        // rather than rows visited.
        const uint16_t budget = Config::MQTT_TELEMETRY_PER_STEP;
        uint16_t sent = 0;
        for (; _cursor < _io->count(); ++_cursor) {
            if (budget != 0 && sent >= budget) return;   // resume next beat
            Vars::Var h = _io->var(_cursor);
            const Vars::Variable& d = *h.meta();
            if (d.name == nullptr || !Vars::var_published(d)) continue;
            // Publish only what has actually been read: a default value on the
            // broker is indistinguishable from a measurement, and a subscriber
            // cannot tell the sensor died.
            if (!h.valid()) continue;
            const char* out = Vars::var_format(*_io, h, payload, sizeof(payload));
            _emit(Vars::var_group(d), d.name, out);
            sent++;
        }
        _cursor = SWEEP_IDLE;       // table exhausted: the sweep is done
    }

    void MqttSender::_emit(const char* group, const char* name, const char* payload) {
        char topic[Config::MQTT_TOPIC_LEN];
        int n = group != nullptr
            ? snprintf(topic, sizeof(topic), "%s/%s/%s/%s",
                       _prefix, Config::MQTT_TELEMETRY_TOPIC, group, name)
            : snprintf(topic, sizeof(topic), "%s/%s/%s",
                       _prefix, Config::MQTT_TELEMETRY_TOPIC, name);
        if (n <= 0 || (size_t)n >= sizeof(topic)) return;
        // Best-effort: a publish that can't be sent now is dropped (the next
        // cycle re-sends the current value).
        _mqtt->publish(topic, payload, Config::MQTT_TELEMETRY_RETAIN);
    }

}}
