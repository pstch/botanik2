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
#include "libbotanik/vars/access.h"

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Network {

    // Vars::var_published / var_group (what goes out, and under what) live in
    // vars/access.h, so non-network consumers (the MONITOR page) read them
    // without the MqttSender.

    /** @brief Periodic MQTT telemetry publisher (see file header). */
    class MqttSender : public Botanik::Component {
    public:
        /** @brief One task, PHASE_SERVICE: publish what is due this beat. */
        void register_tasks() override;

    private:
        /** @brief The registration handler. A static MEMBER, so the cast has
         *  access to the protected hook and stays beside the class. */
        static void _service_task(Component* c, const Tasks::Tick& t);

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
         *  A sweep hands out MQTT_TELEMETRY_PER_STEP topics per beat and carries
         *  the cursor, so the per-beat work stays bounded (config/net.h has the
         *  cost of an unpaced sweep). The cadence starts on time and lands over
         *  the beats that follow. */
        uint16_t              _cursor      = SWEEP_IDLE;

        /** @} */

    public:
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

        /** @brief Publish every base + site field once
         *  Config::MQTT_TELEMETRY_PERIOD_MS has elapsed and the link is
         *  connected. No-op when telemetry is disabled (period 0) or the period
         *  has not elapsed. */
        void on_service(uint64_t now_ms) override;

        /** @brief Resolve the two cadence rows. Here rather than at
         *  set_variables(): bring-up freezes the bank, then registers the
         *  table, then runs this - so the index exists and is final. */
        void on_setup(uint64_t now_ms) override;

    private:
        /** @brief Publish the library base fields under "system". */
        void _publish_base(const SystemContext* sys);
        /** @brief Publish every registered site variable group, each under its
         *  own sub-topic, resolved against _io. */
        void _publish_scalars();
        /** @brief Whether a sweep is mid-flight. Derived, not stored. */
        bool _sweeping() const { return _cursor != SWEEP_IDLE; }
        /** @brief The live publish cadence in ms, from `mqtt_tlm_sec`
         *  (Config::MQTT_TELEMETRY_PERIOD_MS is its factory default). 0
         *  disables telemetry. */
        uint32_t _period_ms() const;
        /** @brief Whether to publish retained, from `mqtt_retain`. */
        bool _retain() const;

        /** @name The two cadence rows, resolved once
         *  Still read LIVE - a change lands on the next sweep, with nothing to
         *  re-initialise. What is cached is the INDEX, not the value: by name
         *  these cost a binary search over the whole table, and _retain() is
         *  asked once per published row. VAR_NONE falls back to Config.
         *  @{ */
        Vars::VarRef _r_period = { Vars::VAR_NONE };
        Vars::VarRef _r_retain = { Vars::VAR_NONE };
        /** @} */
        /** @brief Build the field topic ("<prefix>/<root>[/<group>]/<name>") and
         *  publish @p payload (best-effort). */
        void _emit(const char* group, const char* name, const char* payload);
    };

}}

#endif // NETWORK_TELEMETRY_H
