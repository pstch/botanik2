#ifndef MODBUS_TCP_MASTER_H
#define MODBUS_TCP_MASTER_H

/**
 * @file modbus/tcp/master.h
 * @brief TcpMaster: the other end of the TCP slave, one transaction at a time.
 *
 * Written for the simulator, where it is what proves the slave actually serves -
 * but it is ordinary firmware, not test scaffolding: a site polling another PLC
 * over Ethernet uses the same object, which is why it is here behind its own
 * feature flag rather than under `botanik_sim/`.
 *
 * @section ma_why_not_a_library Why this is ours
 *
 * Every published Modbus client for this platform is synchronous - Arduino's
 * wraps libmodbus, whose `modbus_read_registers()` sends, waits and receives
 * inside a single call, bounded only by a response timeout. One transaction is
 * milliseconds of blocking on a good day and the whole timeout on a bad one,
 * inside a 10 ms beat in which nothing else would run. So this is a `step()`
 * state machine like everything else here: connect, send, poll, time out.
 *
 * @section ma_one One transaction in flight
 *
 * Deliberately, and not only for simplicity: MBAP carries a transaction id
 * precisely so a master MAY pipeline, and a master that does has to keep a table
 * of outstanding requests and match answers to it. One at a time makes the id a
 * check rather than a lookup - an answer carrying a different one is an answer to
 * a question we already gave up on, and is refused as ERR_ECHO instead of being
 * quietly accepted for the current request.
 *
 * @section ma_share What it shares with the RTU client
 *
 * The vocabulary and the diagnosis: it reports wire.h's @ref Error, fills
 * wire.h's @ref Response, and checks the answer against wire.h's @ref Expect in
 * the same order the RTU parser does. Only the framing differs.
 */

#include <cstddef>                    // size_t - frame lengths
#include <cstdint>                    // uint8_t/uint16_t/uint64_t - the wire and the clock

#include "libbotanik_config.h"        // Config::MODBUS_TCP_* - port, timeouts

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
        uint64_t                _since_ms = 0;   ///< when this state was entered
        /** @brief The beat's timestamp, kept because a request may be started
         *  from outside a step() and every deadline here is measured from when
         *  the transaction ACTUALLY began. Stamping one with the time the
         *  connection went ready made every request arrive already expired. */
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
        TcpMaster(Logs::Logger* logger = nullptr)
            : Component(logger, "ModbusTCPMaster") {}

        /** @name Connection
         *  @{ */

        /** @brief Point it at a slave. Takes effect on the next step; an open
         *  connection to another peer is dropped. */
        void connect_to(const HAL::Network::Ip4& peer, uint16_t port);
        bool connected() const { return _state >= MA_READY; }
        /** @brief Drop the connection (idempotent). */
        void disconnect();

        /** @} */

        /** @name Transactions
         *
         *  Each returns false when one is already in flight or the request could
         *  not be framed - never a silent enqueue, because there is no queue.
         *  @{ */

        bool read_registers(uint8_t unit, uint16_t addr, uint16_t qty);
        bool read_bits(uint8_t unit, uint8_t fc, uint16_t addr, uint16_t qty);
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
        void take_result() { if (_state == MA_DONE) _state = MA_READY; }

        /** @} */

        /** @name Counters
         *  @{ */
        uint32_t txns() const   { return _txns; }
        uint32_t faults() const { return _faults; }
        /** @} */

    protected:
        void on_step(uint64_t now_ms) override;

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
