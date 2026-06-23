#ifndef ALARMS_ALARMS_H
#define ALARMS_ALARMS_H

/**
 * @file alarms.h
 * @brief Alarm model - merges remote (PLC) and local alarm bitsets.
 *
 * Tracks each condition's dwell, raises ALARM-topic log lines / MQTT
 * notifications on the ON/OFF thresholds, and exposes the set for display.
 *
 * The condition list is two disjoint namespaces: PLC/site alarms fill [0..N),
 * mapping to the low N bits of the PLC's 64-bit `alarms` word; the generic
 * platform-health alarms (platform.h) follow at [N..N+M), driven only locally
 * (set_local()). step() masks the remote word to the low N bits before merging
 * local bits, so a stray PLC bit can never drive a platform condition. N + M
 * must be <= 64.
 *
 * The model owns its working AlarmCondition array: it copies both const tables
 * in at construction, then owns the dynamic state. The site table stays
 * read-only.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>                      // snprintf (AlarmNotifier::_emit)

#include "libbotanik/utils/assert.h"   // BOTANIK_ASSERT
#include "libbotanik/logs/logger.h"
#include "libbotanik/component.h"      // Component (first-step announce)
#include "libbotanik/alarms/publisher.h" // Publisher (notifier transport seam)

namespace Botanik { namespace Alarms {

    /** @brief Width of the raw alarm bitset (and of the condition table). */
    static constexpr uint8_t ALARM_BITS = 64;

    /** @brief 1 << id as a 64-bit mask, for building masked_by sets. */
    inline uint64_t alarm_bit(uint8_t id) { return (uint64_t)1u << id; }

    /** @brief Default RECENT-mode window (s): AlarmCondition::recent_s overrides
     *  per-condition (0 = this default, < 0 = never expires). */
    static constexpr int32_t DEFAULT_RECENT_S = 7 * 24 * 3600;   // one week

    /**
     * @brief One alarm condition: static metadata + model-owned dynamic state.
     *
     * Static prefix site-defined (aggregate-init; row index = bit index).
     * Dynamic suffix left zeroed, model-owned. A threshold of -1 disables that
     * action; dwell in seconds.
     */
    struct AlarmCondition {
        /** @name Static metadata (site-defined)
         *  @{ */
        /** short code, e.g. "CPU_LOAD" (null = undefined slot) */
        const char* code;
        /** human-readable description */
        const char* desc;
        /** alarms that suppress this one while active */
        uint64_t    masked_by;
        /** min ON dwell (s) before a WARNING "START" log (-1 off) */
        int32_t     log_on_s;
        /** min OFF dwell (s) before an INFO "END" log (-1 off) */
        int32_t     log_off_s;
        /** min ON dwell (s) before an MQTT notification (-1 off) */
        int32_t     mqtt_on_s;
        /** min OFF dwell (s) before an MQTT notification (-1 off) */
        int32_t     mqtt_off_s;
        /** RECENT-mode window (s): 0 = DEFAULT_RECENT_S, <0 = never expires. */
        int32_t     recent_s;
        /** hold active this long (s) after the raw bit clears (0 = no latch). */
        int32_t     latch_s;
        /** min ON time (s) from onset, even if the raw bit clears (0 = none).
         *  Local anti-flicker net behind the PLC's debounce. */
        int32_t     min_on_s;
        /** @} */

        /** @name Dynamic state (model-owned; leave zeroed in a site table)
         *  @{ */
        /** effective active (raw bit set, or still latched) */
        bool        active;
        /** active and no alarm in masked_by is active */
        bool        unmasked;
        /** MQTT ON notification sent (cleared when OFF) */
        bool        on_notified;
        /** MQTT OFF notification sent (cleared when ON) */
        bool        off_notified;
        /** "alarm START" logged this episode */
        bool        log_on_sent;
        /** "alarm END" logged this episode */
        bool        log_off_sent;
        /** operator ack'd this episode (cleared on the next ON edge);
         *  informational, never alters active/unmasked. */
        bool        acknowledged;
        /** uptime of the last active/inactive transition */
        uint64_t    edge_ms;
        /** uptime of the last ON transition (onset, 0 if never); drives the
         *  AlarmsPage start-time sort. */
        uint64_t    started_ms;
        /** latch countdown after the raw bit cleared (0 = not latched).
         *  Wrap-immune; drained by the step elapsed. */
        uint32_t    latch_remaining_ms;
        /** minimum-ON countdown from onset (0 = elapsed), seeded to min_on_s
         *  on the ON edge. Wrap-immune. */
        uint32_t    min_on_remaining_ms;
        /** @} */
    };

    /** @brief Bit index of the condition whose code equals @p code, or -1 if
     *  none. The table is the single source of truth (no parallel id enum); code
     *  lookups are how generic firmware and the site mock-PLC name the alarms
     *  they assert. */
    inline int alarm_index(const AlarmCondition* table, uint8_t count,
                           const char* code) {
        if (table == nullptr || code == nullptr) return -1;
        for (uint8_t i = 0; i < count; i++)
            if (table[i].code != nullptr && std::strcmp(table[i].code, code) == 0)
                return (int)i;
        return -1;
    }

    /**
     * @brief Formats an alarm transition into a topic/payload for a Publisher.
     *
     * Builds "<prefix>/alarms/<CODE>" + "ON"/"OFF" and forwards it to a
     * Publisher. The model calls notify_on/off when an alarm crosses its MQTT
     * ON/OFF dwell threshold; the transport is wired later via set_publisher(),
     * so it is a no-op until then (null publisher disables it).
     */
    class AlarmNotifier {
    private:
        /** @name State
         *  @{ */
        const char* _prefix;   // topic root, e.g. "botanik/greenhouse"
        Publisher*  _pub;      // transport (null = no-op)
        /** @} */

    public:
        /** @brief @p prefix is the MQTT topic root. */
        explicit AlarmNotifier(const char* prefix)
            : _prefix(prefix && prefix[0] ? prefix : "botanik"), _pub(nullptr) {}

        /** @brief Wire the transport (called once at firmware setup). */
        void set_publisher(Publisher* pub) { _pub = pub; }

        void notify_on(const AlarmCondition& a)  { _emit(a, true); }
        void notify_off(const AlarmCondition& a) { _emit(a, false); }

    private:
        /** @brief Build "<prefix>/alarms/<CODE>" + ON/OFF and publish (no-op if
         *  unwired). */
        void _emit(const AlarmCondition& a, bool on) {
            if (_pub == nullptr) return;
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/alarms/%s",
                     _prefix, a.code ? a.code : "?");
            _pub->publish(topic, on ? "ON" : "OFF");
        }
    };

    /**
     * @brief Owns the dynamic alarm state over a site-supplied condition table.
     *
     * step() runs once per loop with the remote raw bitset + uptime: merges
     * local bits, advances dwell, raises log lines / notifications, recomputes
     * the masked view.
     */
    class AlarmModel : public Botanik::Component {
    private:
        /** @name State
         *  @{ */
        AlarmCondition  _conditions[ALARM_BITS]; // model-owned working array
        uint8_t         _plc_count;      // N: PLC/site conditions at [0..N)
        uint8_t         _count;          // N + M: total conditions scanned
        /** (1<<N)-1: masks the remote word to the PLC range */
        uint64_t        _site_mask;
        Logs::Logger*   _logger;         // ALARM-topic log sink (may be null)
        AlarmNotifier*  _notifier;       // MQTT sink (may be null)
        /** runtime-sourced alarm bits (platform range) */
        uint64_t        _local;
        /** last merged raw ((remote & mask) | local) */
        uint64_t        _raw;
        /** uptime of the last step (latch countdown) */
        uint64_t        _last_now;
        /** suppress latch decrement on first step */
        bool            _first_step;
        uint8_t         _active_count;   // active conditions this step
        /** active and unmasked conditions this step */
        uint8_t         _unmasked_count;
        /** @} */

    public:
        /** @brief Bind to the site PLC table (@p site_count at [0..N)) and the
         *  optional platform-health table (@p platform_count appended at
         *  [N..N+M)). Both const, copied in; N + M <= ALARM_BITS. Alarms start
         *  "settled OFF" so a never-active one never announces an END it never
         *  started. */
        AlarmModel(const AlarmCondition* site_table, uint8_t site_count,
                   Logs::Logger* logger, AlarmNotifier* notifier = nullptr,
                   const AlarmCondition* platform = nullptr,
                   uint8_t platform_count = 0)
            : _plc_count(0), _count(0), _site_mask(0),
              _logger(logger), _notifier(notifier),
              _local(0), _raw(0), _last_now(0), _first_step(true),
              _active_count(0), _unmasked_count(0) {
            BOTANIK_ASSERT(site_count + platform_count <= ALARM_BITS);
            // Clamp defensively so a too-large count can't overflow the array.
            if (site_count > ALARM_BITS) site_count = ALARM_BITS;
            if (platform_count > (uint8_t)(ALARM_BITS - site_count))
                platform_count = (uint8_t)(ALARM_BITS - site_count);
            std::memset(_conditions, 0, sizeof(_conditions));
            // Site alarms [0..N), platform alarms [N..N+M): disjoint ranges.
            for (uint8_t i = 0; i < site_count; i++)
                _conditions[i] = site_table[i];
            for (uint8_t j = 0; j < platform_count; j++)
                _conditions[site_count + j] = platform[j];
            _plc_count = site_count;
            _count     = (uint8_t)(site_count + platform_count);
            _site_mask = (site_count >= 64) ? ~(uint64_t)0
                                            : (((uint64_t)1 << site_count) - 1);
            for (uint8_t i = 0; i < _count; i++) {   // boot state: settled OFF
                _conditions[i].off_notified = true;
                _conditions[i].log_off_sent = true;
            }
        }

        /** @name Local alarms
         *  @{ */

        /** @brief Set/clear a runtime-sourced alarm bit (merged next step()). */
        void set_local(uint8_t id, bool on) {
            if (id >= ALARM_BITS) return;
            if (on) _local |=  alarm_bit(id);
            else    _local &= ~alarm_bit(id);
        }

        /** @brief Set/clear a local alarm by table code (e.g. "CPU_LOAD"); no-op
         *  if the code is absent, so generic firmware needn't depend on the site
         *  defining it. */
        void set_local(const char* code, bool on) {
            int i = alarm_index(_conditions, _count, code);
            if (i >= 0) set_local((uint8_t)i, on);
        }

        /** @brief Mark an active condition acknowledged by table code; false if
         *  unknown or inactive. Informational only (cleared on next ON edge). */
        bool ack(const char* code) {
            int i = alarm_index(_conditions, _count, code);
            if (i < 0 || !_conditions[i].active) return false;
            _conditions[i].acknowledged = true;
            return true;
        }

        /** @brief Acknowledge every active condition; returns the count ack'd. */
        uint8_t ack_all() {
            uint8_t n = 0;
            for (uint8_t i = 0; i < _count; i++)
                if (_conditions[i].active) { _conditions[i].acknowledged = true; n++; }
            return n;
        }

        /** @} */

        /** @brief Merge the PLC + local bitsets, advance each condition, raise
         *  due log lines / notifications. */
        void step(uint64_t remote_raw, uint64_t now) {
            announce_step(_logger, "AlarmModel");
            // Mask the remote word to the PLC range (a stray high bit can't drive a
            // platform condition), then merge the local bits on top.
            _raw = (remote_raw & _site_mask) | _local;
            // dt = 0 on the first step (no latch decrement yet).
            uint32_t dt = _first_step ? 0 : (uint32_t)(now - _last_now);
            _first_step = false;
            _last_now   = now;
            _active_count = 0;
            _unmasked_count = 0;
            for (uint8_t i = 0; i < _count; i++) {
                AlarmCondition& c = _conditions[i];
                bool was = c.active;
                bool raw_on = ((_raw >> i) & 1u) != 0;
                // Two wrap-immune countdowns drained by the elapsed time: the
                // latch holds active for latch_s after the raw bit clears
                // (refreshed while set); the minimum-ON hold keeps it active
                // >= min_on_s from onset (anti-flicker).
                if (c.latch_remaining_ms > 0)
                    c.latch_remaining_ms = (c.latch_remaining_ms > dt)
                                         ? (uint32_t)(c.latch_remaining_ms - dt) : 0;
                if (c.min_on_remaining_ms > 0)
                    c.min_on_remaining_ms = (c.min_on_remaining_ms > dt)
                                          ? (uint32_t)(c.min_on_remaining_ms - dt) : 0;
                if (raw_on && c.latch_s > 0)
                    c.latch_remaining_ms = (uint32_t)c.latch_s * 1000u;
                bool on = raw_on || (c.latch_remaining_ms > 0) ||
                          (c.min_on_remaining_ms > 0);
                if (on != was) {
                    c.edge_ms = now;
                    // Reset log_on_sent on the ON edge (not OFF) so it survives
                    // the OFF episode and tells END from FLAP below; seed the
                    // minimum-ON hold.
                    if (on) { c.off_notified = false; c.log_off_sent = false;
                              c.log_on_sent  = false;
                              c.acknowledged = false;  // a fresh episode is unacknowledged
                              c.started_ms = now;      // onset, for start-time ordering
                              if (c.min_on_s > 0)
                                  c.min_on_remaining_ms = (uint32_t)c.min_on_s * 1000u; }
                    else    { c.on_notified  = false; }
                }
                c.active   = on;
                c.unmasked = on && ((_raw & c.masked_by) == 0);
                if (on) {
                    _active_count++;
                    if (c.unmasked) _unmasked_count++;
                }

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
                        // END with no matching START this episode = toggled too
                        // fast to dwell past log_on_s: report a FLAP, not a
                        // misleading bare END.
                        if (_logger) {
                            if (c.log_on_sent) _logger->info(PlcIO::ALARM_END, "%s", label);
                            else               _logger->notice(PlcIO::ALARM_FLAP, "%s", label);
                        }
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
        /** @brief Index of @p code, or -1 (POST uses it to verify set_local
         *  resolves). */
        int index_of(const char* code) const {
            return alarm_index(_conditions, _count, code);
        }

        /** @} */
    };

}}

#endif // ALARMS_ALARMS_H
