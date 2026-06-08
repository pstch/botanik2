/**
 * @file network.cpp
 * @brief NetworkHandler and TimeSyncHandler implementations (stubs; WIP).
 */
#include "libbotanik/network/network.h"

namespace Botanik { namespace Network {

    // Stub constructors — members are wired up when these handlers are implemented
    // (see TODO.md and the design notes in network.h).
    NetworkHandler::NetworkHandler()
        : _logger(nullptr), _ctx(nullptr), _last_config_serial(0) {}

    TimeSyncHandler::TimeSyncHandler()
        : _logger(nullptr), _ctx(nullptr), _last_config_serial(0) {}

    void NetworkHandler::step() {

    }

    void TimeSyncHandler::step() {

    }


}}