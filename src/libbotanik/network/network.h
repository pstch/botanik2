#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief Network and time-sync handlers — PLC-config-driven state machines for
 *        bringing up the network device and running an NTP client (WIP).
 */

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace Network {

    // TODO: write NetworkHandler
    // 
    // - This class should be a state machine, implemented in a way similar to the other state machines in the codebase. It should have at least a RUNNING state, for when the network is operational, a SETUP state when we are still setting up the network (maybe waiting for DHCP, or for the L2 device to be available), probably even an ERROR state when no network can be setup (in that state, we'd retry after a timeout). The state machine should watch for the "network configuration serial number", a field exposed from the PLC (through the sync handler, we probably need to add a new "config" member to PlcOutput_t for this stuff so that we can reach it through PlcSyncContext). When the serial number increases, we transition to the SETUP state and use the configuration data to setup the network device.

    /** @brief Brings the network device up from PLC-published configuration,
     *  as a state machine mirroring the PLC sync handlers. WIP — see the TODO above. */
    class NetworkHandler {
    private:
        /** @name State
         *  @{ */

        /** @brief Logger for network events (may be null). */
        Logs::Logger*               _logger;
        /** @brief PLC sync context exposing the network configuration. */
        PlcIO::PlcSyncContext*      _ctx;
        /** @brief Last seen network-configuration serial number. */
        uint32_t                    _last_config_serial;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct the network handler. */
        NetworkHandler();
        /** @brief Advance the network state machine; call once per tick. */
        void step();

        /** @} */
    };

    // - This class should be a state machine, very similar to NetworkHandler, except that it is used as an NTP client (and should update the system's RTC)

    /** @brief NTP client that disciplines the system RTC from PLC-published
     *  configuration, as a state machine like NetworkHandler. WIP — see the TODO below. */
    class TimeSyncHandler {
    private:
        /** @name State
         *  @{ */

        /** @brief Logger for time-sync events (may be null). */
        Logs::Logger*               _logger;
        /** @brief PLC sync context exposing the NTP configuration. */
        PlcIO::PlcSyncContext*      _ctx;
        /** @brief Last seen NTP-configuration serial number. */
        uint32_t                    _last_config_serial;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        /** @brief Construct the time-sync handler. */
        TimeSyncHandler();
        /** @brief Advance the time-sync state machine; call once per tick. */
        void step();

        /** @} */
    };
    // TODO: write TimeSyncHandler
    //         (handles NTP configuration)

}}

#endif // NETWORK_NETWORK_H
