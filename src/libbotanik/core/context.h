#ifndef LIBBOTANIK_CORE_CONTEXT_H
#define LIBBOTANIK_CORE_CONTEXT_H

/** @file context.h
 *  @brief SystemContext - one aggregate of the per-subsystem status contexts
 *  the terminal UI reads. */

// Include library configuration
#include "libbotanik/config/base.h"

// Forward-declare pointer types for context members
namespace Botanik { namespace Scheduler { struct SchedulerContext;  } }
namespace Botanik { namespace Tasks     {  class Registry;          } }
namespace Botanik { namespace Vars      {  class Bank;              } }
namespace Botanik { namespace Plc { namespace Runtime { class Plant; } } }


namespace Botanik { namespace Network   {  class NetworkHandler;    } }

namespace Botanik { namespace Network   {  class NtpClient;         } }

namespace Botanik { namespace Network   {  class MqttClient;        } }

namespace Botanik { namespace Modbus    {  class TcpSlave;          } }
namespace Botanik { namespace Modbus    {  class RtuMaster;         } }

namespace Botanik {

    /** @brief Aggregate of the subsystem context structs and of the optional
     *  components, read by the reporting commands and the metric pages. Every
     *  member may be null when its subsystem is absent at runtime.
     *  @note Every pointer is declared whatever the build: a forward-declared
     *  pointer costs nothing, and gating the FIELD would only move the `#if` to
     *  each of its consumers, which already have one. */
    struct SystemContext {

        Scheduler::SchedulerContext*   scheduler = nullptr; ///< Performance snapshots

        /** @brief The task table, with the per-task cost samples: the
         *  attribution the beat's CPU figure lacks, and what `tasks` reports.
         *  Const, because the registry is frozen once the loop starts. */
        Tasks::Registry*               tasks     = nullptr;

        /** @brief The ST plant: the scan roster, and the operator's gate on
         *  each program. There is no Plc::Program behind an ST program, so
         *  what lists them asks here rather than walking the registry. */
        Plc::Runtime::Plant*           plant     = nullptr;

        /** @brief The variables - THE object to get, set and override anything
         *  the firmware exposes. Non-const: `set` and `override` write through
         *  it. Consumers reach it here, so there is one wiring point rather than
         *  one per consumer. */
        Vars::Bank*                    vars      = nullptr;


        const Network::NetworkHandler* network   = nullptr; ///< IP config handler

        const Network::NtpClient*      ntp       = nullptr; ///< SNTP client

        const Network::MqttClient*     mqtt      = nullptr; ///< MQTT client

        /** @brief The Modbus TCP slave. The component rather than a context
         *  struct, like the network handlers above: SlaveContext is computed on
         *  demand, so there is no long-lived struct to point at. */
        const Modbus::TcpSlave*        modbus_tcp = nullptr;

        /** @brief The field-bus master. Its plan, its peers and its counters are
         *  the only view anyone has of what the RS-485 segment is doing. */
        const Modbus::RtuMaster*       modbus_rtu = nullptr;

    };

}

#endif // LIBBOTANIK_CORE_CONTEXT_H
