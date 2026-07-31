// REVIEW_35: TODO
#ifndef NETWORK_TELEMETRY_H
#define NETWORK_TELEMETRY_H

/**
 * @file telemetry.h
 * @brief MqttSender - periodic publication of field values over MQTT.
 *
 * Publishes named values under "<prefix>/<root>/<group>/<field>" every
 * Config::MQTT_TELEMETRY_PERIOD_MS while the MqttClient is connected (root =
 * Config::MQTT_TELEMETRY_TOPIC, prefix supplied at construction). Each cycle
 * publishes the library base set (uptime, CPU load/jitter, stack - read from
 * the SystemContext) under "system", then every site variable group registered
 * through set_site_scalar_groups(), resolved against the variable model.
 *
 * Non-blocking: a cycle that the link can't absorb is simply skipped (the next
 * one retries); nothing is queued.
 */

#include <cstddef>
#include <cstdint>

#include "../../libbotanik_config.h"
#include "../core/context.h"
#include "mqtt.h"
#include "../vars/access.h"   // VarGroup / VarRef + resolve/format

namespace Botanik { namespace Network {

    /** @brief One telemetry datapoint: a topic leaf @p name and a @p format
     *  callback that writes its payload into @p buf (cap bytes). @p format
     *  returns false to skip publishing this field this cycle (e.g. no data
     *  yet). @p ctx is the opaque pointer registered with the field's table. */
    struct TelemetryField {
        const char* name;
        bool (*format)(char* buf, size_t cap, void* ctx);
    };

    // Vars::VarGroup / VarRef (the status-variable descriptor
    // table) live in core/scalar.h so non-network consumers (the shell's
    // `status` command) can read them without the MqttSender. Published here.

    /** @brief Periodic MQTT telemetry publisher (see file header). */
    class MqttSender {
        /** @name State
         *  @{ */

        /** @brief Transport (publishes only while connected()). */
        MqttClient*           _mqtt;
        /** @brief Topic prefix ("<prefix>/<root>/<field>"). */
        const char*           _prefix;
        /** @brief Live system status the base fields read. */
        const SystemContext*  _sys;
        /** @brief Site variable groups (set_site_scalar_groups), each published
         *  under its own sub-topic, resolved against _io; null until a site
         *  registers them. */
        const Vars::VarGroup* _groups       = nullptr;
        size_t                _group_count  = 0;
        const Vars::Io*       _io           = nullptr;
        /** @brief Last publish cycle (uptime ms); 0 = none yet. */
        uint64_t              _last_ms     = 0;

        /** @} */

    public:
        /** @brief @p mqtt is the transport, @p sys the live status read by the
         *  base fields, @p prefix the topic root segment before the field. */
        MqttSender(MqttClient* mqtt, const SystemContext* sys, const char* prefix)
            : _mqtt(mqtt), _prefix(prefix), _sys(sys) {}

        /** @brief Register the site's variable groups, each published under its
         *  own sub-topic and rendered generically by resolving its VarRefs
         *  against @p io. Both must outlive the firmware. Pass count 0 (or
         *  null) to clear. */
        void set_site_scalar_groups(const Vars::VarGroup* groups, size_t count,
                                    const Vars::Io* io) {
            _groups      = groups;
            _group_count = (groups != nullptr) ? count : 0;
            _io          = io;
        }

        /** @brief Publish every base + site field once
         *  Config::MQTT_TELEMETRY_PERIOD_MS has elapsed and the link is
         *  connected. No-op when telemetry is disabled (period 0) or the period
         *  has not elapsed. */
        void step(uint64_t now_ms);

    private:
        /** @brief Publish each field of @p table under @p group; ctx is the
         *  formatter context. */
        void _publish_table(const char* group, const TelemetryField* table,
                            size_t count, void* ctx);
        /** @brief Publish every registered site variable group, each under its
         *  own sub-topic, resolved against _io. */
        void _publish_scalars();
        /** @brief Build the field topic ("<prefix>/<root>[/<group>]/<name>") and
         *  publish @p payload (best-effort). */
        void _emit(const char* group, const char* name, const char* payload);
        /** @brief The library base field table (ctx = the SystemContext). */
        static const TelemetryField* _base_fields(size_t& count);
    };

}}

#endif // NETWORK_TELEMETRY_H
