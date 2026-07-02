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
#include "libbotanik/utils/text.h"     // compare_str
#include "libbotanik/logs/logger.h"
#include "libbotanik/core/component.h"      // Component (first-step announce)
#include "libbotanik/alarms/publisher.h" // Publisher (notifier transport seam)

namespace Botanik { namespace Alarms {

    /** @brief Width of the raw alarm bitset (and of the condition table). */
    static constexpr uint8_t ALARM_BITS = 64;

    /** @brief 1 << id as a 64-bit mask, for building masked_by sets. */
    inline uint64_t alarm_bit(uint8_t id) { return (uint64_t)1u << id; }

    /** @brief Wrap-immune countdown drain: @p rem - @p dt, floored at 0. */
    inline uint32_t drain_ms(uint32_t rem, uint32_t dt) {
        return rem > dt ? rem - dt : 0;
    }

    /** @brief Default RECENT-mode window (s): AlarmCondition::recent_s
     *  overrides per-condition (0 = this default, < 0 = never expires). */
    static constexpr int32_t DEFAULT_RECENT_S = 7 * 24 * 3600;   // one week

    struct AlarmSink;   // side-effect sink (log + MQTT), defined below

    /**
     * @brief One alarm condition: static metadata + model-owned dynamic state.
     *
     * Static prefix site-defined (aggregate-init; row index = bit index).
     * Dynamic suffix left zeroed, model-owned. A threshold of -1 disables that
     * action; dwell in seconds.
     */
    struct AlarmCondition {
        /** @name Static metadata
         *  @{ */

        const char* code;       //< short code, max 8 chars
        const char* desc;       //< human-readable description
        uint64_t    masked_by;  //< bitset of alarms that suppress this alarm

        int32_t     log_on_s;   //< time ON  before logging event (-1: off)
        int32_t     log_off_s;  //< time OFF before logging event (-1: off)

        int32_t     mqtt_on_s;  //< time ON  before MQTT notification (-1: off)
        int32_t     mqtt_off_s; //< time OFF before MQTT notification (-1: off)
        int32_t     recent_s;   //< duration the event is "recent" (-1: forever)

        int32_t     off_delay_s;    //< hold ON this long after raw clears (0: none)
        int32_t     min_on_s;   //< minimum ON time from onset (0: no latch)
        /** @} */

        /** @name Dynamic state
         *  @{ */
        bool        active;         //< alarm active or latched
        bool        unmasked;       //< active and not masked by other alarms

        bool        mqtt_on_sent;   //< MQTT ON  notification sent
        bool        mqtt_off_sent;  //< MQTT OFF notification sent

        bool        log_on_sent;    //< logging ON  event written
        bool        log_off_sent;   //< logging OFF event written

        bool        user_ack;       //< alarm acknowledged by the user
        uint64_t    edge_ms;        //< time since last transition
        uint64_t    started_ms;     //< time since last ON transition

        /** latch countdown after the raw bit cleared (0 = not latched).
         *  Wrap-immune; drained by the step elapsed. */
        uint32_t    off_delay_rem_ms;
        /** minimum-ON countdown from onset (0 = elapsed), seeded to min_on_s
         *  on the ON edge. Wrap-immune. */
        uint32_t    min_on_rem_ms;
        /** @} */

        /** @brief Advance this condition one tick. Drains the off-delay /
         *  minimum-ON countdowns by @p dt, recomputes active/unmasked (the
         *  latter against the model's @p merged_raw word), seeds the edge
         *  bookkeeping on a transition, and raises any due log / MQTT event
         *  through @p sink. @p raw_on is this condition's merged raw bit; @p
         *  now is the current uptime (ms). */
        void step(bool raw_on, uint32_t dt, uint64_t now, uint64_t merged_raw,
                  const AlarmSink& sink);

    private:
        /** @brief A once-per-episode event is due: unsent, enabled (>= 0), and
         *  dwell has reached the threshold. */
        static bool due(bool sent, int32_t threshold_s, uint64_t dwell_ms) {
            return !sent && threshold_s >= 0 &&
                   dwell_ms >= (uint64_t)threshold_s * 1000u;
        }
        /** @brief This OFF edge is a FLAP, not a clean end: no matching ON event
         *  was raised this episode, yet ON raising was enabled (>= 0). When ON is
         *  disabled by design (< 0, e.g. SYS_RST) the OFF is always a plain end. */
        static bool is_flap(bool on_sent, int32_t on_threshold_s) {
            return !on_sent && on_threshold_s >= 0;
        }
    };

