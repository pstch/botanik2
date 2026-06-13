/**
 * @file network.cpp
 * @brief NetworkHandler and TimeSyncHandler implementations (stubs; WIP).
 */
#include "libbotanik/network/network.h"

namespace Botanik { namespace Network {

    // NetworkHandler
    // ------------------------------------------------------------------------

    // Stub constructor — members are wired up when this handler is implemented
    // (see TODO.md and the design notes in network.h).
    NetworkHandler::NetworkHandler()
        : _logger(nullptr), _ctx(nullptr), _last_config_serial(0) {}

    void NetworkHandler::step() {

    }

    // TimeSyncHandler
    // ------------------------------------------------------------------------

    // Stub constructor — members are wired up when this handler is implemented
    // (see TODO.md and the design notes in network.h).
    TimeSyncHandler::TimeSyncHandler()
        : _logger(nullptr), _ctx(nullptr), _last_config_serial(0) {}

    void TimeSyncHandler::step() {

    }

}}
