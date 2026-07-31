// REVIEW_35: TODO
#ifndef NETWORK_TELEMETRY_H
#define NETWORK_TELEMETRY_H

/**
 * @file telemetry.h
 * @brief MqttSender - periodic publication of field values over MQTT.
 *
 * Publishes a set of named values under "<prefix>/<root>/<field>" every
 * Config::MQTT_TELEMETRY_PERIOD_MS while the MqttClient is connected (root =
 * Config::MQTT_TELEMETRY_TOPIC, prefix supplied at construction). Two field
 * sets are published each cycle: a library base set (uptime, CPU load/jitter,
 * stack, PLC link - read from the SystemContext) and the site's own set,
 * appended via set_site_fields() from the BotanikFirmware register_telemetry()
 * hook. A field is just a name plus a formatter taking an opaque ctx, so each
 * reads whatever data it needs - the SystemContext for the base set, the site
 * PLC payload for a site set.
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
        /** @brief Site field table (set_site_fields), tried after the base
         *  set; null until a site registers one. */
        const TelemetryField* _site       = nullptr;
        size_t                _site_count  = 0;
        void*                 _site_ctx    = nullptr;
        /** @brief Site variable groups (set_site_scalar_groups), each published
         *  under its own sub-topic and rendered generically off @ref
         *  _scalar_base; null until a site registers them. */
        const Vars::VarGroup* _groups       = nullptr;
        size_t                _group_count  = 0;
        const Vars::Bank*       _io           = nullptr;
        /** @brief Last publish cycle (uptime ms); 0 = none yet. */
        uint64_t              _last_ms     = 0;

        /** @} */

    public:
        /** @brief @p mqtt is the transport, @p sys the live status read by the
         *  base fields, @p prefix the topic root segment before the field. */
        MqttSender(MqttClient* mqtt, const SystemContext* sys, const char* prefix)
            : _mqtt(mqtt), _prefix(prefix), _sys(sys) {}

        /** @brief Register the site's extra fields, published after the base
         *  set each cycle. @p table must outlive the firmware; @p ctx is passed
         *  to each formatter. Pass count 0 (or null) to clear. */
        void set_site_fields(const TelemetryField* table, size_t count, void* ctx) {
            _site       = table;
            _site_count = (table != nullptr) ? count : 0;
            _site_ctx   = ctx;
        }

        /** @brief Register the site's variable groups, each published under its
         *  own sub-topic and rendered generically by resolving its VarRefs
         *  against @p io. Both must outlive the firmware. Pass count 0 (or
         *  null) to clear. */
        void set_site_scalar_groups(const Vars::VarGroup* groups, size_t count,
                                    const Vars::Bank* io) {
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
        /** @brief Publish each field of @p table under @p group (null = no group
         *  sub-topic); ctx is the formatter context. */
        void _publish_table(const char* group, const TelemetryField* table,
                            size_t count, void* ctx);
        /** @brief Publish every registered site scalar group, each under its own
         *  sub-topic, rendered generically off @ref _scalar_base. */
        void _publish_scalars();
        /** @brief Build the field topic ("<prefix>/<root>[/<group>]/<name>") and
         *  publish @p payload (best-effort). */
        void _emit(const char* group, const char* name, const char* payload);
        /** @brief The library base field table (ctx = the SystemContext). */
        static const TelemetryField* _base_fields(size_t& count);
    };

}}

#endif // NETWORK_TELEMETRY_H
