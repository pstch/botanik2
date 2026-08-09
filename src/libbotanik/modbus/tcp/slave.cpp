/**
 * @file modbus/tcp/slave.cpp
 * @brief TcpSlave: the socket handling and the two-phase beat.
 *
 * Every loop here is bounded and every call non-blocking, like the rest of the
 * firmware: `recv` returning 0 is would-block and not a close, a partial `send`
 * resumes on the next beat, and nothing waits for a peer.
 */
#include "libbotanik/modbus/tcp/slave.h" // TcpSlave - implemented here

#include <cstring>                    // memmove - shifting a consumed frame out of the RX buffer

#include "libbotanik/alarms/alarms.h" // Alarms::AlarmController - the ack block's target
#include "libbotanik/logs/logger.h"   // Logs::Logger - the bind, the drops and every commit
#include "libbotanik/vars/params.h"   // Vars::get_int / group_serial - the `mb` config group

#if BOTANIK_FEATURE_MODBUS_TCP_SERVER

namespace Botanik { namespace Modbus {

    using namespace Vars;
    namespace Net = HAL::Network;

    TcpSlave::TcpSlave(Bank* io, Logs::Logger* logger,
                         Alarms::AlarmController* alarms)
        : Component(logger, "ModbusTCP"), _io(io), _alarms(alarms), _map(io) {}

    SlaveContext TcpSlave::context() const {
        SlaveContext c = _ctx;
        c.listening = (_listener >= 0);
        c.port      = _port;
        c.rows      = _map.count();
        c.clients   = 0;
        for (const Conn& k : _conn) if (k.sock >= 0) c.clients++;
        return c;
    }

    // Lifecycle
    // ------------------------------------------------------------------------

    void TcpSlave::on_setup(uint64_t now_ms) {
        // The map is built once, here rather than on the first beat: it is a
        // scan of the whole variable table, and the first beat is not where a
        // couple of hundred rows of insertion sort belongs.
        _map.build();
        _bind_acks();
        if (_map.overflow() != 0 && _logger != nullptr)
            _logger->critical(Logs::MODBUS,
                              "map full: %u rows unpublished",
                              (unsigned)_map.overflow());
        _apply_config(now_ms);
    }

    uint32_t TcpSlave::param_serial() const { return group_serial(_io, "mb"); }

    void TcpSlave::on_param_changed(uint64_t now_ms) { _apply_config(now_ms); }

    void TcpSlave::_apply_config(uint64_t now_ms) {
        const bool     want_on   = get_int(_io, "mb_enable", 1) != 0;
        const uint16_t want_port = (uint16_t)get_int(_io, "mb_port",
                                                    Config::MODBUS_TCP_PORT);
        _writable = get_int(_io, "mb_write_enable", 1) != 0;

        (void)now_ms;
        _enabled = want_on;
        if (!want_on) { _bind_failed = false; _close_listener(); return; }
        // A port change means the socket has to move, and the connections on the
        // old one are conversations about a service that is no longer there.
        if (_listener >= 0 && want_port != _port) _close_listener();
        _port = want_port;
        if (_listener < 0) _open_listener();
    }

    void TcpSlave::_retry_listener(uint64_t now_ms) {
        if (!_enabled || _listener >= 0 || !_bind_failed) return;
        if (now_ms - _retry_ms < Config::MODBUS_TCP_BIND_RETRY_MS) return;
        _retry_ms = now_ms;
        _open_listener();
    }

    void TcpSlave::_open_listener() {
        _listener = Net::tcp_listen(_port);
        if (_listener >= 0) {
            _bind_failed = false;
            if (_logger != nullptr)
                _logger->info(Logs::NETWORK, "modbus slave on port %u",
                              (unsigned)_port);
            return;
        }
        // Retried, and reported. A port already taken is usually a configuration
        // mistake with an operator behind it - but "usually" is not "always",
        // and the old behaviour was to give up for the life of the firmware
        // while healthy() went on saying yes. Once per failure in the log, so a
        // permanent misconfiguration does not become a log entry every minute.
        if (!_bind_failed && _logger != nullptr)
            _logger->error(Logs::NETWORK, "modbus slave cannot bind port %u",
                           (unsigned)_port);
        _bind_failed = true;
    }

