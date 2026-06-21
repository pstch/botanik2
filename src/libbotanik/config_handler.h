#ifndef LIBBOTANIK_CONFIG_HANDLER_H
#define LIBBOTANIK_CONFIG_HANDLER_H

/**
 * @file config_handler.h
 * @brief ConfigHandler - base for PLC-config-driven step handlers.
 *
 * Several components share one shape: each owns a small state machine that must restart
 * whenever the operator changes its slice of the PLC config. The config blocks each
 * carry a `serial` the PLC bumps on any edit (plcio/config.h), so this base watches that
 * serial and, on a change, resets the derived machine before stepping it. That
 * reset-on-serial-change logic then lives here once instead of in every handler.
 *
 * Consumers: Network::NetworkHandler (the `net` block), Network::TimeSyncHandler (`ntp`),
 * and the future sun-position tracker (`geo`).
 *
 * Template Method: step() is non-virtual and fixes the order (read serial -> reset on
 * change -> advance), so a handler can't skip or reorder it. A handler implements three
 * hooks: config_serial() (which block's serial), on_config_changed() (reset + latch the
 * new config), and on_step() (advance the state machine).
 */

#include <cstdint>

#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"   // PlcSyncContext (+ config())

namespace Botanik {

    /** @brief Base for a PLC-config-driven state machine: resets on a config-serial bump. */
    class ConfigHandler {
        /** @name State
         *  @{ */

        /** @brief Last config serial acted on (0 = none yet; matches the unset block,
         *  so a never-configured block never triggers a reset). */
        uint32_t _applied_serial;

        /** @} */

    protected:
        /** @name Wiring (for the derived handler)
         *  @{ */

        /** @brief PLC sync context exposing the config blocks (may be null in tests). */
        PlcIO::PlcSyncContext* _ctx;
        /** @brief Logger for handler events (may be null). */
        Logs::Logger*          _logger;

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        ConfigHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger = nullptr)
            : _applied_serial(0), _ctx(ctx), _logger(logger) {}
        virtual ~ConfigHandler() {}

        /** @brief Advance the handler one tick: if the watched config serial changed,
         *  reset the derived machine (on_config_changed) before stepping it (on_step).
         *  Non-virtual so the read/reset/step order can't be skipped or reordered. The
         *  serial lives in the main-loop RX snapshot, so it is read without a barrier. */
        void step(uint64_t now_ms) {
            uint32_t serial = config_serial();
            if (serial != _applied_serial) {
                _applied_serial = serial;
                on_config_changed(now_ms);
            }
            on_step(now_ms);
        }

        /** @} */

    protected:
        /** @name Helpers
         *  @{ */

        /** @brief The generic config block (network/NTP/MQTT/geo), or null if no ctx. */
        const PlcIO::PlcOutputConfig_t* config() const { return _ctx ? _ctx->config() : nullptr; }

        /** @} */

        /** @name Hooks (implemented by the handler)
         *  @{ */

        /** @brief Return the serial of the config block this handler watches
         *  (e.g. config()->net.serial). 0 means "no config yet". */
        virtual uint32_t config_serial() const = 0;
        /** @brief Reset the state machine and latch the new config. Called when the
         *  watched serial changes (including the first non-zero value). */
        virtual void on_config_changed(uint64_t now_ms) = 0;
        /** @brief Advance the state machine one tick (called every step). */
        virtual void on_step(uint64_t now_ms) = 0;

        /** @} */
    };

}

#endif // LIBBOTANIK_CONFIG_HANDLER_H
