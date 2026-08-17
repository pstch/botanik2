// REVIEW_35: DONE
#ifndef LIBBOTANIK_CORE_CONTEXT_H
#define LIBBOTANIK_CORE_CONTEXT_H

/** @file context.h
 *  @brief SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads. */

// Include library configuration
#include "libbotanik/config/base.h"

// Forward-declare pointer types for context members
namespace Botanik { namespace Scheduler { struct SchedulerContext;  } }
namespace Botanik { namespace Tasks     {  class Registry;          } }
namespace Botanik { namespace Vars      {  class Bank;              } }


#if BOTANIK_FEATURE_NETWORK
namespace Botanik { namespace Network   {  class NetworkHandler;    } }
#endif

#if BOTANIK_FEATURE_NTP
namespace Botanik { namespace Network   {  class NtpClient;         } }
#endif

#if BOTANIK_FEATURE_MQTT
namespace Botanik { namespace Network   {  class MqttClient;        } }
#endif

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
namespace Botanik { namespace Modbus    {  class TcpSlave;          } }
#endif
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
namespace Botanik { namespace Modbus    {  class RtuClient;         } }
#endif

namespace Botanik {

    /** @brief Aggregate of subsystem context structs and pointers to the
     *  optional network components; used by reporting commands and metric pages.
     *  Every member may be null when its subsystem is absent at runtime.
     * @warning Consumers should be gated under the matching `#if BOTANIK_FEATURE_*`. */
    struct SystemContext {

        Scheduler::SchedulerContext*   scheduler = nullptr; ///< Performance snapshots

        /** @brief The task table, with the per-task cost samples: the
         *  attribution the beat's CPU figure lacks, and what `tasks` reports.
         *  Const, because the registry is frozen once the loop starts. */
        Tasks::Registry*               tasks     = nullptr;

        /** @brief The variables - THE object to get, set and override anything
         *  the firmware exposes. Non-const: `set` and `override` write through
         *  it. Consumers reach it here, so there is one wiring point rather than
         *  one per consumer. */
        Vars::Bank*                    vars      = nullptr;

#if BOTANIK_FEATURE_NETWORK
        const Network::NetworkHandler* network   = nullptr; ///< IP config handler
#endif

#if BOTANIK_FEATURE_NTP
        const Network::NtpClient*      ntp       = nullptr; ///< SNTP client
#endif

#if BOTANIK_FEATURE_MQTT
        const Network::MqttClient*     mqtt      = nullptr; ///< MQTT client
#endif

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER
        /** @brief The Modbus TCP slave. The component rather than a context
         *  struct, like the network handlers above: SlaveContext is computed on
         *  demand, so there is no long-lived struct to point at. */
        const Modbus::TcpSlave*        modbus_tcp = nullptr;
#endif

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
        /** @brief The field-bus master. Its plan, its peers and its counters are
         *  the only view anyone has of what the RS-485 segment is doing. */
        const Modbus::RtuClient*       modbus_rtu = nullptr;
#endif

    };

}

#endif // LIBBOTANIK_CORE_CONTEXT_H
