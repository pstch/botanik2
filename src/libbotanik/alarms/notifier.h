#ifndef ALARMS_NOTIFIER_H
#define ALARMS_NOTIFIER_H

/**
 * @file notifier.h
 * @brief MqttAlarmNotifier - draft MQTT plumbing for alarm notifications.
 *
 * Formats a retained-style topic + payload per alarm transition and hands it to
 * publish(). The transport is not wired yet (MQTT still reads N/A on the status
 * page): publish() forwards to stub transport methods that do nothing for now,
 * so the path is exercised without touching the log. Swap their bodies for a
 * real client once MQTT lands.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Alarms {

    /** @brief Draft MQTT notifier: builds topic/payload, hands it to publish(). */
    class MqttAlarmNotifier : public AlarmNotifier {
    private:
        /** @name State
         *  @{ */
        const char* _prefix;   // topic prefix, e.g. "botanik/site1"
        /** @} */

    public:
        /** @brief @p prefix is the MQTT topic root. */
        MqttAlarmNotifier(const char* prefix)
            : _prefix(prefix ? prefix : "botanik") {}

        void notify_on(const AlarmCondition& a)  override { _emit(a, true); }
        void notify_off(const AlarmCondition& a) override { _emit(a, false); }

    private:
        /** @brief Build "<prefix>/alarms/<CODE>" + an ON/OFF payload and publish. */
        void _emit(const AlarmCondition& a, bool on) {
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/alarms/%s", _prefix,
                     a.code ? a.code : "?");
            publish(topic, on ? "ON" : "OFF");
        }

        /** @brief TODO: hand off to the MQTT client. For now, route through the
         *  stub transport so the notification path runs end to end. */
        virtual void publish(const char* topic, const char* payload) {
            if (!_connect()) return;
            _send(topic, payload);
        }

        /** @name Stub transport
         *  No-op placeholders standing in for a real MQTT client.
         *  @{ */
        /** @brief Ensure a session exists. Stub: always "connected". */
        virtual bool _connect() { return true; }

        /** @brief Hand a retained-style message to the broker. Stub: drops it. */
        virtual void _send(const char* topic, const char* payload) {
            (void)topic;
            (void)payload;
        }
        /** @} */
    };

}}

#endif // ALARMS_NOTIFIER_H
