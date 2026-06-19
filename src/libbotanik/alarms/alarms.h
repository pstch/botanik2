#ifndef ALARMS_ALARMS_H
#define ALARMS_ALARMS_H

/**
 * @file alarms.h
 * @brief Alarm model - merges the remote (PLC) and local alarm bitsets, tracks
 *        each condition's dwell, raises ALARM-topic log lines and MQTT
 *        notifications on the configured ON/OFF thresholds, and exposes the set
 *        for display (the terminal's bottom alarm bar and the AlarmsPage table).
 *
 * Each bit of a 64-bit raw field is one alarm condition. The PLC owns the
 * debouncing/low-pass filtering and reports the bitset; some bits are instead
 * set by our own runtime (set_local()). The two are merged here before analysis.
 *
 * The 64-entry AlarmCondition table is site-supplied RAM (never the read-only PLC
 * shared region): the site fills the static metadata per entry and leaves the
 * dynamic state zeroed; the model owns the dynamic state from then on. Array index
 * must equal the bit index so (raw >> i) maps to entry i.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "libbotanik/logs/logger.h"

namespace Botanik { namespace Alarms {

    /** @brief Width of the raw alarm bitset (and of the condition table). */
    static constexpr uint8_t ALARM_BITS = 64;

    /** @brief 1 << id as a 64-bit mask, for building masked_by sets. */
    inline uint64_t alarm_bit(uint8_t id) { return (uint64_t)1u << id; }

    /** @brief Default "recent" window in seconds: a condition whose last event is
     *  newer than this shows in the AlarmsPage RECENT mode. Per-condition override
     *  via AlarmCondition::recent_s (0 = use this default, < 0 = never expires). */
    static constexpr int32_t DEFAULT_RECENT_S = 7 * 24 * 3600;   // one week

    /**
     * @brief One alarm condition: static metadata + model-owned dynamic state.
     *
     * The static prefix is site-defined (aggregate-initialised; row index is the bit index).
     * The dynamic suffix is left zeroed by the site and owned by AlarmModel; a
     * threshold of -1 disables that action. Dwell thresholds are in seconds.
     */
    struct AlarmCondition {
        /** @name Static metadata (site-defined)
         *  @{ */
        const char* code;        // short code, e.g. "CPU_LOAD" (null = undefined slot)
        const char* desc;        // human-readable description
        uint64_t    masked_by;   // set of alarms that suppress this one while active
        int32_t     log_on_s;    // min ON dwell (s) before a WARNING "alarm START" log (-1 off)
        int32_t     log_off_s;   // min OFF dwell (s) before an INFO "alarm END" log (-1 off)
        int32_t     mqtt_on_s;   // min ON dwell (s) before an MQTT notification (-1 off)
        int32_t     mqtt_off_s;  // min OFF dwell (s) before an MQTT notification (-1 off)
        int32_t     recent_s;    // RECENT-mode window (s): 0 = DEFAULT_RECENT_S, <0 = never expires
        int32_t     latch_s;     // hold active this long (s) after the raw bit clears (0 = no latch)
        /** @} */

        /** @name Dynamic state (model-owned; leave zeroed in a site table)
         *  @{ */
        bool        active;       // effective active (raw bit set, or still latched)
        bool        unmasked;     // active and no alarm in masked_by is active
        bool        on_notified;  // MQTT ON notification sent (cleared when OFF)
        bool        off_notified; // MQTT OFF notification sent (cleared when ON)
        bool        log_on_sent;  // "alarm START" logged this episode
        bool        log_off_sent; // "alarm END" logged this episode
        uint64_t    edge_ms;      // uptime of the last active/inactive transition
        uint64_t    started_ms;   // uptime of the last ON transition (onset); 0 if
                                  // never active. Drives the AlarmsPage start-time sort
        uint64_t    latch_until_ms; // uptime the latch holds active until (0 = not latched)
        /** @} */
    };

    /** @brief Bit index of the condition whose code equals @p code, or -1 if none.
     *  The table is the single source of truth for the alarm set (there is no parallel
     *  id enum); code lookups are how generic firmware and the site mock-PLC name the
     *  few alarms they assert directly. */
    inline int alarm_index(const AlarmCondition* table, uint8_t count, const char* code) {
        if (table == nullptr || code == nullptr) return -1;
        for (uint8_t i = 0; i < count; i++)
            if (table[i].code != nullptr && std::strcmp(table[i].code, code) == 0)
                return (int)i;
        return -1;
    }

    /**
     * @brief Draft MQTT notification sink. The model calls notify_on/off when an
     * alarm crosses its MQTT ON/OFF dwell threshold. Concrete plumbing lives in
     * notifier.h; a null notifier disables MQTT notifications.
     */
    class AlarmNotifier {
    public:
        virtual ~AlarmNotifier() {}
        virtual void notify_on(const AlarmCondition& a)  = 0;
        virtual void notify_off(const AlarmCondition& a) = 0;
    };

    /**
     * @brief Owns the dynamic alarm state over a site-supplied condition table.
     *
     * step() is called once per loop with the remote (PLC) raw bitset and the
     * current uptime; it merges in the local bits, advances each condition's
     * dwell, raises log lines / notifications, and recomputes the masked view.
     */
    class AlarmModel {
    private:
        /** @name State
         *  @{ */
        AlarmCondition* _conditions;     // site table, ALARM_BITS entries, index == bit
        uint8_t         _count;          // entries to scan (<= ALARM_BITS)
        Logs::Logger*   _logger;         // ALARM-topic log sink (may be null)
        AlarmNotifier*  _notifier;       // MQTT sink (may be null)
        uint64_t        _local;          // runtime-sourced alarm bits
        uint64_t        _raw;            // last merged raw (remote | local)
        uint8_t         _active_count;   // active conditions this step
        uint8_t         _unmasked_count; // active and unmasked conditions this step
        /** @} */

    public:
        /** @brief Bind to a site condition table (@p count entries scanned, clamped to
         *  ALARM_BITS). @p platform, when given, is the generic platform-health table
         *  (platform.h); its @p platform_count entries are spliced into the *top* of the
         *  bitset, away from the site/PLC alarms that fill from bit 0. A never-active
         *  alarm starts "settled OFF" so it never announces an END it never started. */
        AlarmModel(AlarmCondition* conditions, uint8_t count,
                   Logs::Logger* logger, AlarmNotifier* notifier = nullptr,
                   const AlarmCondition* platform = nullptr, uint8_t platform_count = 0)
            : _conditions(conditions),
              _count(count > ALARM_BITS ? ALARM_BITS : count),
              _logger(logger), _notifier(notifier),
              _local(0), _raw(0), _active_count(0), _unmasked_count(0) {
            // Place the platform-health alarms at the high end of the table, so their
            // locally-set bits never collide with the PLC-reported site bits below.
            if (platform != nullptr && platform_count > 0) {
                if (platform_count > ALARM_BITS) platform_count = ALARM_BITS;
                for (uint8_t j = 0; j < platform_count; j++)
                    _conditions[ALARM_BITS - platform_count + j] = platform[j];
                if (_count < ALARM_BITS) _count = ALARM_BITS;   // scan the high slots too
            }
            for (uint8_t i = 0; i < _count; i++) {
                _conditions[i].off_notified = true;   // boot state: already cleared
                _conditions[i].log_off_sent = true;
            }
        }

        /** @name Local alarms
         *  @{ */

        /** @brief Set or clear a runtime-sourced alarm bit (merged with the PLC's
         *  on the next step()). */
        void set_local(uint8_t id, bool on) {
            if (id >= ALARM_BITS) return;
            if (on) _local |=  alarm_bit(id);
            else    _local &= ~alarm_bit(id);
        }

        /** @brief Set/clear a local alarm by its table code (e.g. "CPU_LOAD"). A no-op
         *  if the site table has no such code, so generic firmware can assert platform
         *  alarms without depending on a site defining them. */
        void set_local(const char* code, bool on) {
            int i = alarm_index(_conditions, _count, code);
            if (i >= 0) set_local((uint8_t)i, on);
        }

        /** @} */

        /** @brief Merge the PLC raw bitset with the local one, advance each
         *  condition, and raise any due log lines / notifications. */
        void step(uint64_t remote_raw, uint64_t now) {
            _raw = remote_raw | _local;
            _active_count = 0;
            _unmasked_count = 0;
            for (uint8_t i = 0; i < _count; i++) {
                AlarmCondition& c = _conditions[i];
                bool was = c.active;
                // Latch: a condition with latch_s holds "active" for latch_s after its
                // raw bit clears. Refresh the deadline every beat the raw bit is set, so
                // the effective state stays on until latch_s past the last raw-on beat.
                bool raw_on = ((_raw >> i) & 1u) != 0;
                if (raw_on && c.latch_s > 0)
                    c.latch_until_ms = now + (uint64_t)c.latch_s * 1000u;
                bool on = raw_on || (c.latch_s > 0 && now < c.latch_until_ms);
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
                    if (!c.log_on_sent && c.log_on_s >= 0 &&
                        dwell >= (uint64_t)c.log_on_s * 1000) {
                        // The topic (ALARM_START) carries the event; the message is just
                        // the alarm description.
                        if (_logger)
                            _logger->warning(PlcIO::ALARM_START, "%s", c.desc ? c.desc : "");
                        c.log_on_sent = true;
                    }
                    if (!c.on_notified && c.mqtt_on_s >= 0 &&
                        dwell >= (uint64_t)c.mqtt_on_s * 1000) {
                        if (_notifier) _notifier->notify_on(c);
                        c.on_notified = true;
                    }
                } else {
                    if (!c.log_off_sent && c.log_off_s >= 0 &&
                        dwell >= (uint64_t)c.log_off_s * 1000) {
                        // Topic (ALARM_END) carries the event; message is the description.
                        if (_logger)
                            _logger->info(PlcIO::ALARM_END, "%s", c.desc ? c.desc : "");
                        c.log_off_sent = true;
                    }
                    if (!c.off_notified && c.mqtt_off_s >= 0 &&
                        dwell >= (uint64_t)c.mqtt_off_s * 1000) {
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
        /** @brief Active and unmasked conditions (the compact alarm-bar set). */
        uint8_t  unmasked_count() const { return _unmasked_count; }
        /** @brief Condition @p i (0 <= i < count()). */
        const AlarmCondition& condition(uint8_t i) const { return _conditions[i]; }

        /** @} */
    };

}}

#endif // ALARMS_ALARMS_H
