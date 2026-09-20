#ifndef MODBUS_RTU_SLAVE_H
#define MODBUS_RTU_SLAVE_H

/**
 * @file modbus/rtu/slave.h
 * @brief RtuSlave: one field device, answering on a shared RS-485 wire.
 *
 * The mirror image of @ref modbus/rtu/master.h. Written for the simulator's
 * field devices, but ordinary firmware: a unit that has to APPEAR as a device to
 * somebody else's master uses the same object.
 * @note The rationale behind this file is @ref sl_framing.
 */

#include <cstdint>

#include "libbotanik/config/modbus.gen.h"

#include "libbotanik/modbus/wire.h"

#if BOTANIK_FEATURE_MODBUS_RTU_SERVER

namespace Botanik { namespace Modbus {

    /**
     * @brief One addressable device: a unit id, a register image, a coil image.
     *
     * Fed bytes, it answers bytes. It holds no socket and no clock - the caller
     * owns the wire, which is what lets several of these share one.
     */
    class RtuSlave {

        /** @name State
         *  @{ */

        uint8_t  _unit = 0;
        uint16_t _regs[Config::MODBUS_SIM_REGS] = {};
        uint8_t  _coils[(Config::MODBUS_SIM_COILS + 7) / 8] = {};
        /** @brief The request being accumulated. */
        uint8_t  _req[ADU_MAX] = {};
        uint16_t _req_len = 0;

        uint32_t _served   = 0;   ///< requests answered
        uint32_t _refused  = 0;   ///< answered with an exception
        uint32_t _dropped  = 0;   ///< discarded: bad CRC, or not ours

        /** @} */

    public:
        explicit RtuSlave(uint8_t unit = 0) : _unit(unit) {}

        void set_unit(uint8_t unit) { _unit = unit; }
        uint8_t unit() const        { return _unit; }

        /** @name The image
         *
         *  Whoever owns the device writes what its registers hold; whoever reads
         *  the bus finds it there. Out-of-range is ignored rather than clamped -
         *  a register this device does not have is not a register.
         *  @{ */
        void     set_reg(uint16_t addr, uint16_t v);
        uint16_t reg(uint16_t addr) const;
        void     set_coil(uint16_t addr, bool on);
        bool     coil(uint16_t addr) const;
        /** @} */

        /** @name The wire
         *  @{ */

        /** @brief Take @p n bytes off the wire. */
        void feed(const uint8_t* bytes, uint16_t n);

        /** @brief If a complete request has arrived, answer it into @p out.
         *
         *  @return the response length, or 0 when there is nothing to say - the
         *  request is incomplete, addressed to another unit, or its checksum is
         *  wrong. A slave that answers a corrupt frame is worse than one that
         *  stays quiet: the master's retry is the recovery, and a wrong answer
         *  defeats it.
         */
        uint16_t poll(uint8_t* out, uint16_t cap);

        /** @brief Forget a partial request (a wire that went quiet). */
        void reset() { _req_len = 0; }

        /** @} */

        /** @name Counters
         *  @{ */
        uint32_t served() const  { return _served; }
        uint32_t refused() const { return _refused; }
        uint32_t dropped() const { return _dropped; }
        /** @} */

    private:
        /** @brief Bytes this request will occupy once complete, or 0 while that
         *  is not yet knowable. */
        uint16_t _expected() const;
        /** @brief Build an exception response for @p fc. */
        uint16_t _except(uint8_t fc, Exception ex, uint8_t* out, uint16_t cap);
        /** @brief Answer a read; @p out already framed by the caller. */
        uint16_t _do_read(uint8_t fc, uint16_t addr, uint16_t qty,
                          uint8_t* out, uint16_t cap);
        uint16_t _do_write(uint8_t fc, const uint8_t* pdu, uint16_t pdu_len,
                           uint8_t* out, uint16_t cap);
    };

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_SERVER

#endif // MODBUS_RTU_SLAVE_H
