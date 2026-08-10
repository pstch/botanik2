#ifndef MODBUS_TCP_MASTER_H
#define MODBUS_TCP_MASTER_H

/**
 * @file modbus/tcp/master.h
 * @brief TcpMaster: the other end of the TCP slave, one transaction at a time.
 *
 * Written for the simulator, where it proves the slave actually serves, but
 * ordinary firmware: a site polling another PLC over Ethernet uses the same
 * object, hence its own feature flag rather than a home under `botanik_sim/`.
 * A `step()` state machine (connect, send, poll, time out) for the reason
 * @ref modbus/modbus.h gives once for the subsystem.
 *
 * ONE transaction in flight. MBAP carries a transaction id so a master MAY
 * pipeline, and one that does needs a table of outstanding requests; one at a
 * time makes the id a check instead: an answer carrying a different one is an
 * answer to a question we already gave up on, and is refused as ERR_ECHO rather
 * than accepted for the current request.
 *
 * It shares the vocabulary and the diagnosis with the RTU client: wire.h's
 * @ref Error, @ref Response and @ref Expect, checked in the same order. Only the
 * framing differs.
 */

#include <cstddef>                    // size_t - frame lengths
#include <cstdint>                    // uint8_t/uint16_t/uint64_t - the wire and the clock

#include "libbotanik/config/modbus.h" // Config::MODBUS_TCP_* - port, timeouts

#include "libbotanik/core/component.h" // Component - the per-beat lifecycle
#include "libbotanik/hal/net.h"       // HAL::Network - the socket
#include "libbotanik/modbus/tcp/pdu.h" // the MBAP framing this speaks
#include "libbotanik/modbus/wire.h"   // Expect / Response / Error - shared with RTU

#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT

namespace Botanik { namespace Modbus {

    /**
     * @brief A Modbus TCP master: one connection, one transaction in flight.
     *
     * Usage is poll-shaped, like the DNS resolver: start a request, step the
     * component each beat, ask whether it finished. Nothing blocks and nothing
     * calls back.
     *
     * @code
     * if (master.read_registers(1, 1000, 2)) {      // unit, address, quantity
     *     ...each beat: master.step(now_ms) ...
     *     if (master.done()) {
     *         if (master.error() == ERR_OK) use(master.response());
     *     }
     * }
     * @endcode
     */
    class TcpMaster : public Component {

        /** @name State
         *  @{ */

        enum State : uint8_t {
            MA_IDLE = 0,   ///< no socket
            MA_CONNECT,    ///< non-blocking connect in progress
            MA_READY,      ///< connected, nothing in flight
            MA_SENDING,    ///< the request is going out
            MA_WAITING,    ///< sent; listening for the answer
            MA_DONE,       ///< a terminal result is waiting to be read
        };

        HAL::Network::Ip4       _peer = {{127, 0, 0, 1}};
        uint16_t                _port = 0;
        HAL::Network::TcpSocket _sock = -1;
        State                   _state = MA_IDLE;
        /** @brief Borrowed, flash-resident: what its log lines call it. */
        const char*             _name  = "ModbusTCPMaster";
        uint64_t                _since_ms = 0;   ///< when this state was entered
        /** @brief The beat's timestamp, kept because a request may be started
         *  from outside a step() and every deadline here is measured from when
         *  the transaction ACTUALLY began, not from when the connection went
         *  ready (which would arrive already expired). */
        uint64_t                _now_ms = 0;

        uint8_t  _tx[TCP_ADU_MAX];
        uint16_t _tx_len  = 0;
        uint16_t _tx_sent = 0;
        uint8_t  _rx[TCP_ADU_MAX];
        uint16_t _rx_len  = 0;

        Expect   _expect = Expect();
        uint16_t _txn    = 0;      ///< the id sent with the request in flight
        Response _resp   = Response();
        Error    _err    = ERR_OK;

        uint32_t _txns   = 0;      ///< transactions that completed
        uint32_t _faults = 0;      ///< that did not

        /** @} */

    public:
        /** @param name what it calls itself in the log. The simulator's own
         *  master passes a name saying so, or its lines read as firmware talking
         *  to a real peer. */
        TcpMaster(Logs::Logger* logger = nullptr,
                  const char* name = "ModbusTCPMaster")
            : Component(logger, name), _name(name) {}

        /** @name Connection
         *  @{ */

        /** @brief Point it at a slave. Takes effect on the next step; an open
         *  connection to another peer is dropped. */
        void connect_to(const HAL::Network::Ip4& peer, uint16_t port);
        bool connected() const { return _state >= MA_READY && _sock >= 0; }
        /** @brief Drop the connection (idempotent). */
        void disconnect();

        /** @} */

        /** @name Transactions
         *
         *  Each returns false when one is already in flight or the request could
         *  not be framed - never a silent enqueue, because there is no queue.
         *  @{ */

        /** @brief FC01/02/03/04: the function code says which table. A code that
         *  is not a read is refused rather than framed. */
        bool read(uint8_t unit, uint8_t fc, uint16_t addr, uint16_t qty);
        /** @brief FC16 from @p data, already big-endian register bytes. */
        bool write_registers(uint8_t unit, uint16_t addr, uint16_t qty,
                             const uint8_t* data);

        /** @brief Whether a terminal result is waiting. Stays true until
         *  take_result() or the next request. */
        bool done() const { return _state == MA_DONE; }
        /** @brief The result. ERR_OK, ERR_EXCEPTION (the slave refused - see
         *  response().ex), or the specific failure. */
        Error error() const { return _err; }
        /** @brief The payload, valid until the next request reuses the buffer. */
        const Response& response() const { return _resp; }
        /** @brief Consume the result and go back to READY. */
        void take_result() {
            // Back to READY only if the socket survived. Every failure path
            // disconnects before finishing, and MA_READY with _sock == -1 is a
            // master that never reconnects: on_service()'s MA_READY is a bare
            // return.
            if (_state != MA_DONE) return;
            _state = (_sock >= 0) ? MA_READY : MA_IDLE;
        }

        /** @} */

        /** @name Counters
         *  @{ */
        uint32_t txns() const   { return _txns; }
        uint32_t faults() const { return _faults; }
        /** @} */

    protected:
        void on_service(uint64_t now_ms) override;

    private:
        void _enter(State s, uint64_t now_ms);
        /** @brief Stage a framed request and start sending it. */
        bool _start(size_t len, const Expect& e);
        /** @brief Finish with @p e, counting it. */
        void _finish(Error e, uint64_t now_ms);
        void _service_connect(uint64_t now_ms);
        void _service_send(uint64_t now_ms);
        void _service_wait(uint64_t now_ms);
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_CLIENT

#endif // MODBUS_TCP_MASTER_H
