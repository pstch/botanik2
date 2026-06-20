#ifndef ALARMS_ALARMS_H
#define ALARMS_ALARMS_H

/**
 * @file alarms.h
 * @brief Alarm model - merges the remote (PLC) and local alarm bitsets, tracks
 *        each condition's dwell, raises ALARM-topic log lines and MQTT
 *        notifications on the configured ON/OFF thresholds, and exposes the set
 *        for display (the terminal's bottom alarm bar and the AlarmsPage table).
 *
 * The condition list is split into two disjoint namespaces: the PLC/site alarms
 * fill indices [0..N) and map one-to-one to the low N bits of the PLC's 64-bit
 * `alarms` word; the generic platform-health alarms (platform.h) follow at
 * [N..N+M) and are driven only locally (set_local()). step() masks the remote word
 * to the low N bits before merging the local bits, so a stray PLC bit can never
 * drive a platform condition (no overlap by construction). N + M must be <= 64.
 *
 * The model owns its working AlarmCondition array (RAM): it copies the site's
 * const PLC table and the const platform table in at construction, then owns the
 * dynamic state from then on. The site table stays read-only.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

#include "libbotanik_asserts.h"        // BOTANIK_ASSERT
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
        bool        acknowledged; // operator ack'd this episode (cleared on the next ON
                                  // edge); informational - does not change active/unmasked
        uint64_t    edge_ms;      // uptime of the last active/inactive transition
        uint64_t    started_ms;   // uptime of the last ON transition (onset); 0 if
                                  // never active. Drives the AlarmsPage start-time sort
        uint32_t    latch_remaining_ms; // latch countdown: ms still held active after the
                                  // raw bit cleared (0 = not latched). Wrap-immune (no
                                  // absolute deadline); decremented by the step elapsed.
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
        AlarmCondition  _conditions[ALARM_BITS]; // model-owned working array
        uint8_t         _plc_count;      // N: PLC/site conditions at [0..N)
        uint8_t         _count;          // N + M: total conditions scanned
        uint64_t        _site_mask;      // (1<<N)-1: masks the remote word to the PLC range
        Logs::Logger*   _logger;         // ALARM-topic log sink (may be null)
        AlarmNotifier*  _notifier;       // MQTT sink (may be null)
        uint64_t        _local;          // runtime-sourced alarm bits (platform range)
        uint64_t        _raw;            // last merged raw ((remote & mask) | local)
        uint64_t        _last_now;       // uptime of the last step (latch countdown)
        bool            _first_step;     // suppress the latch decrement on the first step
        uint8_t         _active_count;   // active conditions this step
        uint8_t         _unmasked_count; // active and unmasked conditions this step
        /** @} */

    public:
        /** @brief Bind to the site's PLC alarm table (@p site_count entries at [0..N))
         *  and, when given, the generic platform-health table (platform.h, @p platform_count
         *  entries appended at [N..N+M)). Both are const inputs, copied into the model's
         *  own array; N + M must be <= ALARM_BITS. A never-active alarm starts "settled
         *  OFF" so it never announces an END it never started. */
        AlarmModel(const AlarmCondition* site_table, uint8_t site_count,
                   Logs::Logger* logger, AlarmNotifier* notifier = nullptr,
                   const AlarmCondition* platform = nullptr, uint8_t platform_count = 0)
            : _plc_count(0), _count(0), _site_mask(0),
              _logger(logger), _notifier(notifier),
              _local(0), _raw(0), _last_now(0), _first_step(true),
              _active_count(0), _unmasked_count(0) {
            BOTANIK_ASSERT(site_count + platform_count <= ALARM_BITS);
            // Clamp defensively so a too-large count can't overflow the working array.
            if (site_count > ALARM_BITS) site_count = ALARM_BITS;
            if (platform_count > (uint8_t)(ALARM_BITS - site_count))
                platform_count = (uint8_t)(ALARM_BITS - site_count);
            std::memset(_conditions, 0, sizeof(_conditions));
            // PLC/site alarms fill [0..N); platform-health alarms follow at [N..N+M).
            // The ranges are disjoint, so a PLC bit can never drive a platform condition.
            for (uint8_t i = 0; i < site_count; i++)     _conditions[i] = site_table[i];
            for (uint8_t j = 0; j < platform_count; j++) _conditions[site_count + j] = platform[j];
            _plc_count = site_count;
            _count     = (uint8_t)(site_count + platform_count);
            _site_mask = (site_count >= 64) ? ~(uint64_t)0
                                            : (((uint64_t)1 << site_count) - 1);
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

        /** @brief Mark an active condition acknowledged by table code; returns false if
         *  the code is unknown or the alarm isn't active. Informational only (cleared on
         *  the next ON edge); never alters the active/unmasked state or the safety bar. */
        bool ack(const char* code) {
            int i = alarm_index(_conditions, _count, code);
            if (i < 0 || !_conditions[i].active) return false;
            _conditions[i].acknowledged = true;
            return true;
        }

        /** @brief Acknowledge every currently-active condition; returns the count ack'd. */
        uint8_t ack_all() {
            uint8_t n = 0;
            for (uint8_t i = 0; i < _count; i++)
                if (_conditions[i].active) { _conditions[i].acknowledged = true; n++; }
            return n;
        }

        /** @} */

        /** @brief Merge the PLC raw bitset with the local one, advance each
         *  condition, and raise any due log lines / notifications. */
        void step(uint64_t remote_raw, uint64_t now) {
            // Mask the remote word to the PLC range so a stray high bit can't drive a
            // platform-health condition; merge the locally-set bits (platform range) on top.
            _raw = (remote_raw & _site_mask) | _local;
            // Elapsed since the last step, for the latch countdown (0 on the first step).
            uint32_t dt = _first_step ? 0 : (uint32_t)(now - _last_now);
            _first_step = false;
            _last_now   = now;
            _active_count = 0;
            _unmasked_count = 0;
            for (uint8_t i = 0; i < _count; i++) {
                AlarmCondition& c = _conditions[i];
                bool was = c.active;
                bool raw_on = ((_raw >> i) & 1u) != 0;
                // Latch as a countdown: drain it by the elapsed time, refresh to latch_s
                // while the raw bit is set. No absolute deadline, so it can't wrap.
                if (c.latch_remaining_ms > 0)
                    c.latch_remaining_ms = (c.latch_remaining_ms > dt)
                                         ? (uint32_t)(c.latch_remaining_ms - dt) : 0;
                if (raw_on && c.latch_s > 0)
                    c.latch_remaining_ms = (uint32_t)c.latch_s * 1000u;
                bool on = raw_on || (c.latch_remaining_ms > 0);
                if (on != was) {
                    c.edge_ms = now;
                    if (on) { c.off_notified = false; c.log_off_sent = false;
                              c.acknowledged = false;  // a fresh episode is unacknowledged
                              c.started_ms = now; }   // onset, for start-time ordering
                    else    { c.on_notified  = false; c.log_on_sent  = false; }
                }
                c.active   = on;
                c.unmasked = on && ((_raw & c.masked_by) == 0);
                if (on) {
                    _active_count++;
                    if (c.unmasked) _unmasked_count++;
                }

                // Log/notify messages carry the description, falling back to the code.
                uint64_t dwell = now - c.edge_ms;
                const char* label = c.desc ? c.desc : (c.code ? c.code : "");
                if (on) {
                    if (!c.log_on_sent && c.log_on_s >= 0 &&
                        dwell >= (uint64_t)c.log_on_s * 1000) {
                        if (_logger) _logger->warning(PlcIO::ALARM_START, "%s", label);
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
                        if (_logger) _logger->info(PlcIO::ALARM_END, "%s", label);
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
        /** @brief Index of the condition whose code equals @p code, or -1 (POST check:
         *  verifies a set_local(code) call will actually resolve rather than no-op). */
        int index_of(const char* code) const { return alarm_index(_conditions, _count, code); }

        /** @} */
    };

}}

#endif // ALARMS_ALARMS_H