    void TcpSlave::_close_listener() {
        for (Conn& c : _conn) if (c.sock >= 0) _drop(c, nullptr);
        if (_listener >= 0) Net::tcp_close(_listener);
        _listener = -1;
    }

    // The input half of the beat
    // ------------------------------------------------------------------------

    void TcpSlave::on_step(uint64_t now_ms) {
        _retry_listener(now_ms);

        // Disabled, or a bind that has not come good yet: either way there is no
        // socket to serve on. _retry_listener() above is what changes that.
        if (_listener < 0) return;

        _accept(now_ms);
        for (Conn& c : _conn) {
            if (c.sock < 0) continue;
            _receive(c);
            if (c.sock >= 0) _dispatch(c, now_ms);
        }
        _reap(now_ms);
    }

    void TcpSlave::_accept(uint64_t now_ms) {
        for (uint8_t n = 0; n < Config::MODBUS_TCP_ACCEPT_PER_STEP; n++) {
            const Net::TcpSocket s = Net::tcp_accept(_listener);
            if (s < 0) return;                      // nothing pending

            Conn* free_slot = nullptr;
            for (Conn& c : _conn) if (c.sock < 0) { free_slot = &c; break; }
            if (free_slot == nullptr) {
                // Full: take it and hang up at once rather than leaving it in the
                // backlog, where the master would sit waiting on a connection
                // that looks established. Counted, because "the SCADA cannot
                // connect" and "somebody is leaking sockets" look identical from
                // the outside.
                Net::tcp_close(s);
                _ctx.rejects++;
                if (_logger != nullptr)
                    _logger->warning(Logs::NETWORK, "modbus: no free connection slot");
                return;
            }
            Conn& c   = *free_slot;
            c.sock    = s;
            if (!Net::tcp_peer(s, &c.peer)) c.peer = Net::Ip4{{0, 0, 0, 0}};
            c.rx_len  = 0;
            c.tx_len  = 0;
            c.tx_sent = 0;
            c.pending = PEND_NONE;
            c.last_ms = now_ms;
        }
    }

    void TcpSlave::_receive(Conn& c) {
        if (c.rx_len >= TCP_ADU_MAX) return;            // full: decode first
        const int n = Net::tcp_recv(c.sock, c.rx + c.rx_len,
                                    (size_t)(TCP_ADU_MAX - c.rx_len));
        if (n < 0) { _drop(c, nullptr); return; }   // peer closed
        if (n > 0) c.rx_len = (uint16_t)(c.rx_len + n);
    }

    void TcpSlave::_dispatch(Conn& c, uint64_t now_ms) {
        // One request in flight: while an answer is owed, further bytes stay in
        // the buffer. That is what keeps a pipelining master from needing a
        // queue here.
        if (c.pending != PEND_NONE || c.rx_len == 0) return;

        Request  r;
        size_t   used = 0;
        const Decode d = decode(c.rx, c.rx_len, &r, &used);
        if (d == DEC_INCOMPLETE) return;
        if (d == DEC_INVALID) {
            // Framing is broken and TCP has nothing to resynchronise to, so the
            // connection is the only thing that can be reset.
            _ctx.rejects++;
            _drop(c, "malformed frame");
            return;
        }

        c.last_ms = now_ms;
        _ctx.requests++;
        _ctx.last_req_ms = now_ms;
        _ctx.last_peer   = c.peer;
        if (r.mbap.unit != Config::MODBUS_TCP_UNIT_ID && r.mbap.unit != 0 &&
            r.mbap.unit != 255)
            _ctx.unit_misses++;

        // Acted on BEFORE the frame is consumed: a multi-register write's data
        // points into this very buffer, and shifting it first would commit
        // whatever slid into its place.
        if (d == DEC_EXCEPTION) {
            _refuse(c, r, r.ex);
        } else if (!is_read(r.fc)) {
            _answer_write(c, r);
        } else {
            // Deferred to the present phase, so the answer reflects the beat that
            // is about to run rather than the one before it. Only the request's
            // scalar fields are kept; `data` belongs to the buffer.
            c.req      = r;
            c.req.data = nullptr;
            c.pending  = PEND_READ;
        }

        // Consume it whatever came of it: the length field was authoritative, so
        // the bytes are accounted for even when the request was refused.
        c.rx_len = (uint16_t)(c.rx_len - used);
        if (c.rx_len > 0) memmove(c.rx, c.rx + used, c.rx_len);
    }

