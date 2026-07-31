#ifndef LIBBOTANIK_PARAMS_STORE_LOCAL_H
#define LIBBOTANIK_PARAMS_STORE_LOCAL_H

/**
 * @file store_local.h
 * @brief LocalParamStore - the ParamStore when there is no PLC.
 *
 * Owns a single fixed-size RAM image (Config::LOCAL_PARAM_IMAGE_BYTES), which
 * parameters address by their byte out_offset. Boot-loads each parameter from
 * HAL::Storage (falling back to the zeroed default when absent); an edit writes
 * the value into the image and back to storage. With no PLC to latch it, an edit
 * applies to the image directly; a consumer still re-applies off the serial
 * parameter the user bumps. Selected by BotanikFirmware when BOTANIK_FEATURE_PLC
 * is off.
 *
 * The image is a flat byte buffer, not a payload type: nothing here names a
 * field, so a fixed buffer sized to hold the parameter block is all it needs (a
 * parameter whose out_offset + width overruns it is rejected, not silently
 * truncated).
 */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "libbotanik_config.h"          // Config::LOCAL_PARAM_IMAGE_BYTES
#include "libbotanik/params/store.h"
#include "libbotanik/hal/storage.h"
#include "libbotanik/logs/logger.h"     // critical() on an overrunning parameter

namespace Botanik { namespace Params {

    /** @brief Flash/file-backed parameter store (see file header). */
    class LocalParamStore : public ParamStore {

        /** @brief The live parameter image (loaded from storage, edited in
         *  place). A flat buffer that out_offset addresses; sized to hold the
         *  site's parameter block. */
        uint8_t       _image[Config::LOCAL_PARAM_IMAGE_BYTES];

        /** @brief For reporting a parameter that overruns the image (may be null). */
        Logs::Logger* _logger;

        /** @brief True if @p p's value fits wholly inside the image. */
        bool _fits(const Param& p) const {
            return p.field.out_offset >= 0 &&
                   (size_t)p.field.out_offset + param_width(p) <= sizeof(_image);
        }

    public:
        explicit LocalParamStore(Logs::Logger* logger) : _logger(logger) {
            memset(_image, 0, sizeof(_image));
        }

        const void* image() const override { return _image; }

        SetResult set(const Param& p, const char* text) override {
            if (!_fits(p)) return SET_INVALID;
            if (!write_value(p, text, _image, p.field.out_offset)) return SET_INVALID;
            // The value is already live in the image. If storage refuses it, say
            // so rather than reporting success on an edit that dies at reboot.
            if (!HAL::Storage::set(p.field.name, _image + p.field.out_offset,
                                   param_width(p))) {
                if (_logger)
                    _logger->critical(Logs::BOOT, "param '%s' not persisted",
                                      p.field.name);
                return SET_NOT_PERSISTED;
            }
            return SET_OK;
        }

        /** @brief Seed one parameter from storage (missing keys keep the zero
         *  default). A parameter that would overrun the image is skipped and
         *  reported once: the site must raise LOCAL_PARAM_IMAGE_BYTES to cover it. */
        void load(const Param& p) override {
            if (!_fits(p)) {
                if (_logger)
                    _logger->critical(Logs::BOOT,
                        "param '%s' overruns LOCAL_PARAM_IMAGE_BYTES (%u)",
                        p.field.name, (unsigned)sizeof(_image));
                return;
            }
            // Missing key -> the parameter's declared default, NOT zero. With
            // the PLC gone this store is the only source of parameters, so a
            // first boot on empty storage would otherwise bring the firmware up
            // with a zeroed network/NTP/MQTT config and no way to tell that
            // apart from a deliberate 0.
            if (!HAL::Storage::get(p.field.name, _image + p.field.out_offset,
                                   param_width(p), nullptr)) {
                if (p.meta.def != nullptr && p.meta.def[0] != '\0')
                    write_value(p, p.meta.def, _image, p.field.out_offset);
            }
        }
    };

}}

#endif // LIBBOTANIK_PARAMS_STORE_LOCAL_H
