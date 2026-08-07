#ifndef MODBUS_RTU_DEVICE_H
#define MODBUS_RTU_DEVICE_H

/**
 * @file modbus/rtu/device.h
 * @brief What one MODEL of field device will and will not answer.
 *
 * @section dev_why Why this is not in the variable table
 *
 * A row says where a signal lives. It cannot say that this meter refuses a
 * request for more than 40 values, or that it answers exception 02 to any
 * request whose start address is odd, because those are facts about the DEVICE
 * and the same fact would then be repeated on all 37 of its rows.
 *
 * They are also not facts about the bus: two models on the same RS-485 segment
 * disagree about all of them. So they sit here, one entry per model, and the
 * site names which model answers at which unit address.
 *
 * @section dev_notprofile What this is deliberately NOT
 *
 * Not a device profile in the SCADA sense: there is no per-signal map here, no
 * object index, no register list. That was considered and rejected - a
 * translation table from object index to register would be a second hand-copy of
 * the vendor PDF the addresses already came from, with a second chance to be
 * wrong and nothing to check it against. Addresses live in the row, once.
 *
 * What is here is the handful of scalars a PLANNER needs in order to cut a legal
 * request: how many registers may be asked for at once, what the address and
 * quantity must be multiples of, whether the multi-writes exist, and how much
 * silence the device wants afterwards.
 *
 * @section dev_unknown A device nobody described
 *
 * generic_model() is what a unit no table names gets: the protocol's own limits
 * and nothing assumed. That is the honest default - a model entry is only ever
 * as good as the manual behind it, and inventing constraints for a device whose
 * datasheet nobody has read would be worse than admitting we do not know.
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
        // @warning Positional initialisation, C++11 having no designated
        // initialisers: a field inserted anywhere but the END silently
        // reinterprets every existing literal rather than failing to compile.
        // Adding max_bits here turned four test models into nonsense that still
        // built. Append, or expect to fix every site by hand.

        /** @brief For logs and the `modbus` command. Never matched on. */
        const char* name;
        /** @brief Most registers one read may ask for; 0 means the protocol's
         *  own ceiling. The SDM630 refuses past 40 values, i.e. 80 registers,
         *  "prompting the SDM630Modbus to generate an exception response". */
        uint16_t    max_regs;
        /** @brief Most coils one read or write may cover; 0 means the
         *  protocol's own. A relay board with eight relays says 8, and it is a
         *  separate number from max_regs because a device can have plenty of
         *  one and none of the other. */
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

    /** @brief One unit address on the segment, and the model answering there. */
    struct Device {
        uint8_t            unit;
        const DeviceModel* model;
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

    /** @} */

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
#endif // MODBUS_RTU_DEVICE_H
