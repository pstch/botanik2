// REVIEW_35: TODO
/**
 * @file telemetry.cpp
 * @brief MqttTelemetry implementation - the base field set + the publish cycle.
 */
#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "../hal/platform.h"
#include "../scheduler/scheduler.h"
#include "../utils/text.h"

namespace Botanik { namespace Network {

    namespace {
        // Base-field formatters. ctx is the SystemContext; a field returns false
        // to skip when its source is absent (a null sub-context).
        bool fmt_uptime(char* buf, size_t cap, void* ctx) {
            (void)ctx;
            Utils::format_uint(buf, cap, (unsigned long)(HAL::Clock::uptime_ms() / 1000));
            return true;
        }
        bool fmt_cpu_load(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_float(buf, cap, s->scheduler->cpu_load_ema * 100.0f, 1);
            return true;
        }
        bool fmt_cpu_jitter(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_float(buf, cap, s->scheduler->cpu_jitter_ema * 100.0f, 1);
            return true;
        }
        bool fmt_stack(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_float(buf, cap, s->scheduler->stack_usage * 100.0f, 1);
            return true;
        }
        bool fmt_loop_us(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_uint(buf, cap, (unsigned long)s->scheduler->cpu_loop_us_recent);
            return true;
        }
        bool fmt_ticks(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_uint(buf, cap, (unsigned long)s->scheduler->ticks_per_second);
            return true;
        }
        bool fmt_overruns(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_uint(buf, cap, (unsigned long)s->scheduler->overruns);
            return true;
        }
        bool fmt_stack_free(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->scheduler == nullptr) return false;
            Utils::format_uint(buf, cap, (unsigned long)s->scheduler->stack_bytes_free);
            return true;
        }
    }  // namespace

    const TelemetryField* MqttSender::_base_fields(size_t& count) {
        static const TelemetryField TABLE[] = {
            { "uptime_s",      &fmt_uptime     },
            { "cpu_load",      &fmt_cpu_load   },
            { "cpu_jitter",    &fmt_cpu_jitter },
            { "loop_us",       &fmt_loop_us    },
            { "ticks_per_sec", &fmt_ticks      },
            { "overruns",      &fmt_overruns   },
            { "stack",         &fmt_stack      },
            { "stack_free_b",  &fmt_stack_free },
        };
        count = sizeof(TABLE) / sizeof(TABLE[0]);
        return TABLE;
    }

    void MqttSender::step(uint64_t now_ms) {
        const uint32_t period = Config::MQTT_TELEMETRY_PERIOD_MS;
        if (period == 0) return;                       // telemetry disabled
        if (_mqtt == nullptr || !_mqtt->connected()) return;
        // First call after a (re)connect publishes immediately, then on cadence.
        if (_last_ms != 0 && now_ms - _last_ms < period) return;
        _last_ms = now_ms;

        size_t base_n = 0;
        const TelemetryField* base = _base_fields(base_n);
        _publish_table("system", base, base_n, (void*)_sys);   // library status
        _publish_table(nullptr, _site, _site_count, _site_ctx); // site computed
        _publish_scalars();                                     // site scalars
    }

    void MqttSender::_publish_table(const char* group, const TelemetryField* table,
                                       size_t count, void* ctx) {
        if (table == nullptr) return;
        char payload[64];
        for (size_t i = 0; i < count; ++i) {
            const TelemetryField& f = table[i];
            if (f.name == nullptr || f.format == nullptr) continue;
            if (!f.format(payload, sizeof(payload), ctx)) continue;   // skipped
            _emit(group, f.name, payload);
        }
    }

    void MqttSender::_publish_scalars() {
        if (_groups == nullptr || _io == nullptr) return;
        char payload[64];
        for (size_t gi = 0; gi < _group_count; ++gi) {
            const Vars::VarGroup& g = _groups[gi];
            for (size_t i = 0; i < g.count; ++i) {
                Vars::Var h = _io->var(g.refs[i]);
                if (h.meta()->name == nullptr) continue;
                // Publish only what has actually been read: a default value on
                // the broker is indistinguishable from a measurement, and a
                // subscriber cannot tell the sensor died.
                if (!h.valid()) continue;
                const char* out = Vars::var_format(*_io, h, payload, sizeof(payload));
                _emit(g.topic, h.meta()->name, out);
            }
        }
    }

    void MqttSender::_emit(const char* group, const char* name, const char* payload) {
        char topic[96];
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
