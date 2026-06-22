#ifndef ALARMS_NOTIFIER_H
#define ALARMS_NOTIFIER_H

/**
 * @file notifier.h
 * @brief MqttAlarmNotifier - draft MQTT plumbing for alarm notifications.
 *
 * Formats a retained-style topic + payload per alarm transition and hands it to
 * publish(). Transport not wired yet: publish() forwards to no-op stubs, so the path
 * runs without touching the log. Swap the stub bodies for a real client once MQTT lands.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/alarms/alarms.h"

namespace Botanik { namespace Alarms {

    /** @brief Draft MQTT notifier: builds topic/payload, hands it to publish(). */
    class MqttAlarmNotifier : public AlarmNotifier {
    private:
        const char* _prefix;   // topic prefix, e.g. "botanik/site1"

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

        /** @brief TODO: hand off to the MQTT client. For now routes through the stubs. */
        virtual void publish(const char* topic, const char* payload) {
            if (!_connect()) return;
            _send(topic, payload);
        }

        /** @name Stub transport (no-op placeholders for a real MQTT client)
         *  @{ */
        virtual bool _connect() { return true; }   // stub: always "connected"

        virtual void _send(const char* topic, const char* payload) {   // stub: drops it
            (void)topic;
            (void)payload;
        }
        /** @} */
    };

}}

#endif // ALARMS_NOTIFIER_H
