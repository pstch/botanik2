#ifndef ALERTS_NOTIFIER_H
#define ALERTS_NOTIFIER_H

/**
 * @file notifier.h
 * @brief MqttAlertNotifier - draft MQTT plumbing for alert notifications.
 *
 * Formats a retained-style topic + payload per alert transition and hands it to
 * publish(). The transport is not wired yet (MQTT still reads N/A on the status
 * page): publish() is a stub that traces the message to the log so the path is
 * observable. Swap its body for a real client once MQTT lands.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "libbotanik/alerts/alerts.h"
#include "libbotanik/logs/logger.h"

namespace Botanik { namespace Alerts {

    /** @brief Draft MQTT notifier: builds topic/payload, traces via publish(). */
    class MqttAlertNotifier : public AlertNotifier {
    private:
        /** @name State
         *  @{ */
        const char*   _prefix;   // topic prefix, e.g. "botanik/site1"
        Logs::Logger* _logger;   // trace sink for the draft (may be null)
        /** @} */

    public:
        /** @brief @p prefix is the MQTT topic root; @p logger traces unsent drafts. */
        MqttAlertNotifier(const char* prefix, Logs::Logger* logger = nullptr)
            : _prefix(prefix ? prefix : "botanik"), _logger(logger) {}

        void notify_on(const AlertCondition& a)  override { _emit(a, true); }
        void notify_off(const AlertCondition& a) override { _emit(a, false); }

    private:
        /** @brief Build "<prefix>/alerts/<CODE>" + an ON/OFF payload and publish. */
        void _emit(const AlertCondition& a, bool on) {
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/alerts/%s", _prefix,
                     a.code ? a.code : "?");
            publish(topic, on ? "ON" : "OFF");
        }

        /** @brief TODO: hand off to the MQTT client. For now, trace only so the
         *  notification path is exercised end to end without a transport. */
        virtual void publish(const char* topic, const char* payload) {
            if (_logger) _logger->debug(PlcIO::NETWORK, "mqtt (draft) %s = %s", topic, payload);
        }
    };

}}

#endif // ALERTS_NOTIFIER_H
