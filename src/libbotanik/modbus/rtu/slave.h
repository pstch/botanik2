#ifndef MODBUS_RTU_SLAVE_H
#define MODBUS_RTU_SLAVE_H

/**
 * @file modbus/rtu/slave.h
 * @brief RtuSlave: one field device, answering on a shared RS-485 wire.
 *
 * The mirror image of @ref modbus/rtu/client.h, and the last of the four
 * corners: RTU master, RTU slave, TCP master, TCP slave. Written for the
 * simulator's field devices, where it is what turns "the bus" from an idea into
 * bytes - but it is ordinary firmware, and a unit that has to APPEAR as a device
 * to somebody else's master uses the same object.
 *
 * @section sl_framing Framing without a length field
 *
 * This is the hard half of RTU, and it is harder for a slave than for a master.
 * A master knows what it asked, so it knows how long the answer must be
 * (@ref RtuFramer). A slave knows nothing until it has read the function code -
 * and then the length follows from it: eight bytes for the single-value
 * functions, and for the multi-writes a byte count that is itself inside the
 * frame. So a request is complete when `request_len()` says it is, and never
 * because a gap in the bytes suggested it.
 *
 * @section sl_image What it answers from
 *
 * A flat register image and a flat coil image, addressed exactly as the wire
 * addresses them. No variable table, no bank: a device on a bus is a box of
 * registers, and whoever owns the instance decides what those registers mean.
 * The simulator fills them from the site's own table (botanik_sim/fieldbus.h),
 * which is what makes the values the firmware reads back the values the
 * simulation produced.
 */

#include <cstdint>

#include "libbotanik/config/modbus.h" // Config::MODBUS_SIM_* - the image sizes

#include "libbotanik/modbus/wire.h"   // the vocabulary, shared with every other corner

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
