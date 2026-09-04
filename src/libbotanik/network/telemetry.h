#ifndef NETWORK_TELEMETRY_H
#define NETWORK_TELEMETRY_H

/**
 * @file telemetry.h
 * @brief MqttSender - periodic publication of field values over MQTT.
 *
 * Publishes named values under "<prefix>/<root>/<group>/<field>" every
 * TelemetryConfig::period_ms while the MqttClient is connected (root =
 * Config::MQTT_TELEMETRY_TOPIC, prefix supplied at construction). Each cycle
 * publishes the library base set (uptime, CPU load/jitter, stack, from the
 * SystemContext) under "system", then the variable table registered through
 * set_variables(), grouped by the prefix of each name.
 *
 * Non-blocking: a cycle the link can't absorb is skipped and the next one
 * retries; nothing is queued.
 */

#include "libbotanik/core/component.h"
#include <cstdint>

#include "libbotanik/config/identity.h"
#include "libbotanik/config/net.h"

#include "libbotanik/core/context.h"
#include "libbotanik/network/mqtt.h"
#include "libbotanik/scheduler/scheduler.h"
#include "libbotanik/vars/access.h"

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Network {

    /** @brief How often the base fields go to the broker, and whether it keeps
     *  them. period_ms 0 disables telemetry outright. */
    struct TelemetryConfig {
        uint32_t period_ms = 30000;
        bool     retain    = true;
    };

    // Vars::var_published / var_group (what goes out, and under what) live in
    // vars/access.h, so non-network consumers (the MONITOR page) read them
    // without the MqttSender.

    /** @brief Periodic MQTT telemetry publisher (see file header). */
    class MqttSender : public Botanik::Component {
    public:
        /** @brief One task, PHASE_SERVICE: publish what is due this beat. */
        void register_tasks() override;

    private:

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
        /** @brief The lifecycle, borrowed from the scheduler; null publishes. */
        const Scheduler::RunState* _state = nullptr;
        /** @brief _cursor value meaning "no sweep in progress". */
        static const uint16_t SWEEP_IDLE = 0xFFFF;
        /** @brief Last publish cycle (uptime ms); 0 = none yet. */
        uint64_t              _last_ms     = 0;
        /** @brief Where the current sweep has reached in the variable table.
         *  SWEEP_IDLE when none is in progress - a sentinel rather than a
         *  companion bool, because "sweeping" is exactly "the cursor is inside
         *  the table" and two fields encoding one fact can disagree.
         *
         *  A sweep hands out MQTT_TELEMETRY_PER_STEP topics per beat and carries
         *  the cursor, so the per-beat work stays bounded (config/net.h has the
         *  cost of an unpaced sweep). The cadence starts on time and lands over
         *  the beats that follow. */
        uint16_t              _cursor      = SWEEP_IDLE;

        /** @} */

    public:
        /** @brief Adopt @p cfg; the next sweep runs at it. */
        void set_config(const TelemetryConfig& cfg);

        /** @brief @p mqtt is the transport, @p sys the live status read by the
         *  base fields, @p prefix the topic root segment before the field. */
        MqttSender(const SetupContext& ctx, MqttClient* mqtt,
                   const SystemContext* sys, const char* prefix)
            : Component(ctx, "telemetry"),
              _mqtt(mqtt), _prefix(prefix), _sys(sys) {}

        /** @brief Register the variable table to publish from. It must outlive
         *  the firmware; null clears. Every published row goes out under
         *  "<prefix>/telemetry/<group>/<name>", where the group is the prefix of
         *  the name - there is no per-site list to register or to forget. */
        void set_variables(const Vars::Bank* io) { _io = io; }

        /** @brief Borrow the lifecycle. Nothing is published outside RUN. */
        void set_run_state(const Scheduler::RunState* s) { _state = s; }

        /** @brief Publish every base + site field once
         *  TelemetryConfig::period_ms has elapsed and the link is
         *  connected. No-op when telemetry is disabled (period 0) or the period
         *  has not elapsed. */
        void on_service(uint64_t now_ms) override;

    private:
        /** @brief Publish the library base fields under "system". */
        void _publish_base(const SystemContext* sys);
        /** @brief Publish every registered site variable group, each under its
         *  own sub-topic, resolved against _io. */
        void _publish_scalars();
        /** @brief Whether a sweep is mid-flight. Derived, not stored. */
        bool _sweeping() const { return _cursor != SWEEP_IDLE; }
        /** @name Taken on the mqtt_ commit
         *  Not read live: these belong to a group that commits as one, so
         *  landing the cadence ahead of the host and port it was staged with
         *  would defeat the serial.
         *  @{ */
        TelemetryConfig _cfg;
        /** @} */
        /** @brief Build the field topic ("<prefix>/<root>[/<group>]/<name>") and
         *  publish @p payload (best-effort). */
        void _emit(const char* group, const char* name, const char* payload);
    };

}}

#endif // NETWORK_TELEMETRY_H
