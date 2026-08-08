#ifndef BOTANIK_SIM_FIELDBUS_H
#define BOTANIK_SIM_FIELDBUS_H

/**
 * @file fieldbus.h
 * @brief Sim-only: the field devices at the other end of the RS-485 wire.
 *
 * The firmware holds one end of a socketpair (hal/simulator.h); this holds the
 * other, with one @ref Botanik::Modbus::RtuSlave per device the site's variable
 * table addresses. Bytes really travel, so what is exercised is the master's own
 * framing, its CRCs, its timeouts and its backoff - not a mock of them.
 *
 * @section fb_values Where the values come from
 *
 * `Site::simulate()` computes the plant into the variable bank, as it always
 * has. This copies those values OUT of the bank into the devices' register
 * images, using each row's own `{device, fc, reg}` binding - the same binding
 * the master's planner reads - and then the master polls them back in over the
 * bus. The number a control program sees has therefore been encoded, framed,
 * checksummed, put on a wire, parsed and scaled on the way in.
 *
 * That is deliberately a LOOP rather than a replacement: the sim keeps producing
 * values the way it always did, and the bus becomes the path they take. Cutting
 * `simulate()` off from the bank entirely is the next step, and it only becomes
 * safe once this loop is known to carry everything.
 *
 * @section fb_outputs And back the other way
 *
 * Coils the firmware drives land in the devices' coil images, where the sim can
 * read them - which is the first time an actuator command in the simulator has
 * been observable as something other than a variable the firmware wrote to
 * itself.
 */

#include <cstdint>

#include "libbotanik_config.h"

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER && defined(TARGET_SIMULATOR)

#include <unistd.h>                       // read / write - the peer end of the pair

#include "libbotanik/hal/simulator.h"     // fieldbus_peer_fd
#include <cstring>                        // memcpy - staging a device answer
#include "libbotanik/modbus/rtu/slave.h"  // Modbus::RtuSlave
#include "libbotanik/vars/model.h"        // Vars::Bank - where the simulated values are

namespace Botanik { namespace Sim {

    /**
     * @brief The devices on the simulated bus, and the pump that runs them.
     *
     * Stepped from the sim's main loop, after the beat: whatever the firmware
     * wrote goes to the devices, and whatever they answer goes back.
     */
    class FieldBus {

        /** @name State
         *  @{ */

        static const uint8_t MAX_DEVICES = 16;

        /** @brief How long a simulated device thinks before answering.
         *
         *  Real devices are 2-30 ms, the cheap ones 20-50, so this is the honest
         *  end of that range. It used to be load-bearing for another reason -
         *  the master flushed its receive path a beat after sending, and an
         *  answer arriving before the flush was thrown away, so the turnaround
         *  had to outlast the race. That flush is gone; the master now listens
         *  from the moment the frame is queued. The number stays because it is
         *  what a device does, not because the harness needs it to. */
        static const uint32_t TURNAROUND_MS = 30;

        Modbus::RtuSlave _dev[MAX_DEVICES];
        uint8_t          _count = 0;
        Vars::Bank*      _io    = nullptr;
        uint32_t         _frames = 0;   ///< requests answered, all devices

        /** @brief The answer a device has ready, and when it may go out
         *  (see TURNAROUND_MS). */
        uint8_t  _pending[Modbus::ADU_MAX] = {};
        uint16_t _pending_len = 0;
        uint64_t _pending_at  = 0;

        /** @} */

    public:
        /** @brief Learn the devices from the table itself: every distinct unit
         *  id an XP_MODBUS row names is a box that has to answer. There is no
         *  device list to keep in step with the variables. */
        void bind(Vars::Bank* io) {
            _io = io;
            _count = 0;
            if (io == nullptr) return;
            for (uint16_t i = 0; i < io->count(); i++) {
                const Vars::Variable& d = *io->var(i).meta();
                if (d.bind.transport != Vars::XP_MODBUS || d.bind.device == 0) continue;
                bool known = false;
                for (uint8_t k = 0; k < _count; k++)
                    if (_dev[k].unit() == d.bind.device) { known = true; break; }
                if (known || _count >= MAX_DEVICES) continue;
                _dev[_count].set_unit(d.bind.device);
                _count++;
            }
        }

