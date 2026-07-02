// REVIEW_35: DONE
#ifndef LIBBOTANIK_CORE_CONTEXT_H
#define LIBBOTANIK_CORE_CONTEXT_H

/**
 * @file context.h
 * @brief SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads.
 */

// Declare pointer types for context members
namespace Botanik { namespace Scheduler { struct SchedulerContext;  } }
namespace Botanik { namespace PlcIO     { struct PlcSyncContext;    } }
namespace Botanik { namespace Network   {  class NetworkHandler;    } }
namespace Botanik { namespace Network   {  class NtpClient;         } }
namespace Botanik { namespace Network   {  class MqttClient;        } }

namespace Botanik {

    /** @brief Aggregate of subsystems context struct and pointers to network components. 
     * 
     * Used for reporting commands, for pages that display metrics, etc. */
    struct SystemContext {

        Scheduler::SchedulerContext*   scheduler = nullptr; ///< Performance snapshots
        PlcIO::PlcSyncContext*         plc       = nullptr; ///< PLC link status
        const Network::NetworkHandler* network   = nullptr; ///< [optional] IP config handler
        const Network::NtpClient*      ntp       = nullptr; ///< [optional] SNTP client
        const Network::MqttClient*     mqtt      = nullptr; ///< [optional] MQTT client

        /** @brief Construct a new system context from pointers to subsystem structs
         *  or pointers to subsystem components. 
         *  @note Any can be null when the subsystem is absent. */
        SystemContext(Scheduler::SchedulerContext* sched = nullptr,
                      PlcIO::PlcSyncContext*        plc_ctx = nullptr,
                      const Network::NetworkHandler* net = nullptr,
                      const Network::NtpClient*      ntp_c = nullptr,
                      const Network::MqttClient*     mqtt_c = nullptr)
            : scheduler(sched), plc(plc_ctx), network(net), ntp(ntp_c), mqtt(mqtt_c) {}
    };

}

#endif // LIBBOTANIK_CORE_CONTEXT_H
