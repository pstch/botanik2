// REVIEW_35: DONE
#ifndef ALARMS_NOTIFIER_H
#define ALARMS_NOTIFIER_H

/**
 * @file notifier.h
 * @brief MqttAlarmNotifier - the MQTT transport for alarm transitions.
 *
 * Kept out of alarms.h so the alarm MODEL depends on nothing but the variable
 * table, and a consumer of AlarmController does not pull in the network config.
 */

#include <cstdio>

#include "libbotanik/config/net.h"

#include "libbotanik/alarms/alarms.h"
#include "libbotanik/alarms/publisher.h"

namespace Botanik { namespace Alarms {

    /** @brief AlarmNotifier that formats a transition as an MQTT topic/payload.
     *
     *  Publishes "ON"/"OFF"/"FLAP" at topic
     *  `<prefix>/<MQTT_ALARM_TOPIC>/<variable name>`. */
    class MqttAlarmNotifier : public AlarmNotifier {
    private:
        /** @name State
         *  @{ */
        const char* _prefix;   ///< topic root, e.g. "botanik/greenhouse"
        Publisher*  _pub;      ///< transport (null = no-op)
        /** @} */

    public:
        /** @brief Construct a new notifier with the given @p prefix MQTT topic root. */
        explicit MqttAlarmNotifier(const char* prefix) : _prefix(prefix), _pub(nullptr) {}

        /** @brief Wire the transport (called once at firmware setup). */
        void set_publisher(Publisher* pub) { _pub = pub; }

        void notify_on(const AlarmCondition& a)   override { _emit(a, "ON"); }
        void notify_off(const AlarmCondition& a)  override { _emit(a, "OFF"); }
        void notify_flap(const AlarmCondition& a) override { _emit(a, "FLAP"); }

    private:
        /** @brief Build an MQTT entry and publish it. */
        void _emit(const AlarmCondition& a, const char* state) {
            if (_pub == nullptr) return;
            const char* leaf = a.name();
            char topic[Config::MQTT_TOPIC_LEN];
            snprintf(topic, sizeof(topic), "%s/%s/%s", _prefix,
                     Config::MQTT_ALARM_TOPIC, leaf ? leaf : "?");
            _pub->publish(topic, state);
        }
    };

}}  // namespace Botanik::Alarms

#endif // ALARMS_NOTIFIER_H
