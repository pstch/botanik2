#ifndef ALARMS_PUBLISHER_H
#define ALARMS_PUBLISHER_H

/**
 * @file publisher.h
 * @brief Publisher - the transport seam an AlarmNotifier publishes through.
 *
 * Decouples alarm notification (alarms/) from the network stack: AlarmNotifier
 * formats a topic/payload and hands it to a Publisher; MqttClient implements
 * Publisher. alarms/ depends only on this interface, never on network/.
 */

namespace Botanik { namespace Alarms {

    /** @brief Abstract publish sink (e.g. an MQTT client). */
    struct Publisher {
        virtual ~Publisher() {}
        /** @brief Publish @p payload under @p topic. Returns false if it could
         *  not be sent (e.g. not connected); the caller may drop or retry. */
        virtual bool publish(const char* topic, const char* payload) = 0;
    };

}}

#endif // ALARMS_PUBLISHER_H
