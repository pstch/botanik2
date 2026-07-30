// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_PARAMS_LOCAL_H
#define LIBBOTANIK_CORE_PARAMS_LOCAL_H

/**
 * @file config_local.h
 * @brief LocalParamHandler - the config backend when there is no PLC.
 *
 * Owns a single RAM image, laid out as the site's PLC output payload (@p Out),
 * so parameters address it by their out_offset. Loads each parameter from
 * HAL::Storage at boot (falling back to the zeroed default when absent), and on
 * an edit writes the value into the image (and back to storage). With no PLC to
 * latch it, an edit applies to the image directly; a consumer still re-applies
 * off the serial parameter the user bumps. Selected (templated on the site
 * payload) by BotanikFirmware when BOTANIK_FEATURE_PLC is off.
 */

#include <cstdint>
#include <cstring>

#include "params.h"
#include "../hal/storage.h"

namespace Botanik { namespace Params {

    /** @brief Flash/file-backed configuration backend (see file header). @p Out
     *  is the site's PLC output payload type (the config image layout). */
    template<typename Out>
    class LocalParamHandler : public ParamHandler {

        /** @brief The live config image (loaded from storage, edited in place),
         *  laid out as the site output payload so out_offset addresses it. */
        Out _image;

    public:
        /** @name Lifecycle
         *  @{ */

        explicit LocalParamHandler(Logs::Logger* logger)
            : ParamHandler(logger) { memset(&_image, 0, sizeof(_image)); }

        /** @} */

        /** @name Backend surface
         *  @{ */

        const void* image() const override { return &_image; }

        bool set(const Param& p, const char* text) override {
            // Single image: write (and persist) at out_offset so a read sees it.
            if (!write_value(p, text, &_image, p.field.out_offset)) return false;
            HAL::Storage::set(p.field.name, (const uint8_t*)&_image + p.field.out_offset,
                              param_width(p));
            return true;
        }

        /** @} */

    protected:
        /** @brief Load every registered parameter from storage into the image
         *  (missing keys keep the zero default). Serials are ordinary parameters,
         *  so a site that wants a one-shot boot apply seeds their stored value. */
        void setup(uint64_t /*now_ms*/) override {
            for (uint8_t i = 0; i < count(); i++) {
                const Param& p = param(i);
                HAL::Storage::get(p.field.name, (uint8_t*)&_image + p.field.out_offset,
                                  param_width(p), nullptr);
            }
        }
    };

}}

#endif // LIBBOTANIK_CORE_PARAMS_LOCAL_H