    /** @brief Bit index of the alarm with matching @p code, otherwise -1. */
    inline int alarm_index(const AlarmCondition* table, uint8_t count,
                           const char* code) {

        // bail if we have no valid pointer to the alarm table or alarm code
        if (table == nullptr || code == nullptr) return -1;
        // look for a matching code
        for (uint8_t i = 0; i < count; i++)
            if (Utils::compare_str(table[i].code, code))
                return (int)i;
        // bail if we could'nt find one
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

        void notify_on(const AlarmCondition& a)   { _emit(a, "ON"); }
        void notify_off(const AlarmCondition& a)  { _emit(a, "OFF"); }
        void notify_flap(const AlarmCondition& a) { _emit(a, "FLAP"); }

    private:
        /** @brief Build "<prefix>/alarms/<CODE>" + @p state and publish (no-op
         *  if unwired). */
        void _emit(const AlarmCondition& a, const char* state) {
            if (_pub == nullptr) return;
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/alarms/%s",
                     _prefix, a.code ? a.code : "?");
            _pub->publish(topic, state);
        }
    };

    /**
     * @brief Side-effect sink an AlarmCondition raises events through: the
     *        ALARM-topic log writer and the MQTT notifier.
     *
     * Bundling them keeps AlarmCondition::step free of the model's wiring. Both
     * sinks may be null (no-op). The MQTT path is the feature seam: it is
     * compiled out when BOTANIK_FEATURE_MQTT is off, so the notifier field is
     * simply ignored there (the condition keeps its mqtt_* bookkeeping either
     * way).
     */
    struct AlarmSink {
        Logs::Logger*  logger;     //< ALARM-topic sink (null = no logging)
        AlarmNotifier* notifier;   //< MQTT sink (null = none; MQTT-gated)

        void log_start(const char* label) const {
            if (logger) logger->warning(Logs::ALARM_START, "%s", label);
        }
        void log_end(const char* label) const {
            if (logger) logger->info(Logs::ALARM_END, "%s", label);
        }
        void log_flap(const char* label) const {
            if (logger) logger->notice(Logs::ALARM_FLAP, "%s", label);
        }
        void notify_on(const AlarmCondition& c) const {
#if BOTANIK_FEATURE_MQTT
            if (notifier) notifier->notify_on(c);
#else
            (void)c;
#endif
        }
        void notify_off(const AlarmCondition& c) const {
#if BOTANIK_FEATURE_MQTT
            if (notifier) notifier->notify_off(c);
#else
            (void)c;
#endif
        }
        void notify_flap(const AlarmCondition& c) const {
#if BOTANIK_FEATURE_MQTT
            if (notifier) notifier->notify_flap(c);
#else
            (void)c;
#endif
        }
    };

