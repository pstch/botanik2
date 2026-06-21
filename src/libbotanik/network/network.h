#ifndef NETWORK_NETWORK_H
#define NETWORK_NETWORK_H

/**
 * @file network.h
 * @brief Network and time-sync handlers - PLC-config-driven state machines for
 *        bringing up the network device and running an NTP client (WIP).
 */

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"

namespace Botanik { namespace Network {

    // TODO: write NetworkHandler
    //
    // - This class should be a state machine, implemented in a way similar to the
    //   other state machines in the codebase. It should have at least a RUNNING state,
    //   for when the network is operational, a SETUP state when we are still setting up
    //   the network (maybe waiting for DHCP, or for the L2 device to be available),
    //   probably even an ERROR state when no network can be setup (in that state, we'd
    //   retry after a timeout).
    // - The configuration now lives in the shared region: PlcSyncContext::config()
    //   returns the generic PlcOutputConfig_t (plcio/config.h), so the handler reaches
    //   its block as `_ctx->config()->net`. Watch `net.serial`: latch it in
    //   _last_config_serial and, when it increases, transition to SETUP and (re)bring
    //   the device up from net.dhcp / net.ip / net.netmask / net.gateway / net.dns1/2.
    // - Addresses are text (HostStr_t): a hostname or a dotted-decimal IPv4 literal.
    //   A literal is parsed directly; a hostname needs the DNS resolver (see below),
    //   which must not block - so SETUP drives it asynchronously across ticks.

    /** @brief Brings the network device up from PLC-published configuration,
     *  as a state machine mirroring the PLC sync handlers. WIP - see the TODO above. */
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

    // - This class should be a state machine, very similar to NetworkHandler, except
    //   that it is used as an NTP client (and should update the system's RTC). It reads
    //   its block via `_ctx->config()->ntp` (plcio/config.h) and watches `ntp.serial`:
    //   on a bump it re-applies ntp.enabled / ntp.server1/2 / ntp.poll_s. The servers
    //   are HostStr_t (hostname or IPv4 literal), so a hostname goes through the DNS
    //   resolver below before the NTP exchange.

    /** @brief NTP client that disciplines the system RTC from PLC-published
     *  configuration, as a state machine like NetworkHandler. WIP - see the TODO below. */
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

    // TODO: write Resolver (parallel non-blocking DNS)
    //
    // The PLC config carries addresses as text (HostStr_t): a hostname or a dotted-
    // decimal IPv4 literal. Several consumers need to turn one into an IP without
    // blocking the main loop: NetworkHandler (gateway/DNS are literals, but consistency
    // is nice), TimeSyncHandler (NTP servers), and the future MQTT client (broker). So
    // a shared, non-blocking resolver belongs here rather than duplicated per consumer.
    //
    // Shape (to design): a step()-driven component that owns a small fixed table of
    // in-flight lookups. A consumer submits a HostStr_t and gets a handle/slot; the
    // resolver first tries a literal parse (instant success), else drives an async DNS
    // query and reports DONE(ip) / FAILED / PENDING per tick. Several lookups proceed
    // in parallel (one slot each) so two NTP servers + the broker don't serialize. No
    // call ever blocks; everything advances one bounded step at a time like the rest of
    // the codebase. Upstream DNS comes from the net block's dns1/dns2.

}}

#endif // NETWORK_NETWORK_H
