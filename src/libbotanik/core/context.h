// REVIEW_35: DONE
#ifndef LIBBOTANIK_CORE_CONTEXT_H
#define LIBBOTANIK_CORE_CONTEXT_H

/** @file context.h
 *  @brief The handoff bundles: ProgramContext for one activation, and
 *  SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads. */

// Include library configuration
#include "libbotanik/config/base.h"

// Forward-declare pointer types for context members
namespace Botanik { namespace Scheduler { struct SchedulerContext;  } }
namespace Botanik { namespace Tasks     {  class Registry;          } }
namespace Botanik { namespace Vars      {  class Bank;              } }


namespace Botanik { namespace Network   {  class NetworkHandler;    } }

namespace Botanik { namespace Network   {  class NtpClient;         } }

namespace Botanik { namespace Network   {  class MqttClient;        } }

namespace Botanik { namespace Modbus    {  class TcpSlave;          } }
namespace Botanik { namespace Modbus    {  class RtuClient;         } }

namespace Botanik {

    /** @brief Aggregate of subsystem context structs and pointers to the
     *  optional network components; used by reporting commands and metric pages.
     *  Every member may be null when its subsystem is absent at runtime.
     * @note Every pointer is declared whatever the build: a forward-declared
 *  pointer costs nothing, and gating the FIELD would only move the `#if` to
 *  each of its consumers, which already have one. An absent subsystem simply
 *  leaves its pointer null. */
    /** @brief What a control program is handed each activation.
     *
     *  Built fresh per run, unlike the two wiring contexts around it: @c dt_ms
     *  and @c missed are THIS program's, so two programs in one beat see
     *  different values and no shared instance could hold them. */
    struct ProgramContext {
        uint64_t now_ms;     ///< beat timestamp, same for every program this beat
        uint32_t dt_ms;      ///< REAL ms since THIS program last ran, never the nominal period
        uint16_t missed;     ///< activations dropped since the last run; 0 normally
        bool     first_scan; ///< first activation at bring-up, or after control returned
        /** @brief The variables. Writes go through drive(), which claims the row
         *  for whoever the dispatcher's window names. */
        Vars::Bank& bank;
    };

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


        const Network::NetworkHandler* network   = nullptr; ///< IP config handler

        const Network::NtpClient*      ntp       = nullptr; ///< SNTP client

        const Network::MqttClient*     mqtt      = nullptr; ///< MQTT client

        /** @brief The Modbus TCP slave. The component rather than a context
         *  struct, like the network handlers above: SlaveContext is computed on
         *  demand, so there is no long-lived struct to point at. */
        const Modbus::TcpSlave*        modbus_tcp = nullptr;

        /** @brief The field-bus master. Its plan, its peers and its counters are
         *  the only view anyone has of what the RS-485 segment is doing. */
        const Modbus::RtuClient*       modbus_rtu = nullptr;

    };

}

#endif // LIBBOTANIK_CORE_CONTEXT_H