    void TcpSlave::_answer_write(Conn& c, const Request& r) {
        if (!_writable) { _refuse(c, r, EX_ILLEGAL_ADDRESS); return; }

        uint16_t committed = 0;
        Exception ex = EX_NONE;
        switch (r.fc) {
            case FC_WRITE_REGISTER:
                // One register is half a row, and splicing it into the other
                // half is the map's business, not this layer's.
                ex = _map.write_register(r.addr, r.value, &committed);
                break;
            case FC_WRITE_REGISTERS:
                ex = _map.write_registers(r.addr, r.count, r.data, &committed);
                break;
            case FC_WRITE_COIL:
                if (_is_ack(r.addr)) ex = _write_ack(r.addr, r.value == 0xFF00, c.peer);
                else ex = _map.write_bit(r.addr, r.value == 0xFF00, &committed);
                break;
            // A run of coils would be a run of boolean parameters, which no site
            // declares - so there is no plural bit write to route it to.
            case FC_WRITE_COILS:
            default:
                ex = EX_ILLEGAL_FUNCTION;
                break;
        }

        if (ex != EX_NONE) { _refuse(c, r, ex); return; }

        _ctx.writes += committed;
        if (committed > 0 && _logger != nullptr) {
            char ip[16];
            _logger->notice(Logs::MODBUS, "write from %s\n"
                            "%u row(s) at %u",
                            Net::ip4_to_str(c.peer, ip, sizeof(ip)),
                            (unsigned)committed, (unsigned)r.addr);
        }
        const uint16_t echo = (r.fc == FC_WRITE_COIL) ? r.value : r.count;
        c.tx_len  = (uint16_t)encode_echo(c.tx, sizeof(c.tx), r.mbap, r.fc,
                                          r.addr, echo);
        c.tx_sent = 0;
        c.pending = PEND_BYTES;
    }

    // The acknowledgement block
    // ------------------------------------------------------------------------

    void TcpSlave::_bind_acks() {
        for (uint16_t i = 0; i < Config::MODBUS_TCP_MAX_ROWS; ++i) _ack_cond[i] = -1;
        if (_alarms == nullptr || _io == nullptr) return;
        for (uint16_t i = 0; i < _map.count(); ++i) {
            const Var v = _map.row(i);
            // An alarm and nothing else: the block is the alarm model's, so a
            // parameter's register appearing in it would be a coincidence of
            // arithmetic rather than an address anybody published.
            if (v.meta()->scope != SCOPE_A) continue;
            const int k = _alarms->index_of(v.name());
            if (k >= 0) _ack_cond[i] = (int16_t)k;
        }
    }

    int TcpSlave::_ack_index(uint16_t addr) const {
        if (_alarms == nullptr) return -1;
        const uint16_t reg = (uint16_t)(addr - Config::MODBUS_TCP_ACK_COIL_BASE);
        const int i = _map.index_of(reg);
        if (i < 0 || i >= (int)Config::MODBUS_TCP_MAX_ROWS) return -1;
        return _ack_cond[i];
    }

