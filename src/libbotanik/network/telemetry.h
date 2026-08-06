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
 * through set_variables(), grouped by the prefix of each name.
 *
 * Non-blocking: a cycle that the link can't absorb is simply skipped (the next
 * one retries); nothing is queued.
 */

#include <cstdint>                      // uint16_t/uint64_t

#include "libbotanik_config.h"          // Config::MQTT_TOPIC_PREFIX

#include "libbotanik/core/context.h"    // SystemContext
#include "libbotanik/network/mqtt.h"    // Network::MqttClient
#include "libbotanik/vars/access.h"     // Vars::var_published / var_format

namespace Botanik { namespace Network {

    // Vars::var_published / var_group (what goes out, and under what) live in
    // vars/access.h, so non-network consumers (the MONITOR page)
    // read them without the MqttSender. Published here.

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
        /** @brief The variable table this publishes from; null until a site
         *  registers it. Which rows go out, and under what sub-topic, are
         *  properties of the rows themselves - see Vars::var_published() and
         *  Vars::var_group(). */
        const Vars::Bank*     _io          = nullptr;
        /** @brief Last publish cycle (uptime ms); 0 = none yet. */
        /** @brief _cursor value meaning "no sweep in progress". */
        static const uint16_t SWEEP_IDLE = 0xFFFF;
        uint64_t              _last_ms     = 0;
        /** @brief Where the current sweep has reached in the variable table.
         *  SWEEP_IDLE when none is in progress - a sentinel rather than a
         *  companion bool, because "sweeping" is exactly "the cursor is inside
         *  the table" and two fields encoding one fact can disagree.
         *
         *  A sweep used to publish every eligible row in ONE activation: 38-80
         *  topics, each its own snprintf and its own tcp_send, for 0.8-1.9 ms
         *  inside a 10 ms beat - a 100-300x spike over steady state, once per
         *  cadence. MQTT 3.1.1 has no multi-topic PUBLISH, so the fix is not to
         *  batch but to PACE: hand out MQTT_TELEMETRY_PER_STEP topics per beat
         *  and carry the cursor, the way the log rows and the terminal's byte
         *  budget already bound their own work per tick.
         *
         *  The cadence still starts on time; it just no longer lands all at
         *  once. A value published a few beats into a 30 s sweep is not less
         *  true. */
        uint16_t              _cursor      = SWEEP_IDLE;

        /** @} */

    public:
        /** @brief @p mqtt is the transport, @p sys the live status read by the
         *  base fields, @p prefix the topic root segment before the field. */
        MqttSender(MqttClient* mqtt, const SystemContext* sys, const char* prefix)
            : _mqtt(mqtt), _prefix(prefix), _sys(sys) {}

        /** @brief Register the variable table to publish from. It must outlive
         *  the firmware; null clears. Every published row goes out under
         *  "<prefix>/telemetry/<group>/<name>", where the group is the prefix of
         *  the name - there is no per-site list to register or to forget. */
        void set_variables(const Vars::Bank* io) { _io = io; }

        /** @brief Publish every base + site field once
         *  Config::MQTT_TELEMETRY_PERIOD_MS has elapsed and the link is
         *  connected. No-op when telemetry is disabled (period 0) or the period
         *  has not elapsed. */
        void step(uint64_t now_ms);

    private:
        /** @brief Publish the library base fields under "system". */
        void _publish_base(const SystemContext* sys);
        /** @brief Publish every registered site variable group, each under its
         *  own sub-topic, resolved against _io. */
        void _publish_scalars();
        /** @brief Whether a sweep is mid-flight. Derived, not stored. */
        bool _sweeping() const { return _cursor != SWEEP_IDLE; }
        /** @brief Build the field topic ("<prefix>/<root>[/<group>]/<name>") and
         *  publish @p payload (best-effort). */
        void _emit(const char* group, const char* name, const char* payload);
    };

}}

#endif // NETWORK_TELEMETRY_H
