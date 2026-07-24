// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_CONFIG_LOCAL_H
#define LIBBOTANIK_CORE_CONFIG_LOCAL_H

/**
 * @file config_local.h
 * @brief LocalConfigHandler - the config backend when there is no PLC.
 *
 * Owns the config image in RAM, loads each library parameter from HAL::Storage
 * at boot (falling back to the zeroed default when absent), and on an edit
 * writes the parameter back to storage and bumps its group serial so the
 * config-driven Components re-apply. Selected by BotanikFirmware when
 * BOTANIK_FEATURE_PLC is off (where the network stack is also off, so geo is the
 * live consumer).
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/core/config.h"
#include "libbotanik/hal/storage.h"

namespace Botanik { namespace Cfg {

    /** @brief Flash/file-backed configuration backend (see file header). */
    class LocalConfigHandler : public ConfigHandler {

        /** @name State
         *  @{ */

        /** @brief The live config image (loaded from storage, edited in place). */
        PlcIO::PlcOutputConfig_t _image;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        explicit LocalConfigHandler(Logs::Logger* logger)
            : ConfigHandler(logger) { memset(&_image, 0, sizeof(_image)); }

        /** @} */

        /** @name Backend surface
         *  @{ */

        const PlcIO::PlcOutputConfig_t* image() const override { return &_image; }

        bool set(const ConfigParam& p, const char* text) override {
            if (!write_value(p, text, &_image)) return false;
            HAL::Storage::set(p.name, (const uint8_t*)&_image + p.offset, param_width(p));
            bump_group(p.group);
            return true;
        }

        /** @} */

    protected:
        /** @brief Load every library parameter from storage into the image
         *  (missing keys keep the zero default), then seed each group serial to 1
         *  so each consumer applies its config exactly once at boot. */
        void setup(uint64_t /*now_ms*/) override {
            for (uint8_t i = 0; i < CONFIG_PARAM_COUNT; i++) {
                const ConfigParam& p = CONFIG_PARAMS[i];
                HAL::Storage::get(p.name, (uint8_t*)&_image + p.offset,
                                  param_width(p), nullptr);
            }
            for (uint8_t g = 0; g < CFG_GROUP_COUNT; g++) {
                uint32_t one = 1;
                memcpy((uint8_t*)&_image + group_serial_offset(g), &one, 4);
            }
        }

    private:
        /** @brief Increment the given group's serial word in the image. */
        void bump_group(uint8_t group) {
            uint8_t* s = (uint8_t*)&_image + group_serial_offset(group);
            uint32_t v;
            memcpy(&v, s, 4);
            v += 1;
            memcpy(s, &v, 4);
        }
    };

}}

#endif // LIBBOTANIK_CORE_CONFIG_LOCAL_H