    Exception TcpSlave::_read_ack(uint16_t addr, uint16_t count,
                                   uint8_t* out, size_t cap) const {
        if (_alarms == nullptr) return EX_ILLEGAL_ADDRESS;
        if (cap < bit_bytes(count)) return EX_SLAVE_FAILURE;
        for (uint16_t i = 0; i < bit_bytes(count); i++) out[i] = 0;
        for (uint16_t i = 0; i < count; i++) {
            const int k = _ack_index((uint16_t)(addr + i));
            // The base address, and any register with no alarm on it, read 0 -
            // the same "a gap reads as zero" the register map gives, so a master
            // can poll the block as one round run.
            if (k < 0) continue;
            put_bit(out, i, _alarms->condition((uint8_t)k).user_ack);
        }
        return EX_NONE;
    }

    Exception TcpSlave::_write_ack(uint16_t addr, bool on,
                                    const Net::Ip4& peer) {
        if (_alarms == nullptr) return EX_ILLEGAL_ADDRESS;
        // Un-acknowledging is not in the model, and silently ignoring the write
        // would look to the master exactly like it had worked.
        if (!on) return EX_ILLEGAL_VALUE;

        if (addr == Config::MODBUS_TCP_ACK_COIL_BASE) {
            const uint8_t n = _alarms->ack_all();
            if (n > 0 && _logger != nullptr) {
                char ip[16];
                _logger->notice(Logs::STATUS, "modbus %s: acknowledged\n"
                                "%u alarm(s)",
                                Net::ip4_to_str(peer, ip, sizeof(ip)), (unsigned)n);
            }
            return EX_NONE;
        }
        const int k = _ack_index(addr);
        if (k < 0) return EX_ILLEGAL_ADDRESS;
        const char* name = _alarms->condition((uint8_t)k).name;
        // ack() refuses a condition that is not active. That is not an error to
        // report: a SCADA's "acknowledge all" button reaches every coil in the
        // block, and most of them are quiet.
        if (_alarms->ack(name) && _logger != nullptr) {
            char ip[16];
            _logger->notice(Logs::STATUS, "modbus %s: acknowledged\n%s",
                            Net::ip4_to_str(peer, ip, sizeof(ip)), name);
        }
        return EX_NONE;
    }

    void TcpSlave::_refuse(Conn& c, const Request& r, Exception ex) {
        _ctx.exceptions++;
        c.tx_len  = (uint16_t)encode_exception(c.tx, sizeof(c.tx), r.mbap, r.fc, ex);
        c.tx_sent = 0;
        c.pending = PEND_BYTES;
    }

    // The present half of the beat
    // ------------------------------------------------------------------------

    void TcpSlave::service_persist(uint64_t now_ms) {
        (void)now_ms;
        (void)_map.drain_persists(Config::MODBUS_PERSIST_PER_STEP);
    }

    void TcpSlave::service_tx(uint64_t now_ms) {
        (void)now_ms;
        for (Conn& c : _conn) {
            if (c.sock < 0) continue;
            if (c.pending == PEND_READ) _answer_read(c);
            if (c.pending == PEND_BYTES) _flush(c);
        }
    }

