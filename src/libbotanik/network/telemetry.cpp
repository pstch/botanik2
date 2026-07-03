// REVIEW_35: TODO
/**
 * @file telemetry.cpp
 * @brief MqttTelemetry implementation - the base field set + the publish cycle.
 */
#include "libbotanik/network/telemetry.h"

#include <cstdio>
#include <cstring>

#include "libbotanik/hal/platform.h"
#include "libbotanik/plcio/sync.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/utils/text.h"

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
        bool fmt_plc(char* buf, size_t cap, void* ctx) {
            const SystemContext* s = static_cast<const SystemContext*>(ctx);
            if (s->plc == nullptr) return false;
            const char* str;
            size_t len;   // bytes to copy, NUL included (sizeof the literal)
            switch (s->plc->status()) {
                case PlcIO::PLC_RUNNING: str = "running"; len = sizeof("running"); break;
                case PlcIO::PLC_TIMEOUT: str = "timeout"; len = sizeof("timeout"); break;
                case PlcIO::PLC_FAULT:   str = "fault";   len = sizeof("fault");   break;
                default:                 str = "unknown"; len = sizeof("unknown"); break;
            }
            if (len > cap) len = cap;
            memcpy(buf, str, len);
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
            { "plc",           &fmt_plc        },
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

    namespace {
        // Read a ScalarField's value as a 64-bit integer (sign-extended), from
        // base + offset. memcpy keeps it alignment-safe on the real payload.
        int64_t scalar_read(const void* base, const ScalarField& f) {
            const uint8_t* p = static_cast<const uint8_t*>(base) + f.offset;
            switch (f.type) {
                case SCALAR_U8:
                case SCALAR_ENUM: return *p;
                case SCALAR_U16:  { uint16_t v; memcpy(&v, p, 2); return v; }
                case SCALAR_U32:  { uint32_t v; memcpy(&v, p, 4); return v; }
                case SCALAR_I16:  { int16_t  v; memcpy(&v, p, 2); return v; }
                case SCALAR_I32:  { int32_t  v; memcpy(&v, p, 4); return v; }
            }
            return 0;
        }
    }  // namespace

    void MqttSender::_publish_scalars() {
        if (_groups == nullptr || _scalar_base == nullptr) return;
        char payload[64];
        for (size_t gi = 0; gi < _group_count; ++gi) {
            const ScalarGroup& g = _groups[gi];
            for (size_t i = 0; i < g.count; ++i) {
                const ScalarField& f = g.fields[i];
                if (f.name == nullptr) continue;
                int64_t raw = scalar_read(_scalar_base, f);
                const char* out = payload;
                if (f.type == SCALAR_ENUM) {
                    if (f.str == nullptr) continue;
                    out = f.str((uint8_t)raw);   // static string, point at it directly
                } else if (f.divisor > 1) {
                    Utils::format_float(payload, sizeof(payload), (double)raw / f.divisor, f.decimals);
                } else {
                    Utils::format_int(payload, sizeof(payload), (long long)raw);
                }
                _emit(g.topic, f.name, out);
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
