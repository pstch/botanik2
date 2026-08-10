/**
 * @file modbus/rtu/device.cpp
 * @brief @see device.h
 */

#include "libbotanik/modbus/rtu/device.h"

#include <cstdio>

#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT

namespace Botanik { namespace Modbus {

const DeviceModel& generic_model() {
    // Every field at what the protocol itself allows (max_regs 0 defers to
    // max_quantity(fc)): a device nobody described has constraints we have not
    // read, and guessing at them would be worse than saying so. No shape
    // factory expresses that, hence the literal.
    static const DeviceModel k = DeviceModel{ "generic", 0, 0, 1, 1, true, 0 };
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

const char* device_label(const DeviceTable* t, uint8_t unit,
                         char* buf, uint8_t n) {
    if (buf == nullptr || n == 0) return "";
    const char* tag = device_tag(t, unit);
    if (tag != nullptr && tag[0] != '\0')
        snprintf(buf, n, "%u (%s)", (unsigned)unit, tag);
    else
        snprintf(buf, n, "%u", (unsigned)unit);
    return buf;
}

const char* device_tag(const DeviceTable* t, uint8_t unit) {
    if (t == nullptr || t->devices == nullptr) return nullptr;
    for (uint8_t i = 0; i < t->count; ++i)
        if (t->devices[i].unit == unit) return t->devices[i].tag;
    return nullptr;
}

}}  // namespace Botanik::Modbus

#endif // BOTANIK_FEATURE_MODBUS_RTU_CLIENT
