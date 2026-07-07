// REVIEW_35: TODO
#ifndef LIBBOTANIK_CORE_CONFIG_PLC_H
#define LIBBOTANIK_CORE_CONFIG_PLC_H

/**
 * @file config_plc.h
 * @brief PlcConfigHandler - the config backend when the PLC owns config in NVM.
 *
 * The image is the PLC's RX config snapshot (plc_config(ctx)), read-only here;
 * an edit is pushed back through the PLC input mirror: set() stages the edited
 * parameter's whole group in the input config, bumps a host-owned group write
 * serial, and requests a TX. The PLC applies the group to NVM and reflects it
 * on the output serial the consumers watch. Selected by BotanikFirmware when
 * BOTANIK_FEATURE_PLC is on.
 */

#include <cstring>

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
        /** @brief Monotonic host-side write serial per group. Kept independent
         *  of the PLC output serial so a write is always detected as an advance
         *  by the PLC, regardless of the output serial's value. */
        uint32_t _write_serial[CFG_GROUP_COUNT];

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        PlcConfigHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger)
            : ConfigHandler(logger), _ctx(ctx) {
            for (uint8_t g = 0; g < CFG_GROUP_COUNT; g++) _write_serial[g] = 0;
        }

        /** @} */

        /** @name Backend surface
         *  @{ */

        const PlcIO::PlcOutputConfig_t* image() const override {
            const PlcIO::PlcOutputConfig_t* c = PlcIO::plc_config(_ctx);
            if (c != nullptr) return c;
            static const PlcIO::PlcOutputConfig_t empty = {};   // pre-link fallback
            return &empty;
        }

        /** @brief Push an edit to the PLC: stage the parameter's whole group
         *  (current values with the one edit applied) in the input mirror, bump
         *  the group's host write serial, and request a TX. The PLC applies the
         *  group to NVM and reflects it on the output serial the consumers
         *  watch. @return false (nothing staged) on invalid input or no link. */
        bool set(const ConfigParam& p, const char* text) override {
            PlcIO::PlcInputConfig_t* mirror = (_ctx != nullptr) ? _ctx->input_config() : nullptr;
            if (mirror == nullptr) return false;

            // Stage the edited group's current fields, then apply the one edit,
            // so the PLC applies a coherent group (only the edited field changed).
            uint16_t start, len;
            group_byte_range(p.group, &start, &len);
            memcpy((uint8_t*)mirror + start, (const uint8_t*)image() + start, len);
            if (!write_value(p, text, mirror)) return false;

            // Monotonic host write serial (independent of the output serial).
            _write_serial[p.group] += 1;
            uint16_t soff = group_serial_offset(p.group);
            memcpy((uint8_t*)mirror + soff, &_write_serial[p.group], 4);

            _ctx->write();
            return true;
        }

        /** @} */
    };

}}

#endif // LIBBOTANIK_CORE_CONFIG_PLC_H