    /** @brief Per-condition tick: the model loop body, owned by the condition.
     *  See the declaration for the parameter contract. */
    inline void AlarmCondition::step(bool raw_on, uint32_t dt, uint64_t now,
                                     uint64_t merged_raw, const AlarmSink& sink) {
        // 1. Drain the two wrap-immune latch countdowns, then refresh the
        //    off-delay while the raw bit is set: the off-delay holds active for
        //    off_delay_s after the raw bit clears, the minimum-ON hold keeps it
        //    active >= min_on_s from onset (anti-flicker). `on` holds until the
        //    raw bit and both latches are clear.
        off_delay_rem_ms = drain_ms(off_delay_rem_ms, dt);
        min_on_rem_ms    = drain_ms(min_on_rem_ms, dt);
        if (raw_on && off_delay_s > 0)
            off_delay_rem_ms = (uint32_t)off_delay_s * 1000u;
        bool on = raw_on || off_delay_rem_ms > 0 || min_on_rem_ms > 0;

        // 2. On a transition, stamp the edge and reset the per-episode
        //    bookkeeping. The *_on_sent flags reset on the ON edge (not OFF) so
        //    they survive the OFF episode and tell a clean end from a FLAP below.
        if (on != active) {
            edge_ms = now;
            if (on) {                       // fresh ON episode
                mqtt_off_sent = log_off_sent = false;
                mqtt_on_sent  = log_on_sent  = false;
                user_ack   = false;         // a fresh episode is unacknowledged
                started_ms = now;           // onset, for start-time ordering
                if (min_on_s > 0) min_on_rem_ms = (uint32_t)min_on_s * 1000u;
            }
        }
        active   = on;
        unmasked = on && (merged_raw & masked_by) == 0;

        // 3. Raise any log / MQTT event whose dwell threshold has elapsed. The
        //    OFF side reports a FLAP when no matching ON event fired this episode.
        uint64_t dwell = now - edge_ms;
        const char* label = desc ? desc : (code ? code : "");
        if (on) {
            if (due(log_on_sent,  log_on_s,  dwell)) { sink.log_start(label); log_on_sent  = true; }
            if (due(mqtt_on_sent, mqtt_on_s, dwell)) { sink.notify_on(*this); mqtt_on_sent = true; }
        } else {
            if (due(log_off_sent, log_off_s, dwell)) {
                if (is_flap(log_on_sent, log_on_s)) sink.log_flap(label);
                else                                sink.log_end(label);
                log_off_sent = true;
            }
            if (due(mqtt_off_sent, mqtt_off_s, dwell)) {
                if (is_flap(mqtt_on_sent, mqtt_on_s)) sink.notify_flap(*this);
                else                                  sink.notify_off(*this);
                mqtt_off_sent = true;
            }
        }
    }

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
                _conditions[i].mqtt_off_sent = true;
                _conditions[i].log_off_sent = true;
            }
        }

        /** @name Local alarms
         *  @{ */

        /** @brief Set/clear a runtime-sourced alarm bit (merged next
         *  step()). */
        void set_local(uint8_t id, bool on) {
            if (id >= ALARM_BITS) return;
            if (on) _local |=  alarm_bit(id);
            else    _local &= ~alarm_bit(id);
        }

        /** @brief Set/clear a local alarm by table code (e.g. "CPU_LOAD");
         *  no-op if the code is absent, so generic firmware needn't depend on
         *  the site defining it. */
        void set_local(const char* code, bool on) {
            int i = alarm_index(_conditions, _count, code);
            if (i >= 0) set_local((uint8_t)i, on);
        }

        /** @brief Mark an active condition acknowledged by table code; false if
         *  unknown or inactive. Informational only (cleared on next ON
         *  edge). */
        bool ack(const char* code) {
            int i = alarm_index(_conditions, _count, code);
            if (i < 0 || !_conditions[i].active) return false;
            _conditions[i].user_ack = true;
            return true;
        }

        /** @brief Acknowledge every active condition; returns the count
         *  ack'd. */
        uint8_t ack_all() {
            uint8_t n = 0;
            for (uint8_t i = 0; i < _count; i++)
                if (_conditions[i].active) { _conditions[i].user_ack = true; n++; }
            return n;
        }

        /** @} */

        /** @brief Merge the PLC + local bitsets, advance each condition, raise
         *  due log lines / notifications. */
        void step(uint64_t remote_raw, uint64_t now) {
            announce_step(_logger, "AlarmModel");
            // Mask the remote word to the PLC range (a stray high bit can't
            // drive a platform condition), then merge the local bits on top.
            _raw = (remote_raw & _site_mask) | _local;
            // dt = 0 on the first step (no latch decrement yet).
            uint32_t dt = _first_step ? 0 : (uint32_t)(now - _last_now);
            _first_step = false;
            _last_now   = now;
            _active_count = 0;
            _unmasked_count = 0;
            AlarmSink sink{_logger, _notifier};
            for (uint8_t i = 0; i < _count; i++) {
                AlarmCondition& c = _conditions[i];
                bool raw_on = ((_raw >> i) & 1u) != 0;
                c.step(raw_on, dt, now, _raw, sink);
                if (c.active) {
                    _active_count++;
                    if (c.unmasked) _unmasked_count++;
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
        /** @brief Active and unmasked conditions (the compact alarm-bar
         *  set). */
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
