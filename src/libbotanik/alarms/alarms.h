// REVIEW_35: TODO
#ifndef ALARMS_ALARMS_H
#define ALARMS_ALARMS_H

/**
 * @file alarms.h
 * @brief Alarm model - per-condition dwell/latch state machine.
 *
 * Tracks each condition's dwell, raises ALARM-topic log lines / MQTT
 * notifications on the ON/OFF thresholds, and exposes the set for display.
 *
 * There is no aggregate alarm bitset: each condition's raw input is read
 * directly each step. Two disjoint namespaces: site alarms fill [0..N), each
 * naming the variable that carries its condition (its `ref`, normally a
 * computed digital); the generic platform-health alarms (platform.h) follow at
 * [N..N+M), driven only locally (set_local()). A site alarm's local override is
 * OR'd with its variable; a platform alarm names no variable, so it rests on
 * its override alone. N + M must be <= ALARM_BITS.
 *
 * The model owns its working AlarmCondition array: it copies both const tables
 * in at construction, then owns the dynamic state. The site table stays
 * read-only.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>                      // snprintf (AlarmNotifier::_emit)

#include "../utils/assert.h"   // BOTANIK_ASSERT
#include "../utils/text.h"     // compare_str
#include "../logs/logger.h"
#include "../core/component.h"      // Component (first-step announce)
#include "../../libbotanik_config.h"
#include "publisher.h" // Publisher (notifier transport seam)
#include "../vars/model.h"   // VarRef / Bank

namespace Botanik { namespace Alarms {

    /** @brief Maximum number of alarm conditions (site + platform). */
    static constexpr uint8_t ALARM_BITS = 64;

    // The site-facing half of this number is published as Config::SITE_ALARMS_MAX
    // for generated alarm tables to static_assert against. Keep the two agreed:
    // a site that overruns its share is clamped here, silently losing the tail.
    static_assert(ALARM_BITS == Config::ALARM_SLOTS,
                  "Config::ALARM_SLOTS must track Alarms::ALARM_BITS");

    /** @brief 1 << id as a 64-bit mask. The model no longer uses a bitset; this
     *  helps the console layer build a compact per-frame change signature over
     *  the condition set (alarm bar / alarms page). */
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

        /** @brief Short display code (max 8 chars), for the table's first column
         *  and the alarm bar - places where the full variable name is more than
         *  the space is worth. Identity still belongs to @c name; this is a
         *  label. */
        const char* code;
        /** @brief The variable this reads, by name. It is what ack() and
         *  set_local() key on, and a platform alarm - which has no variable -
         *  carries its own literal here instead. */
        const char* name;
        const char* desc;       ///< human-readable description
        /** The variable carrying this condition: the model reads it directly
         *  each step (non-zero = active). Left zeroed for the platform-health
         *  alarms, which are driven locally via set_local() and never read a
         *  variable. */
        Vars::VarRef ref;

        int32_t     on_s;       ///< time ON  before announcing (log + notify; -1: off)
        int32_t     off_s;      ///< time OFF before announcing (log + notify; -1: off)
        int32_t     recent_s;   ///< duration the event is "recent" (-1: forever)

        int32_t     off_delay_s;    ///< hold ON this long after raw clears (0: none)
        int32_t     min_on_s;   ///< minimum ON time from onset (0: no latch)
        /** Extra notes / caveats (may be null), shown in a line below the ALARMS
         *  table. Last in the metadata block so existing positional literals
         *  that stop earlier keep zero-initialising it. */
        const char* notes;
        /** @} */

        /** @name Dynamic state
         *  @{ */
        /** local override raw: the runtime-sourced input for platform-health
         *  alarms (set_local); OR'd with the payload boolean for site alarms. */
        bool        local_raw;
        bool        active;         ///< alarm active or latched

        bool        on_sent;        ///< ON  transition announced (log + notify)
        bool        off_sent;       ///< OFF transition announced (log + notify)

        bool        user_ack;       ///< alarm acknowledged by the user
        uint64_t    ack_ms;         ///< uptime the user ack'd it (valid if user_ack)
        uint64_t    edge_ms;        ///< time since last transition
        uint64_t    started_ms;     ///< time since last ON transition

        /** latch countdown after the raw bit cleared (0 = not latched).
         *  Wrap-immune; drained by the step elapsed. */
        uint32_t    off_delay_rem_ms;
        /** minimum-ON countdown from onset (0 = elapsed), seeded to min_on_s
         *  on the ON edge. Wrap-immune. */
        uint32_t    min_on_rem_ms;
        /** @} */

        /** @brief Advance this condition one tick. Drains the off-delay /
         *  minimum-ON countdowns by @p dt, recomputes active, seeds the edge
         *  bookkeeping on a transition, and announces a due transition (the log
         *  line + the notifier fan-out) through @p sink. @p raw_on is this
         *  condition's raw input (payload boolean or local override); @p now is
         *  the current uptime (ms). */
        void step(bool raw_on, uint32_t dt, uint64_t now, const AlarmSink& sink);

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

    /** @brief Bit index of the alarm named @p name, otherwise -1. */
    inline int alarm_index(const AlarmCondition* table, uint8_t count,
                           const char* name) {
        if (table == nullptr || name == nullptr) return -1;
        for (uint8_t i = 0; i < count; i++)
            if (Utils::compare_str(table[i].name, name))
                return (int)i;
        return -1;
    }

    /**
     * @brief Abstract alarm-transition sink the model fans out to.
     *
     * Mirrors Logs::LogWriter: AlarmModel keeps a list of these and calls the
     * matching hook when a condition crosses its ON/OFF dwell threshold (or an
     * OFF turns out to be a FLAP). Implementations format/forward the transition
     * however they like (MQTT, a second log channel, a buzzer, ...).
     */
    struct AlarmNotifier {
        virtual ~AlarmNotifier() {}

        /** @brief A condition crossed its ON dwell threshold. */
        virtual void notify_on(const AlarmCondition& a) = 0;
        /** @brief A condition ended cleanly (its ON event had been raised). */
        virtual void notify_off(const AlarmCondition& a) = 0;
        /** @brief A condition ended without a matching ON event (a flap). */
        virtual void notify_flap(const AlarmCondition& a) = 0;
    };

    /**
     * @brief AlarmNotifier that formats a transition as an MQTT topic/payload.
     *
     * Builds "<prefix>/alarms/<CODE>" + "ON"/"OFF"/"FLAP" and forwards it to a
     * Publisher. The transport is wired later via set_publisher(), so it is a
     * no-op until then (null publisher disables it).
     */
    class MqttAlarmNotifier : public AlarmNotifier {
    private:
        /** @name State
         *  @{ */
        const char* _prefix;   // topic root, e.g. "botanik/greenhouse"
        Publisher*  _pub;      // transport (null = no-op)
        /** @} */

    public:
        /** @brief @p prefix is the MQTT topic root. */
        explicit MqttAlarmNotifier(const char* prefix)
            : _prefix(prefix && prefix[0] ? prefix : "botanik"), _pub(nullptr) {}

        /** @brief Wire the transport (called once at firmware setup). */
        void set_publisher(Publisher* pub) { _pub = pub; }

        void notify_on(const AlarmCondition& a)   override { _emit(a, "ON"); }
        void notify_off(const AlarmCondition& a)  override { _emit(a, "OFF"); }
        void notify_flap(const AlarmCondition& a) override { _emit(a, "FLAP"); }

    private:
        /** @brief Build "<prefix>/alarms/<CODE>" + @p state and publish (no-op
         *  if unwired). */
        void _emit(const AlarmCondition& a, const char* state) {
            if (_pub == nullptr) return;
            char topic[64];
            snprintf(topic, sizeof(topic), "%s/alarms/%s",
                     _prefix, a.name ? a.name : "?");
            _pub->publish(topic, state);
        }
    };

    /**
     * @brief Side-effect sink an AlarmCondition raises events through: the
     *        ALARM-topic log writer and the registered notifier fan-out.
     *
     * Bundling them keeps AlarmCondition::step free of the model's wiring. The
     * logger may be null (no-op); the notifier list is the model's own array
     * (borrowed, may be empty). Each notify_* hook fans the transition out to
     * every registered notifier, mirroring how Logs::Logger fans an entry out
     * to its writers.
     */
    struct AlarmSink {
        Logs::Logger*         logger;          ///< ALARM-topic sink (null = no logging)
        AlarmNotifier* const* notifiers;       ///< borrowed notifier array (may be null)
        uint8_t               notifier_count;  ///< entries in @c notifiers

        void log_start(const char* label) const {
            if (logger) logger->warning(Logs::ALARM_START, "%s", label);
        }
        void log_end(const char* label) const {
            if (logger) logger->info(Logs::ALARM_END, "%s", label);
        }
        void log_flap(const char* label) const {
            if (logger) logger->notice(Logs::ALARM_FLAP, "%s", label);
        }
        void _fan(void (AlarmNotifier::*m)(const AlarmCondition&), const AlarmCondition& c) const {
            for (uint8_t i = 0; i < notifier_count; i++)
                if (notifiers[i]) (notifiers[i]->*m)(c);
        }
        void notify_on(const AlarmCondition& c)   const { _fan(&AlarmNotifier::notify_on, c); }
        void notify_off(const AlarmCondition& c)  const { _fan(&AlarmNotifier::notify_off, c); }
        void notify_flap(const AlarmCondition& c) const { _fan(&AlarmNotifier::notify_flap, c); }
    };

    /**
     * @brief Owns the dynamic alarm state over a site-supplied condition table.
     *
     * step(now) runs once per loop: it reads each condition's raw input (its
     * payload boolean OR its local override), advances dwell, and raises log
     * lines / notifications. No aggregate bitset is built or stored.
     */
    class AlarmModel : public Botanik::Component {
    private:
        /** @name State
         *  @{ */
        AlarmCondition  _conditions[ALARM_BITS]; // model-owned working array
        uint8_t         _site_count;      // N: site conditions at [0..N)
        uint8_t         _count;          // N + M: total conditions scanned
        /** the variable tables the site alarm refs resolve against (may be
         *  null -> site alarms rest on their local override only) */
        const Vars::Bank* _io;
        /** registered transition sinks (fan-out, mirrors Logger's writers) */
        AlarmNotifier*  _notifiers[Config::ALARM_NOTIFIERS_MAX];
        uint8_t         _notifier_count; // entries used in _notifiers
        /** uptime of the last step (latch countdown) */
        uint64_t        _last_now;
        /** suppress latch decrement on first step */
        bool            _first_step;
        uint8_t         _active_count;   // active conditions this step
        /** @} */

    public:
        /** @brief Bind to the site alarm table (@p site_count at [0..N)) and the
         *  optional platform-health table (@p platform_count appended at
         *  [N..N+M)). Both const, copied in; N + M <= ALARM_BITS. @p io is what
         *  the site alarm refs resolve against (null = none). Alarms start
         *  "settled OFF" so a never-active one never announces an END it never
         *  started. */
        AlarmModel(const AlarmCondition* site_table, uint8_t site_count,
                   Logs::Logger* logger,
                   const AlarmCondition* platform = nullptr,
                   uint8_t platform_count = 0,
                   const Vars::Bank* io = nullptr)
            : Component(logger, "AlarmModel"),
              _site_count(0), _count(0), _io(io),
              _notifier_count(0),
              _last_now(0), _first_step(true),
              _active_count(0) {
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
            _site_count = site_count;
            _count     = (uint8_t)(site_count + platform_count);
            for (uint8_t i = 0; i < _count; i++)     // boot state: settled OFF
                _conditions[i].off_sent = true;
        }

        /** @name Notifiers
         *  @{ */

        /** @brief Register a transition sink; false if @p n is null, already
         *  registered, or the list is full. */
        bool add_notifier(AlarmNotifier* n) {
            if (n == nullptr || _notifier_count >= Config::ALARM_NOTIFIERS_MAX)
                return false;
            for (uint8_t i = 0; i < _notifier_count; i++)
                if (_notifiers[i] == n) return false;   // no double-register
            _notifiers[_notifier_count++] = n;
            return true;
        }

        /** @brief Unregister a previously added notifier; false if not
         *  found. */
        bool remove_notifier(AlarmNotifier* n) {
            for (uint8_t i = 0; i < _notifier_count; i++) {
                if (_notifiers[i] != n) continue;
                for (uint8_t j = i; j + 1 < _notifier_count; j++)
                    _notifiers[j] = _notifiers[j + 1];
                _notifier_count--;
                return true;
            }
            return false;
        }

        /** @brief True if at least one notifier is registered. */
        bool has_notifiers() const { return _notifier_count > 0; }

        /** @} */

        /** @name Local alarms
         *  @{ */

        /** @brief Set/clear a condition's local raw override (applied next
         *  step()). Platform-health alarms are driven only this way; a site
         *  alarm's override is OR'd with its payload boolean. */
        void set_local(uint8_t id, bool on) {
            if (id >= _count) return;
            _conditions[id].local_raw = on;
        }

        /** @brief Set/clear a local alarm by table code (e.g. "alarm_cpu_load");
         *  no-op if the code is absent, so generic firmware needn't depend on
         *  the site defining it. */
        void set_local(const char* name, bool on) {
            int i = alarm_index(_conditions, _count, name);
            if (i >= 0) set_local((uint8_t)i, on);
        }

        /** @brief Mark an active condition acknowledged by table code; false if
         *  unknown or inactive. Informational only (cleared on next ON
         *  edge). */
        bool ack(const char* name) {
            int i = alarm_index(_conditions, _count, name);
            if (i < 0 || !_conditions[i].active) return false;
            _conditions[i].user_ack = true;
            _conditions[i].ack_ms   = _last_now;
            return true;
        }

        /** @brief Acknowledge every active condition; returns the count
         *  ack'd. */
        uint8_t ack_all() {
            uint8_t n = 0;
            for (uint8_t i = 0; i < _count; i++)
                if (_conditions[i].active) {
                    _conditions[i].user_ack = true;
                    _conditions[i].ack_ms   = _last_now;
                    n++;
                }
            return n;
        }

        /** @} */

        /** @name Queries
         *  @{ */

        /** @brief Number of table entries scanned. */
        uint8_t  count() const { return _count; }
        /** @brief Active conditions this step. */
        uint8_t  active_count() const { return _active_count; }
        /** @brief Condition @p i (0 <= i < count()). */
        const AlarmCondition& condition(uint8_t i) const { return _conditions[i]; }
        /** @brief Index of @p code, or -1 (POST uses it to verify set_local
         *  resolves). */
        int index_of(const char* name) const {
            return alarm_index(_conditions, _count, name);
        }

        /** @} */

    protected:
        /** @brief Read each condition's raw input (site payload boolean OR its
         *  local override) and advance it, raising due log lines / notifications.
         *  Component::step(now) is the public entry (first-step announce +
         *  on_step); no aggregate bitset is built. */
        void on_step(uint64_t now) override {
            // dt = 0 on the first step (no latch decrement yet).
            uint32_t dt = _first_step ? 0 : (uint32_t)(now - _last_now);
            _first_step = false;
            _last_now   = now;
            _active_count = 0;
            AlarmSink sink{_logger, _notifiers, _notifier_count};
            for (uint8_t i = 0; i < _count; i++) {
                AlarmCondition& c = _conditions[i];
                bool raw_on = c.local_raw || _site_raw(i);
                c.step(raw_on, dt, now, sink);
                if (c.active) _active_count++;
            }
        }

    private:
        /** @brief This site condition's variable, as a boolean. False for the
         *  platform-health range (i >= _site_count) or with no tables bound. */
        bool _site_raw(uint8_t i) const {
            if (i >= _site_count || _io == nullptr) return false;
            return _io->var(_conditions[i].ref).b();
        }
    };

}}

#endif // ALARMS_ALARMS_H
