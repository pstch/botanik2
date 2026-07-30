// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_CONFIG_PLC_H
#define LIBBOTANIK_CORE_CONFIG_PLC_H

/**
 * @file config_plc.h
 * @brief PlcConfigHandler - the config backend when the PLC owns config in NVM.
 *
 * The image is the PLC's RX output payload (config parameters are ordinary
 * fields of it, read at each parameter's out_offset). An edit writes the value
 * into the input payload mirror at the parameter's in_offset and requests a TX;
 * the PLC latches a group when the user commits it by bumping that group's
 * serial (itself a normal parameter). Selected by BotanikFirmware when
 * BOTANIK_FEATURE_PLC is on.
 */

#include "libbotanik/core/config.h"
#include "libbotanik/plcio/sync.h"   // PlcSyncContext

namespace Botanik { namespace Cfg {

    /** @brief PLC-backed configuration backend (see file header). */
    class PlcConfigHandler : public ConfigHandler {

        /** @brief PLC link the payload is read from / staged to (never null in a
         *  live firmware; guarded anyway). */
        PlcIO::PlcSyncContext* _ctx;

    public:
        /** @name Lifecycle
         *  @{ */

        PlcConfigHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger)
            : ConfigHandler(logger), _ctx(ctx) {}

        /** @} */

        /** @name Backend surface
         *  @{ */

        const void* image() const override {
            return (_ctx != nullptr) ? _ctx->output_data_bytes() : nullptr;
        }

        /** @brief Stage an edited value into the PLC input payload mirror at the
         *  parameter's in_offset and request a TX. Serials are ordinary
         *  parameters: editing one is how the user commits its group (the PLC
         *  latches config when the serial field advances). @return false on
         *  invalid input, a read-only parameter, or no link. */
        bool set(const ConfigParam& p, const char* text) override {
            void* mirror = (_ctx != nullptr) ? _ctx->input_data_bytes() : nullptr;
            if (mirror == nullptr) return false;
            if (!write_value(p, text, mirror, p.field.in_offset)) return false;
            _ctx->write();
            return true;
        }

        /** @} */
    };

}}

#endif // LIBBOTANIK_CORE_CONFIG_PLC_H
