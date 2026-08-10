#ifndef BOTANIK_SIM_FIELDBUS_H
#define BOTANIK_SIM_FIELDBUS_H

/**
 * @file fieldbus.h
 * @brief Sim-only: the field devices at the other end of the RS-485 wire.
 *
 * The firmware holds one end of a socketpair (hal/simulator.h); this holds the
 * other, with one @ref Botanik::Modbus::RtuSlave per device the site's variable
 * table addresses. Bytes really travel, so what is exercised is the master's own
 * framing, CRCs, timeouts and backoff, not a mock of them.
 *
 * `Site::simulate()` computes the plant; those values go into the devices'
 * register images through each row's own `{device, fc, reg}` binding, and the
 * master polls them back over the bus. Coils the firmware drives land in the
 * devices' coil images, where the sim can read them.
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
     * Stepped from the sim's main loop after the beat: whatever the firmware
     * wrote goes to the devices, and whatever they answer comes back.
     */
    class FieldBus {

        /** @name State
         *  @{ */

        /** @brief How many distinct units the harness can stand up. A host-side
         *  bound, not a protocol one: each slave carries its own register and
         *  coil image, so it costs about a megabyte of the sim process. Keep it
         *  well clear of the site's device count - a unit past the ceiling never
         *  answers, which reads exactly like a wiring fault (@c dropped() is
         *  what says otherwise). */
        static const uint8_t MAX_DEVICES = 64;

        /** @brief How long a simulated device thinks before answering. Real
         *  devices are 2-30 ms, the cheap ones 20-50, so this is the honest end
         *  of that range. The master listens from the moment the frame is
         *  queued, so no answer can arrive too early to be heard. */
        static const uint32_t TURNAROUND_MS = 30;

        Modbus::RtuSlave _dev[MAX_DEVICES];
        uint8_t          _count = 0;
        uint8_t          _dropped = 0;  ///< units past MAX_DEVICES, see bind()
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
         *  id an XP_MODBUS row names is a box that has to answer. */
        void bind(Vars::Bank* io) {
            _io = io;
            _count = 0;
            _dropped = 0;
            if (io == nullptr) return;

            // Distinctness comes from a bitmap over the whole unit space, not
            // from scanning the devices already made: O(1) per row, and it also
            // sees units past the ceiling, which have no device to scan and
            // would otherwise be counted once per row.
            uint8_t seen[32] = {};
            for (uint16_t i = 0; i < io->count(); i++) {
                const Vars::Variable& d = *io->var(i).meta();
                if (d.bind.transport != Vars::XP_MODBUS || d.bind.device == 0) continue;
                const uint8_t u = d.bind.device;
                if ((seen[u >> 3] & (uint8_t)(1u << (u & 7))) != 0) continue;
                seen[u >> 3] |= (uint8_t)(1u << (u & 7));
                if (_count >= MAX_DEVICES) { _dropped++; continue; }
                _dev[_count].set_unit(u);
                _count++;
            }
        }

        uint8_t  device_count() const { return _count; }

        /** @brief Units the table names that this harness could not stand up.
         *  Nonzero means every row on them times out and looks like hardware
         *  that is not there, so the caller is expected to say so. */
        uint8_t  dropped() const      { return _dropped; }
        uint32_t frames() const       { return _frames; }

        /** @brief Copy this beat's simulated values into the devices, then carry
         *  the wire in both directions. */
        void step(uint64_t now_ms) {
            _pump(now_ms);
        }

        /**
         * @brief Put one simulated reading into the device that carries it.
         *
         * The ONLY way the plant's values enter the bus: the bank row is written
         * by the MASTER and by nothing else, which is what makes the poll
         * interval visible on screen.
         *
         * @param d the row's descriptor, for its device / fc / reg binding
         * @param v the physical quantity, in the row's own engineering units
         */
        void set_physical(const Vars::Variable& d, float v) {
            if (d.bind.transport != Vars::XP_MODBUS) return;
            Modbus::RtuSlave* dev = _find(d.bind.device);
            if (dev == nullptr) return;

            if (d.bind.fc == Modbus::FC_READ_COILS ||
                d.bind.fc == Modbus::FC_READ_DISCRETE) {
                dev->set_coil(d.bind.reg, v != 0.0f);
                return;
            }
            // The wire carries what the row says: a REAL spans two registers,
            // high word first, and anything narrower is one.
            if (d.bind.wire == Vars::T_F32) {
                uint32_t bits = 0;
                memcpy(&bits, &v, 4);
                dev->set_reg(d.bind.reg, (uint16_t)(bits >> 16));
                dev->set_reg((uint16_t)(d.bind.reg + 1), (uint16_t)(bits & 0xFFFF));
            } else {
                dev->set_reg(d.bind.reg, (uint16_t)(int32_t)v);
            }
        }

    private:

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
                // Repeatedly, not once: every device hears every frame, so one
                // normally holds several that belong to other units, and a
                // backlog drained one per pump makes its own answer miss the
                // master's deadline. poll() consumes a frame whether or not it
                // is this device's and answers only its own, so a zero means
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
