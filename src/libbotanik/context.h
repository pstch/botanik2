#ifndef CONTEXT_H
#define CONTEXT_H

/**
 * @file context.h
 * @brief SystemContext - one aggregate of the per-subsystem status contexts the
 *        terminal UI reads.
 */

// Only pointers are stored, so the pointee definitions are not needed here.
namespace Botanik { namespace Scheduler { struct SchedulerContext; } }
namespace Botanik { namespace PlcIO     { struct PlcSyncContext;   } }
namespace Botanik { namespace Network {
    class NetworkHandler; class NtpClient; class MqttClient;
} }

namespace Botanik {

    /** @brief Borrowed-pointer aggregate of the subsystem status contexts read
     *  by the terminal UI (status bar + `stats` command). The owning subsystems
     *  outlive it; any member may be null when its subsystem is absent. The
     *  network components are read-only here; they stay null when their feature
     *  is off, so readers null-check (no feature macros needed). */
    struct SystemContext {
        // tick/overrun + perf snapshot
        Scheduler::SchedulerContext*   scheduler = nullptr;
        // link status + log buffers
        PlcIO::PlcSyncContext*         plc       = nullptr;
        // network stack status (set post-construction when compiled in)
        const Network::NetworkHandler* network   = nullptr;
        const Network::NtpClient*      ntp       = nullptr;
        const Network::MqttClient*     mqtt      = nullptr;

        /** @brief Bundle the subsystem statuses; any may be null when its
         *  subsystem is absent. The network pointers are wired here (not
         *  post-construction) so a by-value consumer copies the live set. */
        SystemContext(Scheduler::SchedulerContext* sched = nullptr,
                      PlcIO::PlcSyncContext*        plc_ctx = nullptr,
                      const Network::NetworkHandler* net = nullptr,
                      const Network::NtpClient*      ntp_c = nullptr,
                      const Network::MqttClient*     mqtt_c = nullptr)
            : scheduler(sched), plc(plc_ctx),
              network(net), ntp(ntp_c), mqtt(mqtt_c) {}
    };

}

#endif // CONTEXT_H
