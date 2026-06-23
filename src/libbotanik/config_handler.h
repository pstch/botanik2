#ifndef LIBBOTANIK_CONFIG_HANDLER_H
#define LIBBOTANIK_CONFIG_HANDLER_H

/**
 * @file config_handler.h
 * @brief ConfigHandler - base for PLC-config-driven step handlers.
 *
 * Each config block carries a `serial` the PLC bumps on any edit
 * (plcio/config.h); this base watches it and resets the derived state machine
 * before stepping it on a change. Consumers: NetworkHandler (`net`), NtpClient
 * (`ntp`), SunPositionTracker (`geo`).
 *
 * Template Method: step() is non-virtual and fixes the order (read serial ->
 * reset on change -> advance). Hooks: config_serial(), on_config_changed()
 * (reset + latch new config), on_step() (advance).
 */

#include <cstdint>

#include "libbotanik/component.h"     // Component (first-step BOOT announce)
#include "libbotanik/logs/logger.h"
#include "libbotanik/plcio/sync.h"   // PlcSyncContext (+ config())

namespace Botanik {

    /** @brief Base for a PLC-config-driven state machine: resets on a
     *  config-serial bump. */
    class ConfigHandler : public Component {
        /** @name State
         *  @{ */

        /** @brief Last config serial acted on (0 = none yet, matches the unset
         *  block, so a never-configured block never triggers a reset). */
        uint32_t _applied_serial;

        /** @} */

    protected:
        /** @name Wiring (for the derived handler)
         *  @{ */

        PlcIO::PlcSyncContext* _ctx;     // config blocks (may be null in tests)
        Logs::Logger*          _logger;  // may be null
        const char*            _name;    // first-step announce label

        /** @} */

    public:
        /** @name Lifecycle
         *  @{ */

        ConfigHandler(PlcIO::PlcSyncContext* ctx, Logs::Logger* logger,
                      const char* name)
            : _applied_serial(0), _ctx(ctx), _logger(logger), _name(name) {}
        virtual ~ConfigHandler() {}

        /** @brief Advance one tick: announce, then on a config-serial change
         *  reset (on_config_changed) before stepping (on_step). Non-virtual so
         *  the read/reset/step order can't be reordered. The serial is in the
         *  main-loop RX snapshot, so read without a barrier. */
        void step(uint64_t now_ms) {
            announce_step(_logger, _name);
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

        /** @brief The generic config block (network/NTP/MQTT/geo), or null if
         *  no ctx. */
        const PlcIO::PlcOutputConfig_t* config() const {
            return _ctx ? _ctx->config() : nullptr;
        }

        /** @} */

        /** @name Hooks (implemented by the handler)
         *  @{ */

        /** @brief Serial of the watched config block
         *  (e.g. config()->net.serial; 0 = none). */
        virtual uint32_t config_serial() const = 0;
        /** @brief Reset and latch the new config (on a watched-serial
         *  change). */
        virtual void on_config_changed(uint64_t now_ms) = 0;
        /** @brief Advance the state machine one tick. */
        virtual void on_step(uint64_t now_ms) = 0;

        /** @} */
    };

}

#endif // LIBBOTANIK_CONFIG_HANDLER_H
