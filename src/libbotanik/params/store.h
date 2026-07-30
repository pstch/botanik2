#ifndef LIBBOTANIK_PARAMS_STORE_H
#define LIBBOTANIK_PARAMS_STORE_H

/**
 * @file store.h
 * @brief ParamStore - the backend seam ParamHandler delegates to.
 *
 * The parameter model (params.h) is one concrete thing; WHERE the bytes live and
 * how an edit commits is the only part that varies by build. A ParamStore
 * captures exactly that: the read image, the write+commit, and the boot load.
 * Two implementations: PlcParamStore (params/store_plc.h, the PLC owns the
 * values) and LocalParamStore (params/store_local.h, flash / a sim file).
 */

#include "libbotanik/params/params.h"   // Param

namespace Botanik { namespace Params {

    /** @brief Where a ParamHandler's bytes live. A pure backend seam: no
     *  registry, no validation (the model owns those) - just the image, the
     *  write path, and the one-shot boot load. */
    struct ParamStore {
        virtual ~ParamStore() {}

        /** @brief The current-value image (out_offset addresses it). Never null
         *  once the backend is up; a not-yet-ready backend may return null. */
        virtual const void* image() const = 0;

        /** @brief Validate + write @p p's edited @p text, then commit (PLC: stage
         *  + request TX) or persist (local: write-through to storage). @return
         *  false (nothing changed) on invalid input or a read-only parameter. */
        virtual bool set(const Param& p, const char* text) = 0;

        /** @brief Boot-time load of one registered parameter. The local store
         *  seeds it from HAL::Storage; the PLC store (the PLC owns NVM) no-ops. */
        virtual void load(const Param& /*p*/) {}
    };

}}

#endif // LIBBOTANIK_PARAMS_STORE_H
