// REVIEW_35: TODO
#ifndef ALARMS_PUBLISHER_H
#define ALARMS_PUBLISHER_H

/**
 * @file publisher.h
 * @brief The file contains the transport interface for external alarm notifications, for
 * example through MQTT.
 *
 * Used to decouple alarm notification handlin (in alarms/) 
 * from the network stack (in network/).
 * 
 */

namespace Botanik { namespace Alarms {

    /** @brief Abstract alarm publish sink */
    struct Publisher {

        /** @brief Construct a new publisher. */
        virtual ~Publisher() {}

        /** @brief Publish @p payload under @p topic. Returns false if it could
         *  not be sent (e.g. not connected); the caller may drop or retry. */
        virtual bool publish(const char* topic, const char* payload) = 0;

    };

}}

#endif // ALARMS_PUBLISHER_H
