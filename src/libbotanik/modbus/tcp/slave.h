#ifndef MODBUS_TCP_SLAVE_H
#define MODBUS_TCP_SLAVE_H

/**
 * @file modbus/tcp/slave.h
 * @brief TcpSlave: the Modbus TCP slave. Sockets, connections, and where in the
 *        beat a request is answered.
 *
 * pdu.h owns the framing and map.h owns the meaning; this owns the timing. It is
 * a @ref Botanik::Component like every other config-driven subsystem, reading the
 * `mb` parameter group and restarting its listener when that group's commit
 * serial moves.
 * @note The rationale behind this file is @ref server_beat, @ref server_ack, @ref server_bounded.
 */

#include <cstdint>

#include "libbotanik/config/modbus.h"

#include "libbotanik/core/component.h"
#include "libbotanik/hal/net.h"
#include "libbotanik/modbus/tcp/map.h"
#include "libbotanik/modbus/tcp/pdu.h"
#include "libbotanik/vars/model.h"

// Forward-declared, not included: the slave keeps a borrowed pointer and only
// asks it one question, so it must not acquire the whole of network/.
namespace Botanik { namespace Network { class NetworkHandler; } }

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

// Only ever held and dereferenced through a pointer here; alarms/alarms.h
// costs an includer logger.h, publisher.h and two config headers, and every
// consumer of this header would pay for them.
namespace Botanik { namespace Alarms { class AlarmController; } }

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Modbus {

    /** @brief The `mb` group: whether the slave answers at all, on which port,
     *  and whether a master may write. */
    struct TcpSlaveConfig {
        bool     enabled  = true;
        bool     writable = true;
        uint16_t port     = 502;
    };

    /** @brief What the shell and the status UI read back. Borrowed by
     *  SystemContext, the SchedulerContext pattern: the server publishes, the
     *  consumers read, and nobody hands anybody a setter. */
    struct SlaveContext {
        /** @name Derived, filled by TcpSlave::context()
         *  The server holds these as the socket and the map, so there is no
         *  second copy to keep in step.
         *  @{ */
        bool     listening   = false;   ///< the port is bound
        /** @brief mb_enable. Not listening is only a FAULT when this is on:
         *  a display that cannot tell the two apart reports a slave the
         *  operator switched off as a slave that broke. */
        bool     enabled     = false;
        /** @brief The port is wanted, and the last attempt to bind it failed.
         *  bind_error is what the backend said (errno on the simulator, an
         *  nsapi code on the OPTA), 0 when it had nothing to say. */
        bool     bind_failed = false;
        int      bind_error  = 0;
        uint16_t port        = 0;       ///< what it is bound to
        uint8_t  clients     = 0;       ///< live connections
        uint16_t rows        = 0;       ///< rows in the map
        /** @} */
        /** @name Counted as it happens
         *  @{ */
        uint32_t requests    = 0;       ///< served, exceptions included
        uint32_t exceptions  = 0;       ///< refused with an exception code
        uint32_t writes      = 0;       ///< rows actually committed
        uint32_t rejects     = 0;       ///< connections refused or dropped
        uint32_t unit_misses = 0;       ///< requests for another unit id
        uint64_t last_req_ms = 0;       ///< uptime of the last request served
        HAL::Network::Ip4 last_peer = {{0, 0, 0, 0}};
        /** @} */
    };

    /**
     * @brief The slave: a listening socket, a few connections, and the two
     *        per-beat entry points.
     */
    class TcpSlave : public Component {

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
            uint8_t  rx[TCP_ADU_MAX];
            uint16_t rx_len  = 0;
            uint8_t  tx[TCP_ADU_MAX];
            uint16_t tx_len  = 0;      ///< bytes staged
            uint16_t tx_sent = 0;      ///< of which already written
            Pending  pending = PEND_NONE;
            Request  req;              ///< the deferred read
            uint64_t last_ms  = 0;     ///< last complete request (idle reap)
            /** @brief When this answer started waiting to go out, and how far it
             *  had got then. A peer that stops reading leaves `pending` set for
             *  ever and the idle reap skips a connection with work pending, so
             *  without these two, two such peers hold both slots for good. */
            uint64_t tx_since = 0;
            uint16_t tx_mark  = 0;
        };

        /** @} */

        /** @name State
         *  @{ */

        Vars::Bank*   _io;                 ///< the process image, and the config source
        /** @brief The alarm model, for the ack block. Borrowed and optional
         *  (HealthMonitor owns it); null leaves the whole block unmapped. */
        Alarms::AlarmController* _alarms;
        Map           _map;
        /** @brief Condition index per MAP ROW, or -1 for a row that is not an
         *  alarm. Resolved once at bring-up.
         *
         *  Answering the ack block by NAME would be two strcmp searches per
         *  coil, and a legal FC01 may ask for 2000 of them: about 2 ms in
         *  PHASE_PRESENT, per connection, per beat, at a remote peer's
         *  choosing. */
        int16_t       _ack_cond[Config::MODBUS_TCP_MAX_ROWS];
        /** @brief The `mb` group in force. Only `enabled`/`port` move the
         *  socket; `writable` is read per request. */
        TcpSlaveConfig _cfg;
        /** @brief Whether the `mb` config says the slave should be up at all. */
        bool          _enabled = false;
        /** @brief The port could not be bound. Kept so the failure is retried
         *  and reported once rather than every attempt. */
        bool          _bind_failed = false;
        int           _bind_error  = 0;    ///< HAL::Network::last_error() from it
        uint64_t      _retry_ms = 0;
        SlaveContext _ctx;
        /** @brief The link, borrowed and possibly null. @see set_network */
        const Network::NetworkHandler* _net = nullptr;
        /** @brief Whether the link was up on the previous beat, so the fall is
         *  an edge rather than a repeated close.
         *  @note Starts FALSE, which is what an edge detector with nothing
         *  observed yet has to say. True would make the first beat of a link
         *  that has not come up yet read as a FALL, and close the listener
         *  on_setup() opened - for a whole MODBUS_TCP_BIND_RETRY_MS. */
        bool         _link_up  = false;
        HAL::Network::TcpSocket _listener = -1;
        Conn     _conn[Config::MODBUS_TCP_MAX_CONN];
        uint16_t _port     = 0;            ///< the port currently bound
        bool     _writable = false;        ///< mb_write_enable

        /** @} */

    public:
        TcpSlave(const SetupContext& ctx, Vars::Bank* io,
                  Alarms::AlarmController* alarms = nullptr);

        /** @brief Give the port back.
         *
         *  On the OPTA this never runs (the slave outlives the firmware), but in
         *  a HOST process the listener and every open connection would otherwise
         *  stay bound for the life of the process, so a later test could not use
         *  the port and the sim could not restart cleanly. */
        ~TcpSlave() override { _close_listener(); }

        /** @brief Adopt @p cfg: rebind if the port moved, close if disabled. */
        void set_config(const TcpSlaveConfig& cfg);

        /** @brief Watch the link, so the listener is given back and retaken
         *  across a teardown. Borrowed; null leaves the slave assuming the link
         *  is always up, which is what a test rig wants.
         *
         *  Without it nothing here ever re-opened a listener that had once
         *  bound: _retry_listener() fires only when a bind FAILED. A commit of
         *  the `net_` group runs link_configure(), whose first act on the OPTA
         *  is disconnect(). @see on_input */
        void set_network(const Network::NetworkHandler* net) { _net = net; }

        /** @name Readback
         *  @{ */

        /** @brief The live status, by value: the counters as they stand plus the
         *  four derived fields filled in on the way out. */
        SlaveContext        context() const;
        const Map&           map() const     { return _map; }
        bool listening() const { return _listener >= 0; }

        /** @brief Healthy unless it was asked to serve and cannot.
         *
         *  Component's default is an unconditional yes, under which a slave that
         *  never bound its port would report well for the life of the firmware,
         *  a failure indistinguishable from outside from a network problem. */
        bool healthy() const override;
        /** @brief Whether writes are accepted at all right now (`mb_write_enable`). */
        bool writable() const  { return _writable; }

        /** @} */

    public:
        /** @brief Three tasks: decode a master's writes at PHASE_INPUT, drain
         *  the persist queue at PHASE_SERVICE, answer at PHASE_PRESENT. One
         *  entry each, so each is separately attributable. */
        void register_tasks() override;

    protected:
        void     on_setup(uint64_t now_ms) override;
        /** @brief Accept, receive, decode and apply writes (PHASE_INPUT), so a
         *  setpoint a master writes this beat is what the programs run on. Also
         *  where the config group is re-read. */
        void     on_input(uint64_t now_ms) override;
        /** @brief Drain the persist queue (PHASE_SERVICE), a few per beat so a
         *  burst of writes is not a flash storm. Separate from the request that
         *  caused it because storage is synchronous: one FC16 can cover 61 rows,
         *  and committing them in PHASE_INPUT would put hundreds of milliseconds
         *  of flash, remotely triggerable, on a beat the field bus shares. */
        void     on_service(uint64_t now_ms) override;
        /** @brief Answer (PHASE_PRESENT), off the image the scan just
         *  produced, so no request is served from a half-updated one. */
        void     on_present(uint64_t now_ms) override;

    private:
        /** @name Internals
         *  @{ */

        void _open_listener();
        void _close_listener();
        void _accept(uint64_t now_ms);
        void _receive(Conn& c);
        /** @brief Decode one frame from @p c's buffer and act on it. */
        void _dispatch(Conn& c, uint64_t now_ms);
        /** @brief Serve a decoded read into @p c's tx buffer (present phase). */
        void _answer_read(Conn& c);
        /** @brief Apply a decoded write and frame its answer (input phase). */
        void _answer_write(Conn& c, const Request& r);
        void _refuse(Conn& c, const Request& r, Exception ex);
        /** @brief Whether @p addr falls in the acknowledgement block. */
        static bool _is_ack(uint16_t addr) {
            return addr >= Config::MODBUS_TCP_ACK_COIL_BASE;
        }
        /** @brief The condition at ack-block address @p addr, or -1. A lookup in
         *  @c _ack_cond, not a search. */
        int  _ack_index(uint16_t addr) const;
        /** @brief Resolve the ack block once, after the map is built. */
        void _bind_acks();
        Exception _read_ack(uint16_t addr, uint16_t count, uint8_t* out, size_t cap) const;
        Exception _write_ack(uint16_t addr, bool on,
                             const HAL::Network::Ip4& peer);
        void _flush(Conn& c);
        void _drop(Conn& c, const char* why);
        void _reap(uint64_t now_ms);
        /** @brief Try the port again, slowly, after a bind failure. */
        void _retry_listener(uint64_t now_ms);
        /** @} */
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER

#endif // MODBUS_TCP_SLAVE_H