        uint8_t  device_count() const { return _count; }
        uint32_t frames() const       { return _frames; }

        /** @brief Copy this beat's simulated values into the devices, then carry
         *  the wire in both directions. */
        void step(uint64_t now_ms) {
            _publish();
            _pump(now_ms);
        }

    private:
        /** @brief The bank's INPUT rows into the register images they are read
         *  from. Only inputs: an output row is what the firmware drives, and
         *  writing it here would be the sim answering its own command. */
        void _publish() {
            if (_io == nullptr) return;
            for (uint16_t i = 0; i < _io->count(); i++) {
                const Vars::Var v = _io->var(i);
                const Vars::Variable& d = *v.meta();
                if (d.bind.transport != Vars::XP_MODBUS) continue;
                if (d.role != Vars::ROLE_INPUT) continue;
                Modbus::RtuSlave* dev = _find(d.bind.device);
                if (dev == nullptr) continue;

                if (d.bind.fc == Modbus::FC_READ_COILS ||
                    d.bind.fc == Modbus::FC_READ_DISCRETE) {
                    dev->set_coil(d.bind.reg, v.b());
                    continue;
                }
                // The wire carries what the row says it carries: a REAL spans
                // two registers, high word first, and anything narrower is one.
                if (d.bind.wire == Vars::T_F32) {
                    const float f = v.value();
                    uint32_t bits = 0;
                    memcpy(&bits, &f, 4);
                    dev->set_reg(d.bind.reg, (uint16_t)(bits >> 16));
                    dev->set_reg((uint16_t)(d.bind.reg + 1), (uint16_t)(bits & 0xFFFF));
                } else {
                    dev->set_reg(d.bind.reg, (uint16_t)v.i());
                }
            }
        }

        /** @brief Everything the firmware wrote to the devices, and everything
         *  they answer. Bounded per call like any other beat work. */
        void _pump(uint64_t now_ms) {
            const int fd = HAL::Serial::fieldbus_peer_fd();
            if (fd < 0) return;

            // An answer that has finished thinking goes out first, so a device
            // is never asked a second question while still holding the first
            // one's reply.
            if (_pending_len > 0 && now_ms >= _pending_at) {
                const ssize_t w = ::write(fd, _pending, _pending_len);
                (void)w;
                _pending_len = 0;
                _frames++;
            }

            uint8_t buf[256];
            for (uint8_t pass = 0; pass < 4; ++pass) {
                const ssize_t n = ::read(fd, buf, sizeof(buf));
                if (n <= 0) break;
                // Every device hears every byte: it is one wire, and deciding
                // whether a frame is theirs is the device's own job (the unit id
                // and the checksum).
                for (uint8_t k = 0; k < _count; k++) _dev[k].feed(buf, (uint16_t)n);
            }

            // One answer in flight, because it is one wire: two devices talking
            // at once is a collision, and a stream cannot represent one.
            if (_pending_len > 0) return;
            for (uint8_t k = 0; k < _count; k++) {
                // Repeatedly, not once: every device hears every frame, so a
                // device is normally holding several that belong to other units.
                // Draining one per pump let that backlog grow until its own
                // answer came out too late for the master's deadline - which
                // looked exactly like a device flapping in and out of backoff.
                // poll() consumes a frame whether or not it belongs to this
                // device, and answers only its own - so keep going: a zero means
                // "that one was not mine", not "there is nothing left".
                for (uint8_t pass = 0; pass < MAX_DEVICES + 2; ++pass) {
                    const uint16_t n = _dev[k].poll(_pending, (uint16_t)sizeof(_pending));
                    if (n == 0) continue;
                    _pending_len = n;
                    _pending_at  = now_ms + TURNAROUND_MS;
                    return;
                }
            }
        }

        Modbus::RtuSlave* _find(uint8_t unit) {
            for (uint8_t k = 0; k < _count; k++)
                if (_dev[k].unit() == unit) return &_dev[k];
            return nullptr;
        }
    };

}}  // namespace Botanik::Sim

#endif // BOTANIK_FEATURE_MODBUS_RTU_SERVER && TARGET_SIMULATOR

#endif // BOTANIK_SIM_FIELDBUS_H
