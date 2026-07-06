// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_CONFIG_PLC_H
#define LIBBOTANIK_CORE_CONFIG_PLC_H

/**
 * @file config_plc.h
 * @brief PlcConfigHandler - the config backend when the PLC owns config in NVM.
 *
 * The image is the PLC's RX config snapshot (plc_config(ctx)), read-only here;
 * an edit is pushed back through the PLC input mirror (a host-bumped input
 * serial the PLC applies to NVM and reflects on the output serial the consumers
 * watch). The write path lands with the input-mirror wire change; until then
 * set() reports failure. Selected by BotanikFirmware when BOTANIK_FEATURE_PLC is
 * on.
 */

#include "libbotanik/core/config.h"
#include "libbotanik/plcio/sync.h"   // PlcSyncContext + plc_config()

namespace Botanik { namespace Cfg {

    /** @brief PLC-backed configuration backend (see file header). */
    class PlcConfigHandler : public ConfigHandler {

        /** @name State
         *  @{ */

        /** @brief PLC link the config snapshot is read from (never null in a
         *  live firmware; guarded anyway). */
        PlcIO::PlcSyncContext* _ctx;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        PlcConfigHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger)
            : ConfigHandler(logger), _ctx(ctx) {}

        /** @} */

        /** @name Backend surface
         *  @{ */

        const PlcIO::PlcOutputConfig_t* image() const override {
            const PlcIO::PlcOutputConfig_t* c = PlcIO::plc_config(_ctx);
            if (c != nullptr) return c;
            static const PlcIO::PlcOutputConfig_t empty = {};   // pre-link fallback
            return &empty;
        }

        bool set(const ConfigParam& /*p*/, const char* /*text*/) override {
            // Write-back lands with the PLC input-config mirror (wire change).
            return false;
        }

        /** @} */
    };

}}

#endif // LIBBOTANIK_CORE_CONFIG_PLC_H
