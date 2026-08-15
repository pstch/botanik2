#ifndef BOTANIK_SIM_MBCHECK_H
#define BOTANIK_SIM_MBCHECK_H

/**
 * @file mbcheck.h
 * @brief Sim-only: a Modbus TCP master that talks to this firmware's own slave,
 *        once, at boot, and says whether it worked.
 *
 * Simulator scaffolding, so it lives here and not in the library; the master it
 * drives (@ref Botanik::Modbus::TcpMaster) is ordinary firmware, and what is
 * sim-only is pointing one at ourselves.
 *
 * Nothing is mocked: a real socket to the real listener, real MBAP frames, the
 * real register map over the real variable table, and `Bank::set()` doing the
 * writing. The round trip is self-checking rather than value-dependent (read a
 * parameter, write it something else, read it back and compare), so it needs no
 * golden and cannot pass by coincidence - and it runs on the shipping code path
 * every time the simulator does.
 */

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "libbotanik_config.h"

#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT

#include "libbotanik/hal/net.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/modbus/tcp/master.h"
#include "libbotanik/vars/model.h"

namespace Botanik { namespace Sim {

    /**
     * @brief One scripted exchange against our own Modbus TCP slave.
     *
     * Stepped from the sim's main loop; it does nothing after it has reported.
     * A state machine like everything else here: one bounded action per beat.
     */
    class ModbusCheck {

        /** @name State
         *  @{ */

        enum Step : uint8_t {
            MC_WAIT,     ///< the one beat spent resolving the row
            MC_READ1,    ///< read the row as it stands
            MC_WRITE,    ///< write it something else
            MC_READ2,    ///< read it back
            MC_DONE      ///< reported; silent from here
        };

        Modbus::TcpMaster _master;
        Vars::Bank*       _io;
        Logs::Logger*     _logger;
        Step              _step  = MC_WAIT;
        uint32_t          _was   = 0;   ///< what the row read before the write
        bool              _ok    = true;

        /** @brief The row this exercises: a parameter with a register, whose
         *  value nothing else in the sim depends on. Writing a setpoint would
         *  move the plant; writing the slave's own port would move the slave.
         *
         *  Its ADDRESS is asked of the bank rather than written down here: a
         *  number copied into this file goes stale silently, and the check then
         *  fails against a perfectly good slave. */
        static constexpr const char* ROW_NAME  = "ntp_poll_sec";
        static const uint32_t        NEW_VALUE = 1234;
        uint16_t _row_addr = 0;

        /** @} */

    public:
        ModbusCheck(const SetupContext& ctx, Vars::Bank* io, Logs::Logger* logger)
            : _master(ctx, "modbus master (sim)"), _io(io), _logger(logger) {}

        /** @brief Advance one beat. Safe to call forever. */
        void step(uint64_t now_ms) {
            if (_step == MC_DONE) return;

            // The first real beat is early enough: the listener is bound in
            // Registry::setup(), which has already run by the time the main loop
            // calls us. No delay for the bring-up log burst either, since
            // _report() writes to stderr rather than through the logger.
            if (_step == MC_WAIT) {
                const Vars::Var v = _io->var(ROW_NAME);
                if (v.meta()->name == nullptr || v.meta()->bind.reg == 0) {
                    _report("skipped", "'%s' has no register", ROW_NAME);
                    _step = MC_DONE;
                    return;
                }
                _row_addr = (uint16_t)(v.meta()->bind.reg * 2);
                _master.connect_to(HAL::Network::mock_loopback_ip(),
                                   Config::MODBUS_TCP_PORT);
                _step = MC_READ1;
            }

            _master.step(now_ms, Tasks::PHASE_SERVICE);
            if (!_master.connected()) return;

            if (_master.done()) {
                _consume(now_ms);
                return;
            }
            _issue();
        }

        bool passed() const { return _step == MC_DONE && _ok; }

