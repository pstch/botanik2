#ifndef BOTANIK_SIM_MBCHECK_H
#define BOTANIK_SIM_MBCHECK_H

/**
 * @file mbcheck.h
 * @brief Sim-only: a Modbus TCP master that talks to this firmware's own slave,
 *        once, at boot, and says whether it worked.
 *
 * Simulator scaffolding, so it lives here and not in the library - but the
 * master it drives (@ref Botanik::Modbus::TcpMaster) is ordinary firmware. What
 * is sim-only is pointing one at ourselves.
 *
 * @section mb_what What it proves
 *
 * The whole path, with nothing mocked: a real socket to the real listener, real
 * MBAP frames, the real register map over the real variable table, and
 * `Bank::set()` doing the writing. It runs a round trip that is self-checking
 * rather than value-dependent - read a parameter, write it something else, read
 * it back and compare - so it needs no golden and cannot pass by coincidence.
 *
 * @section mb_why_here Why at boot, in the sim, rather than a test
 *
 * Because it is the first thing that answers "does the slave actually serve?"
 * and it answers it every time the simulator runs, on the shipping code path. A
 * unit test would have to build a bank and a listener of its own, and would
 * then be testing that arrangement.
 */

#include <cstdarg>                    // va_list - the verdict formatter
#include <cstdint>
#include <cstdio>                     // fprintf - a verdict the log sink cannot drop

#include "libbotanik_config.h"

#if BOTANIK_FEATURE_MODBUS_TCP_CLIENT

#include "libbotanik/hal/net.h"           // mock_loopback_ip - where our own slave listens
#include "libbotanik/logs/logger.h"       // the verdict goes to the log like anything else
#include "libbotanik/modbus/tcp/master.h" // Modbus::TcpMaster
#include "libbotanik/vars/model.h"        // Vars::Bank - to name the row under test

namespace Botanik { namespace Sim {

    /**
     * @brief One scripted exchange against our own Modbus TCP slave.
     *
     * Stepped from the sim's main loop; it does nothing after it has reported.
     * Shaped as a state machine for the same reason everything else here is: one
     * bounded action per beat, no waiting.
     */
    class ModbusCheck {

        /** @name State
         *  @{ */

        enum Step : uint8_t {
            MC_WAIT,     ///< letting the firmware finish booting
            MC_READ1,    ///< read the row as it stands
            MC_WRITE,    ///< write it something else
            MC_READ2,    ///< read it back
            MC_DONE      ///< reported; silent from here
        };

        Modbus::TcpMaster _master;
        Vars::Bank*       _io;
        Logs::Logger*     _logger;
        Step              _step  = MC_WAIT;
        uint64_t          _start = 0;
        uint32_t          _was   = 0;   ///< what the row read before the write
        bool              _ok    = true;

        /** @brief The row this exercises: a parameter with a register, whose
         *  value nothing else in the sim depends on. Writing a setpoint would
         *  move the plant; writing the slave's own port would move the slave. */
        static const uint16_t ROW_REG   = 22;      ///< ntp_poll_sec
        static const uint16_t ROW_ADDR  = ROW_REG * 2;
        static const uint32_t NEW_VALUE = 1234;

        /** @} */

    public:
        ModbusCheck(Vars::Bank* io, Logs::Logger* logger)
            : _master(logger), _io(io), _logger(logger) {}

        /** @brief Advance one beat. Safe to call forever. */
        void step(uint64_t now_ms) {
            if (_step == MC_DONE) return;

            // Deliberately past the boot storm, not merely past bring-up: the
            // listener is bound in Registry::setup(), but for the first seconds
            // the log sinks are still draining the bring-up burst, and a verdict
            // emitted into that is a verdict nobody ever reads.
            if (_step == MC_WAIT) {
                if (_start == 0) _start = now_ms;
                if ((now_ms - _start) < 6000) return;
                _master.connect_to(HAL::Network::mock_loopback_ip(),
                                   Config::MODBUS_TCP_PORT);
                _step = MC_READ1;
            }

            _master.step(now_ms);
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
                    _master.read_registers(Config::MODBUS_TCP_UNIT_ID, ROW_ADDR, 2);
                    return;
                case MC_WRITE: {
                    // Big-endian register bytes, high word first - the map's own
                    // convention, and the master does not reinterpret it.
                    uint8_t regs[4];
                    Modbus::wr16(regs + 0, (uint16_t)(NEW_VALUE >> 16));
                    Modbus::wr16(regs + 2, (uint16_t)(NEW_VALUE & 0xFFFF));
                    _master.write_registers(Config::MODBUS_TCP_UNIT_ID,
                                            ROW_ADDR, 2, regs);
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
         *  the wire: the wire half is what was under test, and it has been. */
        void _restore() {
            if (_io == nullptr) return;
            Vars::Var v = _io->var("ntp_poll_sec");
            if (v.meta()->name != nullptr)
                (void)_io->set(v, Vars::Scalar::U((uint64_t)_was));
        }

        void _fail(const char* why, unsigned detail) {
            _ok   = false;
            _step = MC_DONE;
            _report("FAILED", "%s (%u)", why, detail);
        }

        /** @brief Say what happened, where it cannot be lost.
         *
         *  To stderr, and deliberately NOT through the firmware's logger.
         *
         *  Two reasons, and the second is the one that decided it. The sim's log
         *  sinks are baud-paced and drop entries under load, so a verdict sent
         *  that way reads as "the check never ran" whenever it is busy. And this
         *  is scaffolding: its verdict belongs to whoever ran the simulator, not
         *  in the log an operator reads to understand the plant - where it would
         *  also sit in the home page's log tail, moving whatever was there. */
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
