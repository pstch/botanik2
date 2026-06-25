#ifndef NETWORK_TELEMETRY_H
#define NETWORK_TELEMETRY_H

/**
 * @file telemetry.h
 * @brief MqttTelemetry - periodic publication of field values over MQTT.
 *
 * Publishes a set of named values under "<prefix>/<root>/<field>" every
 * Config::MQTT_TELEMETRY_PERIOD_MS while the MqttClient is connected (root =
 * Config::MQTT_TELEMETRY_TOPIC, prefix supplied at construction). Two field
 * sets are published each cycle: a library base set (uptime, CPU load/jitter,
 * stack, PLC link - read from the SystemContext) and the site's own set,
 * appended via set_site_fields() from the BotanikFirmware register_telemetry()
 * hook. A field is just a name plus a formatter taking an opaque ctx, so each
 * reads whatever data it needs - the SystemContext for the base set, the site
 * PLC payload for a site set.
 *
 * Non-blocking: a cycle that the link can't absorb is simply skipped (the next
 * one retries); nothing is queued.
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik_config.h"
#include "libbotanik/context.h"
#include "libbotanik/network/mqtt.h"

namespace Botanik { namespace Network {

    /** @brief One telemetry datapoint: a topic leaf @p name and a @p format
     *  callback that writes its payload into @p buf (cap bytes). @p format
     *  returns false to skip publishing this field this cycle (e.g. no data
     *  yet). @p ctx is the opaque pointer registered with the field's table. */
    struct TelemetryField {
        const char* name;
        bool (*format)(char* buf, size_t cap, void* ctx);
    };

    /** @brief How to read + render a ScalarField's bytes (its width and sign;
     *  ENUM is a u8 rendered through a str() converter). */
    enum ScalarType {
        SCALAR_U8, SCALAR_U16, SCALAR_U32,   ///< unsigned integer
        SCALAR_I16, SCALAR_I32,              ///< signed integer
        SCALAR_ENUM                          ///< u8 -> string via ScalarField::str
    };

    /** @brief A scalar telemetry value at a byte @p offset in a flat struct
     *  (typically the site PLC payload), rendered generically by MqttTelemetry -
     *  no per-field formatter. Integer types publish verbatim; a @p divisor > 1
     *  makes it fixed-point (value / divisor with @p decimals places, e.g. 100
     *  for centi-units); SCALAR_ENUM renders the raw byte through @p str.
     *
     *  This is the data-driven alternative to a TelemetryField table: a site
     *  whose values are plain payload scalars declares one of these (see the
     *  gh_mlb41 g_telemetry table) and registers it with set_site_scalars(). */
    struct ScalarField {
        const char* name;
        uint16_t    offset;            ///< byte offset into the registered base
        ScalarType  type;
        uint16_t    divisor;           ///< 0/1 = integer; 100 = centi; 1000 = milli
        uint8_t     decimals;          ///< fixed-point precision (divisor > 1)
        const char* (*str)(uint8_t);   ///< SCALAR_ENUM only (else null)
    };

    /** @brief Periodic MQTT telemetry publisher (see file header). */
    class MqttTelemetry {
        /** @name State
         *  @{ */

        /** @brief Transport (publishes only while connected()). */
        MqttClient*           _mqtt;
        /** @brief Topic prefix ("<prefix>/<root>/<field>"). */
        const char*           _prefix;
        /** @brief Live system status the base fields read. */
        const SystemContext*  _sys;
        /** @brief Site field table (set_site_fields), tried after the base
         *  set; null until a site registers one. */
        const TelemetryField* _site       = nullptr;
        size_t                _site_count  = 0;
        void*                 _site_ctx    = nullptr;
        /** @brief Site scalar table (set_site_scalars), rendered generically off
         *  @ref _scalar_base; null until a site registers one. */
        const ScalarField*    _scalars      = nullptr;
        size_t                _scalar_count = 0;
        const void*           _scalar_base  = nullptr;
        /** @brief Last publish cycle (uptime ms); 0 = none yet. */
        uint64_t              _last_ms     = 0;

        /** @} */

    public:
        /** @brief @p mqtt is the transport, @p sys the live status read by the
         *  base fields, @p prefix the topic root segment before the field. */
        MqttTelemetry(MqttClient* mqtt, const SystemContext* sys, const char* prefix)
            : _mqtt(mqtt), _prefix(prefix), _sys(sys) {}

        /** @brief Register the site's extra fields, published after the base
         *  set each cycle. @p table must outlive the firmware; @p ctx is passed
         *  to each formatter. Pass count 0 (or null) to clear. */
        void set_site_fields(const TelemetryField* table, size_t count, void* ctx) {
            _site       = table;
            _site_count = (table != nullptr) ? count : 0;
            _site_ctx   = ctx;
        }

        /** @brief Register the site's scalar fields, rendered generically each
         *  cycle by reading @p base + ScalarField::offset. @p table must outlive
         *  the firmware; @p base is the struct the offsets index (the live PLC
         *  payload). Pass count 0 (or null) to clear. */
        void set_site_scalars(const ScalarField* table, size_t count, const void* base) {
            _scalars      = table;
            _scalar_count = (table != nullptr) ? count : 0;
            _scalar_base  = base;
        }

        /** @brief Publish every base + site field once
         *  Config::MQTT_TELEMETRY_PERIOD_MS has elapsed and the link is
         *  connected. No-op when telemetry is disabled (period 0) or the period
         *  has not elapsed. */
        void step(uint64_t now_ms);

    private:
        /** @brief Publish each field of @p table (ctx its formatter context). */
        void _publish_table(const TelemetryField* table, size_t count, void* ctx);
        /** @brief Publish each scalar of the registered site scalar table,
         *  reading + rendering it generically off @ref _scalar_base. */
        void _publish_scalars();
        /** @brief Build the field topic and publish @p payload (best-effort). */
        void _emit(const char* name, const char* payload);
        /** @brief The library base field table (ctx = the SystemContext). */
        static const TelemetryField* _base_fields(size_t& count);
    };

}}

#endif // NETWORK_TELEMETRY_H
