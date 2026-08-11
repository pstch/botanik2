// REVIEW_35: DONE
#ifndef LIBBOTANIK_CORE_CONTEXT_H
#define LIBBOTANIK_CORE_CONTEXT_H

/** @file context.h
 *  @brief SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads. */

// Include library configuration
#include "libbotanik/config/base.h"       // the BOTANIK_FEATURE_* gates

// Forward-declare pointer types for context members
namespace Botanik { namespace Scheduler { struct SchedulerContext;  } }
namespace Botanik { namespace Tasks     {  class Registry;          } }
namespace Botanik { namespace Vars      {  class Bank;              } }
namespace Botanik { namespace Vars      {  class Params;            } }


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

    /** @brief The console configuration's probation state: what a committed
     *  rate/make/size change is waiting for, and the operator's verdict on it.
     *
     *  Defined here rather than behind a forward declaration like the other
     *  contexts, because it depends on nothing at all - and because it is the
     *  MEETING POINT of two halves that must not see each other. The firmware
     *  owns the apply/revert; the shell's `console` command owns the verdict;
     *  `shell/` cannot include `firmware/` and should not learn how to.
     *
     *  Both verdict flags are consumed and cleared by the firmware on the CONFIG
     *  phase, so they are a one-beat handoff, not state. Main-loop only, no ISR:
     *  no barriers (@ref dev_concurrency). */
    struct ConsoleConfig {
        /** @brief Uptime ms the probation expires at; 0 when none is running,
         *  which is also what says a commit may be accepted. */
        uint64_t probation_until_ms = 0;
        /** @brief Operator confirms: make the applied settings durable. */
        bool     confirm = false;
        /** @brief Operator withdraws: put the previous settings back now,
         *  without waiting the window out. */
        bool     revert  = false;

        bool on_probation() const { return probation_until_ms != 0; }
    };

    /** @brief Aggregate of subsystem context structs and pointers to the
     *  optional network components; used by reporting commands and metric pages.
     *  Every member may be null when its subsystem is absent at runtime.
     * @warning Consumers should be gated under the matching `#if BOTANIK_FEATURE_*`. */
    struct SystemContext {

        Scheduler::SchedulerContext*   scheduler = nullptr; ///< Performance snapshots

        /** @brief The task table, with the per-task cost samples: the
         *  attribution the beat's CPU figure lacks, and what `tasks` reports.
         *  Const, because the registry is frozen once the loop starts. */
        const Tasks::Registry*         tasks     = nullptr;

        /** @brief The variables - THE object to get, set and override anything
         *  the firmware exposes. Non-const: `set` and `override` write through
         *  it. Consumers reach it here, so there is one wiring point rather than
         *  one per consumer. */
        Vars::Bank*                    vars      = nullptr;

        /** @brief The parameter surface over those variables. The bank above is
         *  what reads and writes a row; this owns the things that are true of
         *  the BLOCK - the boot load, and the retained-value save cadence that
         *  the `save` command restarts. Non-const for that reason. */
        Vars::Params*                  params    = nullptr;

        /** @brief Probation on a committed console configuration. Null in a
         *  build with no console, where nothing can commit one. */
        ConsoleConfig*                 console_cfg = nullptr;


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
