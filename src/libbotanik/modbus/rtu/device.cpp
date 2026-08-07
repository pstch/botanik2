/**
 * @file modbus/rtu/device.cpp
 * @brief @see device.h
 */

#include "libbotanik/modbus/rtu/device.h"

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

const DeviceModel& generic_model() {
    // Every field left at what the protocol itself allows. A device nobody has
    // described is not a device with no constraints - it is one whose
    // constraints we have not read, and guessing at them would be worse than
    // saying so. max_regs 0 defers to max_quantity(fc).
    static const DeviceModel k = { "generic", 0, 1, 1, true, 0 };
    return k;
}

const DeviceModel& model_of(const DeviceTable* t, uint8_t unit) {
    if (t != nullptr && t->devices != nullptr) {
        for (uint8_t i = 0; i < t->count; ++i)
            if (t->devices[i].unit == unit && t->devices[i].model != nullptr)
                return *t->devices[i].model;
    }
    return generic_model();
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