    private:
        /** @brief Start whatever this step asks for, if nothing is in flight. */
        void _issue() {
            switch (_step) {
                case MC_READ1:
                case MC_READ2:
                    _master.read(Config::MODBUS_TCP_UNIT_ID, Modbus::FC_READ_HOLDING,
                                 _row_addr, 2);
                    return;
                case MC_WRITE: {
                    // Big-endian register bytes, high word first: the map's own
                    // convention, which the master does not reinterpret.
                    uint8_t regs[4];
                    Modbus::wr16(regs + 0, (uint16_t)(NEW_VALUE >> 16));
                    Modbus::wr16(regs + 2, (uint16_t)(NEW_VALUE & 0xFFFF));
                    _master.write_registers(Config::MODBUS_TCP_UNIT_ID,
                                            _row_addr, 2, regs);
                    return;
                }
                default:
                    return;
            }
        }

        /** @brief Take the result of the step that just finished and move on. */
        void _consume(uint64_t now_ms) {
            (void)now_ms;
            const Modbus::Error e = _master.error();
            const Modbus::Response& r = _master.response();

            if (e != Modbus::ERR_OK) {
                _fail("transaction failed", (unsigned)e);
                _master.take_result();
                return;
            }

            switch (_step) {
                case MC_READ1:
                    _was = _u32(r);
                    _step = MC_WRITE;
                    break;
                case MC_WRITE:
                    _step = MC_READ2;
                    break;
                case MC_READ2: {
                    const uint32_t now = _u32(r);
                    if (now != NEW_VALUE) _fail("read-back mismatch", (unsigned)now);
                    else _report("ok", "read %u, wrote %u, read back %u",
                                 (unsigned)_was, (unsigned)NEW_VALUE, (unsigned)now);
                    // Put it back, so the check leaves the unit as it found it.
                    _restore();
                    _step = MC_DONE;
                    break;
                }
                default:
                    break;
            }
            _master.take_result();
        }

        /** @brief The 32-bit value a two-register payload carries. */
        static uint32_t _u32(const Modbus::Response& r) {
            if (r.data == nullptr || r.len < 4) return 0;
            return ((uint32_t)Modbus::reg_at(r.data, 0) << 16) |
                    Modbus::reg_at(r.data, 1);
        }

        /** @brief Return the row to what it was, through the bank rather than
         *  the wire (the wire half has already been tested).
         *
         *  Persisted, not merely set: the WRITE half went through the slave's
         *  map, which makes a master's write durable, so a restore that only
         *  touched RAM would leave this check's own test value in the store and
         *  the row reading unsaved from every boot onward. */
        void _restore() {
            if (_io == nullptr) return;
            Vars::Var v = _io->var(ROW_NAME);
            if (v.meta()->name == nullptr) return;
            (void)_io->set(v, Vars::Scalar::U((uint64_t)_was));
            (void)_io->save(v);
        }

        void _fail(const char* why, unsigned detail) {
            _ok   = false;
            _step = MC_DONE;
            _report("FAILED", "%s (%u)", why, detail);
        }

        /** @brief Say what happened, where it cannot be lost: stderr, and
         *  deliberately NOT the firmware's logger. The sim's log sinks are
         *  baud-paced and drop entries under load, and this verdict belongs to
         *  whoever ran the simulator rather than in the log an operator reads
         *  (or in the home page's log tail). */
        void _report(const char* verdict, const char* fmt, ...)
            __attribute__((format(printf, 3, 4))) {
            char detail[96];
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(detail, sizeof(detail), fmt, ap);
            va_end(ap);
            fprintf(stderr, "\r\n[modbus self-check] %s: %s\r\n", verdict, detail);
            fflush(stderr);
        }
    };

}}  // namespace Botanik::Sim

#endif // BOTANIK_FEATURE_MODBUS_TCP_CLIENT

#endif // BOTANIK_SIM_MBCHECK_H
