/**
 * @file modbus/tcp/master.cpp
 * @brief TcpMaster: the socket, the one transaction, and the deadlines.
 *
 * Every path here is non-blocking and bounded: a connect is polled, a send
 * resumes next beat, and a deadline is a comparison against a stamp rather than
 * a sleep.
 */
#include "libbotanik/modbus/tcp/master.h" // TcpMaster - implemented here

#include <cstring>                    // memmove - shifting a consumed frame out of the buffer

#include "libbotanik/logs/logger.h"   // Logs::Logger - the connect and the give-ups

#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT

namespace Botanik { namespace Modbus {

    namespace Net = HAL::Network;

    void TcpMaster::_enter(State s, uint64_t now_ms) {
        _state    = s;
        _since_ms = now_ms;
    }

    void TcpMaster::connect_to(const Net::Ip4& peer, uint16_t port) {
        disconnect();
        _peer = peer;
        _port = port;
    }

    void TcpMaster::disconnect() {
        if (_sock >= 0) Net::tcp_close(_sock);
        _sock    = -1;
        _tx_len  = 0;
        _tx_sent = 0;
        _rx_len  = 0;
        _state   = MA_IDLE;
    }

    // Transactions
    // ------------------------------------------------------------------------

    bool TcpMaster::_start(size_t len, const Expect& e) {
        if (len == 0) return false;                 // could not be framed
        _tx_len  = (uint16_t)len;
        _tx_sent = 0;
        _rx_len  = 0;
        _expect  = e;
        _resp    = Response();
        _err     = ERR_OK;
        _enter(MA_SENDING, _now_ms);
        return true;
    }

    bool TcpMaster::read_registers(uint8_t unit, uint16_t addr, uint16_t qty) {
        return read_bits(unit, FC_READ_HOLDING, addr, qty);
    }

    bool TcpMaster::read_bits(uint8_t unit, uint8_t fc, uint16_t addr, uint16_t qty) {
        if (_state != MA_READY) return false;
        // The id is what tells this answer from the one before it, so it moves
        // on every request and is checked on every response.
        const Mbap m = { ++_txn, unit };
        const size_t n = encode_read_request(_tx, sizeof(_tx), m, fc, addr, qty);
        const Expect e = { unit, fc, addr, qty };
        return _start(n, e);
    }

    bool TcpMaster::write_registers(uint8_t unit, uint16_t addr, uint16_t qty,
                                    const uint8_t* data) {
        if (_state != MA_READY) return false;
        const Mbap m = { ++_txn, unit };
        const size_t n = encode_write_request(_tx, sizeof(_tx), m, addr, qty, data);
        const Expect e = { unit, FC_WRITE_REGISTERS, addr, qty };
        return _start(n, e);
    }

    void TcpMaster::_finish(Error e, uint64_t now_ms) {
        _err = e;
        // A refusal is a completed transaction: the slave heard us and declined,
        // which is an answer. Only silence and malformed frames are faults.
        if (e == ERR_OK || e == ERR_EXCEPTION) _txns++;
        else                                   _faults++;
        _enter(MA_DONE, now_ms);
    }

    // The beat
    // ------------------------------------------------------------------------

    void TcpMaster::on_step(uint64_t now_ms) {
        _now_ms = now_ms;
        switch (_state) {
            case MA_IDLE:
                if (_port == 0) return;              // nowhere to go yet
                _sock = Net::tcp_open();
                if (_sock < 0) return;               // no socket budget; retry next beat
                _enter(MA_CONNECT, now_ms);
                return;
            case MA_CONNECT: _service_connect(now_ms); return;
            case MA_SENDING: _service_send(now_ms);    return;
            case MA_WAITING: _service_wait(now_ms);    return;
            case MA_READY:
            case MA_DONE:
                return;
        }
    }

    void TcpMaster::_service_connect(uint64_t now_ms) {
        const int r = Net::tcp_connect(_sock, _peer, _port);
        if (r > 0) {
            if (_logger != nullptr) {
                char ip[16];
                _logger->info(Logs::NETWORK, "modbus master connected to %s:%u",
                              Net::ip4_to_str(_peer, ip, sizeof(ip)),
                              (unsigned)_port);
            }
            _enter(MA_READY, now_ms);
            return;
        }
        if (r < 0 || (now_ms - _since_ms) > Config::MODBUS_TCP_CONNECT_TIMEOUT_MS) {
            disconnect();                            // back to IDLE; the next beat retries
        }
    }

    void TcpMaster::_service_send(uint64_t now_ms) {
        while (_tx_sent < _tx_len) {
            const int n = Net::tcp_send(_sock, _tx + _tx_sent,
                                        (size_t)(_tx_len - _tx_sent));
            if (n == 0) return;                      // would block: resume next beat
            if (n < 0) { disconnect(); _finish(ERR_TIMEOUT, now_ms); return; }
            _tx_sent = (uint16_t)(_tx_sent + n);
        }
        _enter(MA_WAITING, now_ms);
    }

    void TcpMaster::_service_wait(uint64_t now_ms) {
        if (_rx_len < sizeof(_rx)) {
            const int n = Net::tcp_recv(_sock, _rx + _rx_len,
                                        (size_t)(sizeof(_rx) - _rx_len));
            if (n < 0) { disconnect(); _finish(ERR_TIMEOUT, now_ms); return; }
            if (n > 0) _rx_len = (uint16_t)(_rx_len + n);
        }

        size_t used = 0;
        const Error e = decode_response(_rx, _rx_len, _expect, _txn, &_resp, &used);
        if (e == ERR_SHORT && used == 0) {
            // Not all there yet. Only the deadline ends the wait.
            if ((now_ms - _since_ms) > Config::MODBUS_TCP_RESPONSE_TIMEOUT_MS) {
                // The stream is no longer trustworthy: a late answer would arrive
                // while the next request is in flight, and matching it on the
                // transaction id only tells us it is stale AFTER reading it. Drop
                // the connection instead and start clean.
                disconnect();
                _finish(ERR_TIMEOUT, now_ms);
            }
            return;
        }

        // Terminal, good or bad. Consume the frame either way so a stale one
        // cannot be re-parsed against the next request.
        if (used > 0 && used <= _rx_len) {
            _rx_len = (uint16_t)(_rx_len - used);
            if (_rx_len > 0) memmove(_rx, _rx + used, _rx_len);
        } else {
            _rx_len = 0;
        }
        _finish(e, now_ms);
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_CLIENT
