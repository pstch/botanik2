// REVIEW_35: DONE
#ifndef LIBBOTANIK_CORE_CONTEXT_H
#define LIBBOTANIK_CORE_CONTEXT_H

/** @file context.h
 *  @brief SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads. */

// Include library configuration
#include "../../libbotanik_config.h"

// Forward-declare pointer types for context members
namespace Botanik { namespace Scheduler { struct SchedulerContext;  } }

#if BOTANIK_FEATURE_PLC
namespace Botanik { namespace PlcIO     { struct PlcSyncContext;    } }
#endif

#if BOTANIK_FEATURE_NETWORK
namespace Botanik { namespace Network   {  class NetworkHandler;    } }
#endif

#if BOTANIK_FEATURE_NTP
namespace Botanik { namespace Network   {  class NtpClient;         } }
#endif

#if BOTANIK_FEATURE_MQTT
namespace Botanik { namespace Network   {  class MqttClient;        } }
#endif

namespace Botanik {

    /** @brief Aggregate of subsystem context structs and pointers to the
     *  optional network components; used by reporting commands and metric pages.
     *  Every member may be null when its subsystem is absent at runtime. 
     * @warning Consumers should be gated under the matching `#if BOTANIK_FEATURE_*`. */
    struct SystemContext {

        Scheduler::SchedulerContext*   scheduler = nullptr; ///< Performance snapshots

#if BOTANIK_FEATURE_PLC
        PlcIO::PlcSyncContext*         plc       = nullptr; ///< PLC link status
#endif

#if BOTANIK_FEATURE_NETWORK
        const Network::NetworkHandler* network   = nullptr; ///< IP config handler
#endif

#if BOTANIK_FEATURE_NTP
        const Network::NtpClient*      ntp       = nullptr; ///< SNTP client
#endif

#if BOTANIK_FEATURE_MQTT
        const Network::MqttClient*     mqtt      = nullptr; ///< MQTT client
#endif

    };

}

#endif // LIBBOTANIK_CORE_CONTEXT_H