    void TcpSlave::_answer_read(Conn& c) {
        const Request& r = c.req;
        // The payload is built HERE, in the present phase: this is the post-scan
        // image, and answering off it is the reason the two halves are split.
        // 125 registers or 2000 bits: 250 bytes either way, which is why one
        // buffer serves both (wire.h owns both ceilings).
        uint8_t payload[2 * 125];
        uint8_t n = 0;
        Exception ex = EX_NONE;

        switch (r.fc) {
            case FC_READ_HOLDING:
            case FC_READ_INPUT:
                n  = (uint8_t)(r.count * 2);
                ex = _map.read_registers(r.addr, r.count, payload, sizeof(payload));
                break;
            case FC_READ_COILS:
            case FC_READ_DISCRETE: {
                // 2000 bits is the protocol ceiling decode() already held this to,
                // and 2000 bits is 250 bytes: the same as 125 registers, which is
                // why one buffer serves both.
                n = (uint8_t)bit_bytes(r.count);
                // Only the coil table has an ack block. FC02 there falls through
                // to the map, which refuses it: a discrete input is quality, and
                // an acknowledgement is not a quality.
                if (r.fc == FC_READ_COILS && _is_ack(r.addr))
                    ex = _read_ack(r.addr, r.count, payload, sizeof(payload));
                else
                    ex = _map.read_bits(r.addr, r.count,
                                        (r.fc == FC_READ_COILS) ? BITS_VALUE
                                                                : BITS_QUALITY,
                                        payload, sizeof(payload));
                break;
            }
            default:
                ex = EX_ILLEGAL_FUNCTION;
                break;
        }

        if (ex != EX_NONE) { _refuse(c, r, ex); return; }
        c.tx_len  = (uint16_t)encode_read(c.tx, sizeof(c.tx), r.mbap, r.fc, payload, n);
        c.tx_sent = 0;
        c.pending = PEND_BYTES;
    }

    void TcpSlave::_flush(Conn& c) {
        while (c.tx_sent < c.tx_len) {
            const int n = Net::tcp_send(c.sock, c.tx + c.tx_sent,
                                        (size_t)(c.tx_len - c.tx_sent));
            if (n == 0) return;                     // would block: resume next beat
            if (n < 0)  { _drop(c, nullptr); return; }
            c.tx_sent = (uint16_t)(c.tx_sent + n);
        }
        c.tx_len   = 0;
        c.tx_sent  = 0;
        c.tx_mark  = 0;
        c.tx_since = 0;
        c.pending  = PEND_NONE;
    }

    // Housekeeping
    // ------------------------------------------------------------------------

    void TcpSlave::_drop(Conn& c, const char* why) {
        if (c.sock < 0) return;
        Net::tcp_close(c.sock);
        c.sock    = -1;
        c.rx_len   = 0;
        c.tx_len   = 0;
        c.tx_sent  = 0;
        c.tx_mark  = 0;
        c.tx_since = 0;
        c.pending  = PEND_NONE;
        // Only a FAULT is worth a line. A client closing, or going quiet until
        // the idle timer reaps it, is what clients do all day - a SCADA poller
        // reconnecting on its own schedule would fill the log with its own good
        // behaviour, and the sim's self-check did exactly that on every run.
        // Both are already counted in `rejects`, which is where a rate belongs;
        // what a log line is for is the one that should not have happened.
        if (why != nullptr && _logger != nullptr) {
            char ip[16];
            _logger->warning(Logs::MODBUS, "dropped %s\n%s",
                          Net::ip4_to_str(c.peer, ip, sizeof(ip)), why);
        }
    }

    void TcpSlave::_reap(uint64_t now_ms) {
        for (Conn& c : _conn) {
            if (c.sock < 0) continue;

            // An answer that is not going anywhere. Measured on SEND PROGRESS,
            // not on idleness: a peer that has stopped reading is busy by every
            // other measure, and the slot is what it is holding.
            if (c.pending != PEND_NONE) {
                // Armed here rather than where the answer was staged: this is
                // the one place with a clock, and threading now_ms through three
                // framing paths to save a beat of resolution is not worth it.
                if (c.tx_since == 0 || c.tx_sent != c.tx_mark) {
                    c.tx_mark  = c.tx_sent;
                    c.tx_since = now_ms;
                    continue;
                }
                if (now_ms - c.tx_since < Config::MODBUS_TCP_IDLE_TIMEOUT_MS) continue;
                _ctx.rejects++;
                _drop(c, nullptr);      // routine, not an event: see _drop()
                continue;
            }

            if (now_ms - c.last_ms < Config::MODBUS_TCP_IDLE_TIMEOUT_MS) continue;
            _ctx.rejects++;
            _drop(c, nullptr);          // routine, not an event: see _drop()
        }
    }

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_TCP_SERVER
