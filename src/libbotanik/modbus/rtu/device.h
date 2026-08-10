#ifndef MODBUS_RTU_DEVICE_H
#define MODBUS_RTU_DEVICE_H

/**
 * @file modbus/rtu/device.h
 * @brief What one MODEL of field device will and will not answer.
 *
 * A row says where a signal lives. It cannot say that this meter refuses a
 * request for more than 40 values, or answers exception 02 to any request whose
 * start address is odd: those are facts about the DEVICE, and not about the bus
 * either, since two models on one RS-485 segment disagree about all of them. So
 * they sit here, one entry per model, and the site names which model answers at
 * which unit address.
 *
 * Not a device profile in the SCADA sense: no per-signal map, no object index,
 * no register list. Addresses live in the row, once. What is here is what a
 * PLANNER needs to cut a legal request: how many registers may be asked for at
 * once, what the address and quantity must be multiples of, whether the
 * multi-writes exist, and how much silence the device wants afterwards.
 *
 * generic_model() is what a unit no table names gets: the protocol's own limits
 * and nothing assumed, since a model entry is only as good as the manual behind
 * it.
 */

#include <cstdint>

#include "libbotanik/config/base.h"    // BOTANIK_FEATURE_MODBUS_* - the gate below

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

    /** @name The model
     *  @{ */

    /**
     * @brief The constraints one model of device puts on a request.
     *
     * An aggregate with no constructor, so a site's table is constant
     * initialised straight into flash like every other table here.
     */
    struct DeviceModel {
        // Build through the factories below, never brace-initialised: C++11 has
        // no designated initialisers, so a field inserted anywhere but the end
        // silently reinterprets every existing literal rather than failing to
        // compile. The factories take only the arguments a shape can use.

        /** @brief For logs and the `modbus` command. Never matched on. */
        const char* name;
        /** @brief Most registers one read may ask for; 0 means the protocol's
         *  own ceiling. The SDM630 refuses past 40 values, i.e. 80 registers,
         *  "prompting the SDM630Modbus to generate an exception response". */
        uint16_t    max_regs;
        /** @brief Most coils one read or write may cover; 0 means the
         *  protocol's own. Separate from max_regs because a device can have
         *  plenty of one and none of the other. */
        uint16_t    max_bits;
        /** @brief What a start address must be a multiple of. 2 on a device
         *  that stores 32-bit values in register pairs and refuses a request
         *  landing in the middle of one - exception 02 on both meters here. */
        uint8_t     addr_align;
        /** @brief What a quantity must be a multiple of; usually the same. */
        uint8_t     qty_align;
        /** @brief Whether FC15/FC16 exist. A model without them has to be
         *  driven a point at a time with FC05/FC06, which is also the case
         *  where a single write's echoed VALUE is the only confirmation the
         *  point moved (see wire.h's Expect::value). */
        bool        write_multiple;
        /** @brief Silence this model needs after its answer, in ms, when that
         *  is more than the bus default. 0 to use MODBUS_GAP_MS. */
        uint16_t    min_gap_ms;
    };

    /** @}
     *  @name Declaring one
     *
     *  One factory per SHAPE of device, taking only what that shape can say
     *  about itself. A meter has registers and no coils; a relay board has coils
     *  and nothing worth reading; a device nobody has described has neither.
     *  @{ */

    /** @brief A meter: registers only, usually in pairs.
     *
     *  @param regs  most registers one read may ask for, 0 for the protocol's.
     *  @param align what a start address and a quantity must be a multiple of;
     *               2 on anything storing 32-bit values in register pairs.
     *  @param multi whether FC15/FC16 exist. A model without them is driven a
     *               point at a time, and then a single write's echoed VALUE is
     *               the only confirmation the point moved. */
    constexpr DeviceModel meter(const char* name, uint16_t regs, uint8_t align,
                                bool multi = false, uint16_t gap_ms = 0) {
        return DeviceModel{ name, regs, 0, align, align, multi, gap_ms };
    }

    /** @brief A relay or discrete board: coils only, no alignment rule. */
    constexpr DeviceModel relay_board(const char* name, uint16_t coils,
                                      bool multi = true, uint16_t gap_ms = 0) {
        return DeviceModel{ name, 0, coils, 1, 1, multi, gap_ms };
    }

    /** @brief An analog IO board: registers, one per channel, no pairing. */
    constexpr DeviceModel io_board(const char* name, uint16_t regs,
                                   bool multi = true, uint16_t gap_ms = 0) {
        return DeviceModel{ name, regs, 0, 1, 1, multi, gap_ms };
    }

    /** @}
     *  @name The table
     *  @{ */

    /** @brief One unit address on the segment: what answers there, and what the
     *  people who wired it call it.
     *
     *  @c tag is the plant's own designation (the label on the enclosure), not
     *  the model: "dev 34 (SRR/TDC/RO-4) not answering" sends an engineer to a
     *  cabinet rather than to a spreadsheet. Optional: null falls back to the
     *  unit number alone. */
    struct Device {
        uint8_t            unit;
        const DeviceModel* model;
        const char*        tag;
    };

    /** @brief A site's devices. Borrowed and flash-resident, like every other
     *  table the firmware is handed. */
    struct DeviceTable {
        const Device* devices;
        uint8_t       count;
    };

    /** @} */

    /** @name Lookup
     *  @{ */

    /** @brief What a unit nobody described is assumed to be: the protocol's own
     *  limits, the multi-writes present, no alignment rule, no extra silence. */
    const DeviceModel& generic_model();

    /** @brief The model answering at @p unit, or generic_model() when @p t is
     *  null or does not name it. Never returns null: an unknown device is a
     *  device we make no assumptions about, not an error. */
    const DeviceModel& model_of(const DeviceTable* t, uint8_t unit);

    /** @brief What the plant calls the device at @p unit, or nullptr when the
     *  table does not name it. Never invents one: a missing tag reads as the
     *  bare unit number. */
    const char* device_tag(const DeviceTable* t, uint8_t unit);

    /** @brief Render a device for a human: "34 (SRR/TDC/RO-4)", or bare "34"
     *  when it has no tag.
     *
     *  Returns @p buf so it drops straight into a "%s" argument. One helper for
     *  every log site, since a device spelled two ways reads as two devices.
     *  Truncates rather than overflows; @p n of 0 is a no-op. */
    const char* device_label(const DeviceTable* t, uint8_t unit,
                             char* buf, uint8_t n);

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_DEVICE_H
