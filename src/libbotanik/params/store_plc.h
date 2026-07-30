#ifndef LIBBOTANIK_PARAMS_STORE_PLC_H
#define LIBBOTANIK_PARAMS_STORE_PLC_H

/**
 * @file store_plc.h
 * @brief PlcParamStore - the ParamStore when the PLC owns parameters in NVM.
 *
 * The image is the PLC's RX output payload (parameters are ordinary fields of
 * it, read at each parameter's out_offset). An edit writes the value into the
 * input payload mirror at the parameter's in_offset and requests a TX; the PLC
 * latches a group when the user commits it by bumping that group's serial
 * (itself a normal parameter). Boot load is a no-op - the PLC is the source of
 * truth. Selected by BotanikFirmware when BOTANIK_FEATURE_PLC is on.
 */

#include "libbotanik/params/store.h"
#include "libbotanik/plcio/sync.h"   // PlcSyncContext

namespace Botanik { namespace Params {

    /** @brief PLC-backed parameter store (see file header). */
    class PlcParamStore : public ParamStore {

        /** @brief PLC link the payload is read from / staged to (never null in a
         *  live firmware; guarded anyway). */
        PlcIO::PlcSyncContext* _ctx;

    public:
        explicit PlcParamStore(PlcIO::PlcSyncContext* ctx) : _ctx(ctx) {}

        const void* image() const override {
            return (_ctx != nullptr) ? _ctx->output_data_bytes() : nullptr;
        }

        /** @brief Stage an edited value into the PLC input payload mirror at the
         *  parameter's in_offset and request a TX. Serials are ordinary
         *  parameters: editing one is how the user commits its group (the PLC
         *  latches config when the serial field advances). @return false on
         *  invalid input, a read-only parameter, or no link. */
        bool set(const Param& p, const char* text) override {
            void* mirror = (_ctx != nullptr) ? _ctx->input_data_bytes() : nullptr;
            if (mirror == nullptr) return false;
            if (!write_value(p, text, mirror, p.field.in_offset)) return false;
            _ctx->write();
            return true;
        }
        // load(): default no-op - the PLC owns the values.
    };

}}

#endif // LIBBOTANIK_PARAMS_STORE_PLC_H
