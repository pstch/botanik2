#ifndef ALERTS_ALERTS_H
#define ALERTS_ALERTS_H

/**
 * @file alerts.h
 * @brief Alert model - merges the remote (PLC) and local alert bitsets, tracks
 *        each condition's dwell, raises ALERT-topic log lines and MQTT
 *        notifications on the configured ON/OFF thresholds, and exposes the set
 *        for display (the terminal's bottom alert bar and the AlertsPage table).
 *
 * Each bit of a 64-bit raw field is one alert condition. The PLC owns the
 * debouncing/low-pass filtering and reports the bitset; some bits are instead
 * set by our own runtime (set_local()). The two are merged here before analysis.
 *
 * The 64-entry AlertCondition table is site-supplied RAM (never the read-only PLC
 * shared region): the site fills the static metadata per entry and leaves the
 * dynamic state zeroed; the model owns the dynamic state from then on. Array index
 * must equal the AlertId bit index so (raw >> i) maps to entry i.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "libbotanik/logs/logger.h"

namespace Botanik { namespace Alerts {

    /** @brief Width of the raw alert bitset (and of the condition table). */
    static constexpr uint8_t ALERT_BITS = 64;

    /** @brief Alert bit indices. Values are bit positions in the 64-bit raw field;
     *  the site condition table is indexed by the same value. */
    enum AlertId : uint8_t {
        AC_GRID = 0,  // Grid AC out of spec or offline
        DC_PS1,       // DC PS1 volts too low
        GEN_FLT,      // Generator failed to start
        AC_LOCAL,     // Local AC out of spec or offline
        DC_PS2,       // DC PS2 volts too low
        DC_UPS,       // DC UPS on battery
        DC_LOCAL,     // Local DC volts too low
        GEN_BATT,     // Generator start battery too low or offline
        PLC_FLT,      // Failed to synchronize with PLC
        NET_FLT,      // Network should be set up but no TCP/IP I/O
        NTP_TIM,      // Timeout while attempting NTP sync
        ACT_FLT,      // At least one actioner faulted in the last hour
        SNS_FLT,      // At least one sensor faulted in the last hour
        TOO_HOT,      // Inside temperature too high
        TOO_COLD,     // Inside temperature too low
        TOO_HUM,      // RH too high
        TMP_STR,      // Temperature stratification too high
        SYS_RST,      // Reinitialized in the last five minutes
        WTR_LK,       // Leak in irrigation circuit
        WTR_BAR,      // Water pressure too low
        WTR_CLOG,     // Failing to drain a tray
        IO_DROP,      // Logical I/O drops
        LOG_FLD,      // Logs flooding
        WH_STORM,     // Current storm conditions
        ALERT_DEFINED // count of defined alerts (not an alert)
    };

    /** @brief 1 << id as a 64-bit mask, for building masked_by sets. */
    inline uint64_t alert_bit(uint8_t id) { return (uint64_t)1u << id; }

    /**
     * @brief One alert condition: static metadata + model-owned dynamic state.
     *
     * The static prefix is site-defined (aggregate-initialised, in AlertId order).
     * The dynamic suffix is left zeroed by the site and owned by AlertModel; a
     * threshold of -1 disables that action.
     */
    struct AlertCondition {
        /** @name Static metadata (site-defined)
         *  @{ */
        const char* code;        // short code, e.g. "AC_GRID" (null = undefined slot)
        const char* desc;        // human-readable description
        uint64_t    masked_by;   // set of alerts that suppress this one while active
        int32_t     log_on_ms;   // min ON dwell before a WARNING "alarm START" log (-1 off)
        int32_t     log_off_ms;  // min OFF dwell before an INFO "alarm END" log (-1 off)
        int32_t     mqtt_on_ms;  // min ON dwell before an MQTT notification (-1 off)
        int32_t     mqtt_off_ms; // min OFF dwell before an MQTT notification (-1 off)
        /** @} */

        /** @name Dynamic state (model-owned; leave zeroed in a site table)
         *  @{ */
        bool        active;       // merged raw bit is set
        bool        unmasked;     // active and no alert in masked_by is active
        bool        on_notified;  // MQTT ON notification sent (cleared when OFF)
        bool        off_notified; // MQTT OFF notification sent (cleared when ON)
        bool        log_on_sent;  // "alarm START" logged this episode
        bool        log_off_sent; // "alarm END" logged this episode
        uint64_t    edge_ms;      // uptime of the last active/inactive transition
        uint64_t    started_ms;   // uptime of the last ON transition (onset); 0 if
                                  // never active. Drives the AlertsPage start-time sort
        /** @} */
    };

    /**
     * @brief Draft MQTT notification sink. The model calls notify_on/off when an
     * alert crosses its MQTT ON/OFF dwell threshold. Concrete plumbing lives in
     * notifier.h; a null notifier disables MQTT notifications.
     */
    class AlertNotifier {
    public:
        virtual ~AlertNotifier() {}
        virtual void notify_on(const AlertCondition& a)  = 0;
        virtual void notify_off(const AlertCondition& a) = 0;
    };

    /**
     * @brief Owns the dynamic alert state over a site-supplied condition table.
     *
     * step() is called once per loop with the remote (PLC) raw bitset and the
     * current uptime; it merges in the local bits, advances each condition's
     * dwell, raises log lines / notifications, and recomputes the masked view.
     */
    class AlertModel {
    private:
        /** @name State
         *  @{ */
        AlertCondition* _conditions;     // site table, ALERT_BITS entries, index == bit
        uint8_t         _count;          // entries to scan (<= ALERT_BITS)
        Logs::Logger*   _logger;         // ALERT-topic log sink (may be null)
        AlertNotifier*  _notifier;       // MQTT sink (may be null)
        uint64_t        _local;          // runtime-sourced alert bits
        uint64_t        _raw;            // last merged raw (remote | local)
        uint8_t         _active_count;   // active conditions this step
        uint8_t         _unmasked_count; // active and unmasked conditions this step
        /** @} */

    public:
        /** @brief Bind to a site condition table. @p count entries are scanned
         *  (clamped to ALERT_BITS). A never-active alert starts "settled OFF" so it
         *  never announces an END it never started. */
        AlertModel(AlertCondition* conditions, uint8_t count,
                   Logs::Logger* logger, AlertNotifier* notifier = nullptr)
            : _conditions(conditions),
              _count(count > ALERT_BITS ? ALERT_BITS : count),
              _logger(logger), _notifier(notifier),
              _local(0), _raw(0), _active_count(0), _unmasked_count(0) {
            for (uint8_t i = 0; i < _count; i++) {
                _conditions[i].off_notified = true;   // boot state: already cleared
                _conditions[i].log_off_sent = true;
            }
        }

        /** @name Local alerts
         *  @{ */

        /** @brief Set or clear a runtime-sourced alert bit (merged with the PLC's
         *  on the next step()). */
        void set_local(uint8_t id, bool on) {
            if (id >= ALERT_BITS) return;
            if (on) _local |=  alert_bit(id);
            else    _local &= ~alert_bit(id);
        }

        /** @} */

        /** @brief Merge the PLC raw bitset with the local one, advance each
         *  condition, and raise any due log lines / notifications. */
        void step(uint64_t remote_raw, uint64_t now) {
            _raw = remote_raw | _local;
            _active_count = 0;
            _unmasked_count = 0;
            for (uint8_t i = 0; i < _count; i++) {
                AlertCondition& c = _conditions[i];
                bool was = c.active;
                bool on  = ((_raw >> i) & 1u) != 0;
                if (on != was) {
                    c.edge_ms = now;
                    if (on) { c.off_notified = false; c.log_off_sent = false;
                              c.started_ms = now; }   // onset, for start-time ordering
                    else    { c.on_notified  = false; c.log_on_sent  = false; }
                }
                c.active   = on;
                c.unmasked = on && ((_raw & c.masked_by) == 0);
                if (on) {
                    _active_count++;
                    if (c.unmasked) _unmasked_count++;
                }

                uint64_t dwell = now - c.edge_ms;
                if (on) {
                    if (!c.log_on_sent && c.log_on_ms >= 0 &&
                        dwell >= (uint64_t)c.log_on_ms) {
                        // Keep a short description on the same line; wrap a long one to
                        // the next line so it does not crowd the timestamp/topic prefix.
                        const char* d = c.desc ? c.desc : "";
                        if (_logger) _logger->warning(PlcIO::ALERT,
                            strlen(d) < 32 ? "alarm START : %s" : "alarm START :\n%s", d);
                        c.log_on_sent = true;
                    }
                    if (!c.on_notified && c.mqtt_on_ms >= 0 &&
                        dwell >= (uint64_t)c.mqtt_on_ms) {
                        if (_notifier) _notifier->notify_on(c);
                        c.on_notified = true;
                    }
                } else {
                    if (!c.log_off_sent && c.log_off_ms >= 0 &&
                        dwell >= (uint64_t)c.log_off_ms) {
                        const char* d = c.desc ? c.desc : "";
                        if (_logger) _logger->info(PlcIO::ALERT,
                            strlen(d) < 32 ? "alarm END : %s" : "alarm END :\n%s", d);
                        c.log_off_sent = true;
                    }
                    if (!c.off_notified && c.mqtt_off_ms >= 0 &&
                        dwell >= (uint64_t)c.mqtt_off_ms) {
                        if (_notifier) _notifier->notify_off(c);
                        c.off_notified = true;
                    }
                }
            }
        }

        /** @name Queries
         *  @{ */

        /** @brief Merged raw bitset from the last step(). */
        uint64_t raw() const { return _raw; }
        /** @brief Number of table entries scanned. */
        uint8_t  count() const { return _count; }
        /** @brief Active conditions (masked or not). */
        uint8_t  active_count() const { return _active_count; }
        /** @brief Active and unmasked conditions (the compact alert-bar set). */
        uint8_t  unmasked_count() const { return _unmasked_count; }
        /** @brief Condition @p i (0 <= i < count()). */
        const AlertCondition& condition(uint8_t i) const { return _conditions[i]; }

        /** @} */
    };

}}

#endif // ALERTS_ALERTS_H
