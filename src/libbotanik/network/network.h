#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace Network {

    // TODO: write NetworkHandler
    // 
    // - This class should be a state machine, implemented in a way similar to the other state machines in the codebase. It should have at least a RUNNING state, for when the network is operational, a SETUP state when we are still setting up the network (maybe waiting for DHCP, or for the L2 device to be available), probably even an ERROR state when no network can be setup (in that state, we'd retry after a timeout). The state machine should watch for the "network configuration serial number", a field exposed from the PLC (through the sync handler, we probably need to add a new "config" member to PlcOutput_t for this stuff so that we can reach it through PlcSyncContext). When the serial number increases, we transition to the SETUP state and use the confiuration data to setup the network device.
    class NetworkHandler {
    private:
        Logs::Logger*               _logger;
        PlcIO::PlcSyncContext*      _ctx;

        uint32_t                    _last_config_serial;

    public:
        NetworkHandler();
        void step();
    };

    // - This class should be a state machine, very similar to NetworkHandler, except that it is used as an NTP client (and should update the system's RTC)
    class TimeSyncHandler {
    private:
        Logs::Logger*               _logger;
        PlcIO::PlcSyncContext       _ctx;

        uint32_t                    _last_config_serial;
    public:
        TimeSyncHandler();
        void step();
    };
    // TODO: write TimeSyncHandler
    //         (handles NTP configuration)

}}

#endif