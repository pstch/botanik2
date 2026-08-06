#ifndef MODBUS_SERVER_H
#define MODBUS_SERVER_H

/**
 * @file modbus/server.h
 * @brief TcpServer: the Modbus TCP slave. Sockets, connections, and where in the
 *        beat a request is answered.
 *
 * pdu.h owns the framing and map.h owns the meaning; this owns the timing. It is
 * a @ref Botanik::Component like every other config-driven subsystem, reading the
 * `mb` parameter group and restarting its listener when that group's commit
 * serial moves.
 *
 * @section server_beat Two phases, one request
 *
 * The server is stepped TWICE per beat, from two different phases, and that is
 * the whole design:
 *
 *  - `service_rx()` in `PHASE_INPUT` accepts, reads, decodes, and applies WRITES.
 *    A setpoint written this beat is therefore what the control programs run on
 *    this beat, and `PHASE_CONFIG` picks up the group serial in the same cycle.
 *  - `service_tx()` in `PHASE_PRESENT` answers READS, off the image the scan just
 *    produced, and pushes the bytes out.
 *
 * The cost is one beat of latency on a read, against a SCADA poll measured in
 * seconds. What it buys is that no request is ever answered from a half-updated
 * image: nothing writes the bank between the output phase and the encode.
 *
 * @section server_bounded Bounded per beat
 *
 * Like every task: at most `MODBUS_ACCEPT_PER_STEP` accepts, ONE request decoded
 * per connection (a master may pipeline; the extra bytes wait), and the protocol
 * ceilings on the request itself. A partial send resumes next beat rather than
 * blocking.
 */

#include <cstdint>                    // uint8_t/uint16_t/uint64_t - buffers, counters, timestamps

#include "libbotanik_config.h"        // Config::MODBUS_* - port, slots, timeouts

#include "libbotanik/core/component.h" // Component - the config-driven lifecycle
#include "libbotanik/hal/net.h"       // HAL::Network - listener and connection sockets
#include "libbotanik/modbus/map.h"    // Modbus::Map - what an address means
#include "libbotanik/modbus/pdu.h"    // Modbus::Request / ADU_MAX - framing
#include "libbotanik/vars/model.h"    // Vars::Bank - the process image being served

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

namespace Botanik { namespace Modbus {

    /** @brief What the shell and the status UI read back. Borrowed by
     *  SystemContext, the SchedulerContext pattern: the server publishes, the
     *  consumers read, and nobody hands anybody a setter. */
    struct ServerContext {
        bool     listening   = false;   ///< the port is bound
        uint16_t port        = 0;       ///< what it is bound to
        uint8_t  clients     = 0;       ///< live connections
        uint16_t rows        = 0;       ///< rows in the map
        uint32_t requests    = 0;       ///< served, exceptions included
        uint32_t exceptions  = 0;       ///< refused with an exception code
        uint32_t writes      = 0;       ///< rows actually committed
        uint32_t rejects     = 0;       ///< connections refused or dropped
        uint32_t unit_misses = 0;       ///< requests for another unit id
        uint64_t last_req_ms = 0;       ///< uptime of the last request served
        HAL::Network::Ip4 last_peer = {{0, 0, 0, 0}};
    };

    /**
     * @brief The slave: a listening socket, a few connections, and the two
     *        per-beat entry points.
     */
    class TcpServer : public Component {

        /** @name Per-connection state
         *  @{ */

        /** @brief What a pending request is waiting for. One in flight per
         *  connection: every SCADA in the field works that way, and it bounds
         *  the buffers to one frame each. */
        enum Pending : uint8_t {
            PEND_NONE = 0,   ///< idle
            PEND_READ,       ///< decoded; answer it in the present phase
            PEND_BYTES       ///< the response is already framed; send it
        };

        struct Conn {
            HAL::Network::TcpSocket sock = -1;
            HAL::Network::Ip4 peer = {{0, 0, 0, 0}};
            uint8_t  rx[ADU_MAX];
            uint16_t rx_len  = 0;
            uint8_t  tx[ADU_MAX];
            uint16_t tx_len  = 0;      ///< bytes staged
            uint16_t tx_sent = 0;      ///< of which already written
            Pending  pending = PEND_NONE;
            Request  req;              ///< the deferred read
            uint64_t last_ms = 0;      ///< last complete request (idle reap)
            bool     closing = false;  ///< flush, then hang up
        };

        /** @} */

        /** @name State
         *  @{ */

        Vars::Bank*   _io;                 ///< the process image, and the config source
        Map           _map;
        ServerContext _ctx;
        HAL::Network::TcpSocket _listener = -1;
        Conn     _conn[Config::MODBUS_MAX_CONN];
        uint16_t _port    = 0;             ///< the port currently bound
        bool     _enabled = false;         ///< mb_enable
        bool     _writable = false;        ///< mb_write_enable

        /** @} */

    public:
        TcpServer(Vars::Bank* io, Logs::Logger* logger = nullptr);

        /** @name Per-beat entry points
         *  @{ */

        /** @brief PHASE_INPUT: accept, receive, decode, apply writes. This is
         *  the Component's on_step(), so it is also where the config group is
         *  re-read. */
        void service_rx(uint64_t now_ms) { step(now_ms); }
        /** @brief PHASE_PRESENT: answer the reads off the post-scan image and
         *  push bytes out. Not a Component step - a component has one. */
        void service_tx(uint64_t now_ms);

        /** @} */

        /** @name Readback
         *  @{ */

        const ServerContext& context() const { return _ctx; }
        const Map&           map() const     { return _map; }
        bool listening() const { return _listener >= 0; }
        /** @brief Whether writes are accepted at all right now (`mb_write_enable`). */
        bool writable() const  { return _writable; }

        /** @} */

    protected:
        void     on_setup(uint64_t now_ms) override;
        void     on_step(uint64_t now_ms) override;
        uint32_t param_serial() const override;
        void     on_param_changed(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */
        void _apply_config(uint64_t now_ms);   ///< read the `mb` group, rebind if needed
        void _open_listener(uint64_t now_ms);
        void _close_listener();
        void _accept(uint64_t now_ms);
        void _receive(Conn& c, uint64_t now_ms);
        /** @brief Decode one frame from @p c's buffer and act on it. */
        void _dispatch(Conn& c, uint64_t now_ms);
        /** @brief Serve a decoded read into @p c's tx buffer (present phase). */
        void _answer_read(Conn& c);
        /** @brief Apply a decoded write and frame its answer (input phase). */
        void _answer_write(Conn& c, const Request& r);
        void _refuse(Conn& c, const Request& r, Exception ex);
        void _flush(Conn& c);
        void _drop(Conn& c, const char* why);
        void _reap(uint64_t now_ms);
        /** @} */
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER

#endif // MODBUS_SERVER_H
