// REVIEW_35: DONE
#ifndef ALARMS_ALARMS_H
#define ALARMS_ALARMS_H

/**
 * @file alarms.h
 * @brief Alarm model - per-condition dwell/latch state machine.
 *
 * This file tries to implement alarms in a way that matches standard
 * alarm-handling procedures in industrial PLC systems. For each condition
 * we use a global boolean variable, and we track :
 *  - the raw input
 *  - the active state
 *  - the dwell since the last edge
 *  - what this episode already announced to detect FLAPs
 *  - the operator's acknowledgement, cleared on each fresh ON edge.
 *
 * Site conditions fill [0..N) by scanning the variable table for SCOPE_A rows;
 * the platform-health ones (platform.h) follow at [N..N+M). N + M must fit
 * Config::ALARM_SLOTS.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>                  // snprintf (MqttAlarmNotifier::_emit)
#include <cstring>

#include "../../libbotanik_config.h"
#include "../core/component.h"     // Component (first-step announce)
#include "../logs/logger.h"        // the ALARM-topic log lines
#include "../utils/assert.h"       // BOTANIK_ASSERT
#include "../utils/text.h"         // compare_str
#include "../vars/model.h"         // VarRef / Bank
#include "publisher.h"             // Publisher (notifier transport seam)

namespace Botanik { namespace Alarms {

    /** @brief @p id as a 64-bit mask */
    inline uint64_t alarm_bit(uint8_t id) { 
        return (uint64_t)1u << id; 
    }

    /** @brief countdown drain, @p rem - @p dt, floored at 0. */
    inline uint32_t drain_ms(uint32_t rem, uint32_t dt) {
        return rem > dt ? rem - dt : 0;
    }

    // forward declaration for the alarm's side effect sink
    struct AlarmSink;

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

        const char* code;       ///< short display code (max 8 chars)
        const char* name;       ///< the variable this reads, by name
        const char* desc;       ///< human-readable description
        const char* notes;      ///< alarm remediation instructions

        int32_t     on_s;       ///< time ON  before announcing (-1: off)
        int32_t     off_s;      ///< time OFF before announcing (-1: off)
        int32_t     recent_s;   ///< duration the event is "recent" (-1: forever)
        int32_t     hold_on_s;  ///< hold ON this long after raw clears (0: none)
        int32_t     min_on_s;   ///< minimum ON time from onset (0: no latch)

        /** @} */

        /** @name Dynamic state
         *  @{ */

        Vars::VarRef ref;           ///< the PLC variable carrying this condition
        bool        local_raw;      ///< actual raw value
        bool        active;         ///< alarm active or latched

        bool        on_sent;        ///< ON  transition announced (log + notify)
        bool        off_sent;       ///< OFF transition announced (log + notify)

        bool        user_ack;       ///< alarm acknowledged by the user
        uint64_t    ack_ms;         ///< uptime the user ack'd it (valid if user_ack)
        uint64_t    edge_ms;        ///< time since last transition
        uint64_t    started_ms;     ///< time since last ON transition

        uint32_t    hold_on_rem_ms; ///< countdown from OFF edge ; 0 = elapsed
        uint32_t    min_on_rem_ms;  ///< countdown from ON edge ; 0 = elapsed

        /** @} */

        /** @brief Advance this condition one tick. 
         *  @note Drains the countdowns, do some edge bookkeeping, 
         *        recompute local state and announce transitions. */
        void step(bool raw_on, uint32_t dt, uint64_t now, const AlarmSink& sink);

    private:
        /** @brief The ON event is due: unannounced, enabled (on_s >= 0), and
         *         @p dwell_ms has reached the threshold. */
        bool due_on(uint64_t dwell_ms) const {
            return !on_sent && on_s >= 0 &&
                   dwell_ms >= (uint64_t)on_s * 1000u;
        }

        /** @brief Same for the OFF event, on off_sent / off_s. */
        bool due_off(uint64_t dwell_ms) const {
            return !off_sent && off_s >= 0 &&
                   dwell_ms >= (uint64_t)off_s * 1000u;
        }
    };

    /** @brief Index of the alarm @p key names, otherwise -1. @p key may be
     *  either spelling of the same condition: the short display CODE (`SYS_RST`)
     *  or the variable NAME (`alarm_system_reset`).
     *
     *  Both, because both are on screen. The alarm bar, the ALARMS page and
     *  `alarms ack <code>`'s own help show the code; `alarms list` and the
     *  variable table show the name. Matching only one of them meant the
     *  spelling the operator was looking at was the one that did not work.
     *
     *  A linear scan, and deliberately so: this is only ever reached from
     *  operator input (`alarms ack <code>`) and from bring-up
     *  (HealthMonitor resolves its six platform codes ONCE into indices, see
     *  health.h). Nothing per-beat resolves a name - a site condition carries
     *  the VarRef its constructor resolved, and the platform ones are driven by
     *  index. */
    inline int alarm_index(const AlarmCondition* table, uint8_t count,
                           const char* key) {
        if (table == nullptr || key == nullptr) return -1;
        for (uint8_t i = 0; i < count; i++)
            if (Utils::compare_str(table[i].code, key) ||
                Utils::compare_str(table[i].name, key))
                return (int)i;
        return -1;
    }

    /** @brief Abstract alarm-transition sink the model fans out to.
     * 
     *  A bit like the Logs::LogWriter abstraction, but for alarms. Provides
     *  hooks for their three distinct notifications : ON, OFF and FLAP. */
    struct AlarmNotifier {
        virtual ~AlarmNotifier() {}

        /** @brief A condition crossed its ON dwell threshold. */
        virtual void notify_on(const AlarmCondition& a) = 0;
        /** @brief A condition ended cleanly (its ON event had been raised). */
        virtual void notify_off(const AlarmCondition& a) = 0;
        /** @brief A condition ended without a matching ON event (a flap). */
        virtual void notify_flap(const AlarmCondition& a) = 0;
    };

    /** @brief AlarmNotifier that formats a transition as an MQTT topic/payload.
     *
     *  Publishes "ON/OFF/FLAP" at topic "<prefix>/alarms/<CODE>" */
    class MqttAlarmNotifier : public AlarmNotifier {
    private:
        /** @name State
         *  @{ */
        const char* _prefix;   // topic root, e.g. "botanik/greenhouse"
        Publisher*  _pub;      // transport (null = no-op)
        /** @} */

    public:
        /** @brief Construct a new notifier with the given @p prefix MQTT topic root. */
        explicit MqttAlarmNotifier(const char* prefix) : _prefix(prefix), _pub(nullptr) {}

        /** @brief Wire the transport (called once at firmware setup). */
        void set_publisher(Publisher* pub) { _pub = pub; }

        void notify_on(const AlarmCondition& a)   override { _emit(a, "ON"); }
        void notify_off(const AlarmCondition& a)  override { _emit(a, "OFF"); }
        void notify_flap(const AlarmCondition& a) override { _emit(a, "FLAP"); }

    private:
        /** @brief Build an MQTT entry and publish it. */
        void _emit(const AlarmCondition& a, const char* state) {
            if (_pub == nullptr) return;
            char topic[Config::MQTT_TOPIC_LEN];
            snprintf(topic, sizeof(topic), "%s/%s/%s", _prefix,
                     Config::MQTT_ALARM_TOPIC, a.name ? a.name : "?");
            _pub->publish(topic, state);
        }
    };

    /** @brief This sink is passed to AlarmCondition when stepping through it 
     *         and allows it to log events and fan out to the notifiers 
     *         registered in the controller. */
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
    class AlarmController : public Botanik::Component {
    private:
        /** @name State
         *  @{ */
        /// model-owned working array (site conditions, then platform ones)
        AlarmCondition  _conditions[Config::ALARM_SLOTS];
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
        /** @brief Builds the site's alarm table by scanning @p io for SCOPE_A rows.
         * 
         *  The platform alarms stay a separate const table because they name no
         *  variable at all - they are driven by set_local() from the scheduler
         *  metrics. */
        AlarmController(const Vars::Bank*     io,
                              Logs::Logger*   logger,
                        const AlarmCondition* platform = nullptr,
                              uint8_t         platform_count = 0)
            : Component(logger, "AlarmController"),
              _site_count(0), _count(0), _io(io), _notifier_count(0),
              _last_now(0), _first_step(true), _active_count(0) {

            // zero the alarms table
            std::memset(_conditions, 0, sizeof(_conditions));

            // store the id of the first alarm condition
            uint8_t n = 0;

            // look in the IO table for alarm-scope variables
            if (io != nullptr) {
                for (uint16_t i = 0; i < io->count() && n < Config::ALARM_SLOTS; i++) {
                    // fetch the generic variable metadata
                    const Vars::Variable& d = *io->var(i).meta();
                    // continue through the loop unless it's an alarm-variable
                    if (d.scope != Vars::SCOPE_A) continue;
                    // get the alarm-specific variable metadata
                    const Vars::AlarmSpec sp = Vars::alarm_spec(d);
                    // build a new alarm condition
                    AlarmCondition& c = _conditions[n];
                    c.ref.index   = i;
                    c.code        = sp.code;
                    c.name        = d.name;
                    c.desc        = d.desc;
                    c.notes       = d.notes;
                    c.on_s        = sp.on_s;
                    c.off_s       = sp.off_s;
                    c.recent_s    = sp.recent_s;
                    c.hold_on_s   = sp.hold_on_s;
                    c.min_on_s    = sp.min_on_s;
                    // increment the alarm condition count
                    n++;
                }
            }

            // ensure we still have enough place to add the platform alarms
            BOTANIK_ASSERT(n + platform_count <= Config::ALARM_SLOTS);
            if (platform_count > (uint8_t)(Config::ALARM_SLOTS - n))
                platform_count = (uint8_t)(Config::ALARM_SLOTS - n);

            // add the platform-defined alarm conditions to the table
            for (uint8_t j = 0; j < platform_count; j++) {
                _conditions[n + j] = platform[j];
                // Driven by set_local(); they read no variable.
                _conditions[n + j].ref.index = Vars::VAR_NONE;
            }
            // count site-specific alarms and total alarm count
            _site_count = n;
            _count      = (uint8_t)(n + platform_count);
            // initialize boot state of alarm conditions
            for (uint8_t i = 0; i < _count; i++)     // boot state: settled OFF
                _conditions[i].off_sent = true;
        }

        /** @name Notifiers
         *  @{ */

        /** @brief Register a transition sink; false if failed.
         *  @note May fail if @p n is null, already registered, or the list is full. */
        bool add_notifier(AlarmNotifier* n) {
            // nullref and overflow checks
            if (n == nullptr || _notifier_count >= Config::ALARM_NOTIFIERS_MAX) return false;
            // loop through existing to avoid double-registration
            for (uint8_t i = 0; i < _notifier_count; i++) if (_notifiers[i] == n) return false;
            // register the notifier
            _notifiers[_notifier_count++] = n;
            return true;
        }

        /** @brief Unregister a previously added notifier; false if failed. */
        bool remove_notifier(AlarmNotifier* n) {
            // loop through the table to find the notifier
            for (uint8_t i = 0; i < _notifier_count; i++) {
                // go to next row if the current row's notifier does not match
                if (_notifiers[i] != n) continue;
                // once we've found it, left-shift remaining notifiers
                for (uint8_t j = i; j + 1 < _notifier_count; j++)
                    _notifiers[j] = _notifiers[j + 1];
                // decrement the notifier count and return
                _notifier_count--;
                return true;
            }
            return false;
        }

        /** @brief Determines if at least one notifier is registered. */
        bool has_notifiers() const { 
            return _notifier_count > 0;
         }

        /** @} */

        /** @name Local alarms
         *  @{ */

        /** @brief Set/clear an alarm's local raw override
         *  @note A site alarm's override is OR'd with its payload boolean. */
        void set_local(uint8_t id, bool on) {
            if (id >= _count) return;
            _conditions[id].local_raw = on;
        }

        /** @brief Mark an active condition acknowledged, by code or by variable
         *  name (see alarm_index). Operator input, so it resolves here rather
         *  than being pre-resolved: there is nothing to pre-resolve it FROM. */
        bool ack(const char* name) {
            // find the alarm's index
            int i = alarm_index(_conditions, _count, name);
            // check that the condition is properly defined
            if (i < 0 || !_conditions[i].active) return false;
            // update the condition
            _conditions[i].user_ack = true;
            _conditions[i].ack_ms   = _last_now;
            return true;
        }

        /** @brief Acknowledge every active condition; returns how many. */
        uint8_t ack_all() {
            uint8_t n = 0;
            for (uint8_t i = 0; i < _count; i++) {
                // skip the conditions that are not active
                if (!_conditions[i].active) continue;
                // acknowledge and count it
                _conditions[i].user_ack = true;
                _conditions[i].ack_ms   = _last_now;
                n++;
            }
            return n;
        }

        /** @} */

        /** @name State queries
         *  @{ */

        /** @brief Number of table entries scanned. */
        uint8_t  count() const { return _count; }

        /** @brief Active conditions this step. */
        uint8_t  active_count() const { return _active_count; }

        /** @brief Condition @p i (0 <= i < count()). */
        const AlarmCondition& condition(uint8_t i) const { return _conditions[i]; }

        /** @brief Index of the condition @p key names - its code or its variable
         *  name - or -1. Called ONCE per code at bring-up (health.h); the beat
         *  never resolves a name. */
        int index_of(const char* key) const {
            return alarm_index(_conditions, _count, key);
        }

        /** @} */

    protected:

        /** @brief Read each condition's raw input, combine to the local override
         *         and step through the condition's handling code. */
        void on_step(uint64_t now) override {
            // measure dt (use 0 for first step)
            uint32_t dt = _first_step ? 0 : (uint32_t)(now - _last_now);
            _first_step = false;
            _last_now   = now;
            _active_count = 0;
            // build an alarm sink from our logger and notifiers
            AlarmSink sink{_logger, _notifiers, _notifier_count};
            for (uint8_t i = 0; i < _count; i++) {
                // for each condition
                AlarmCondition& c = _conditions[i];
                // combine the raw value with the local override
                bool raw_on = c.local_raw || _site_raw(i);
                // step through the condition
                c.step(raw_on, dt, now, sink);
                // count active conditions
                if (c.active) _active_count++;
            }
        }

    private:

        /** @brief Get the site condition's variable, as a boolean. */
        bool _site_raw(uint8_t i) const {
            // false if platform-alarm or if the IO table is not found
            if (i >= _site_count || _io == nullptr) return false;
            // get the variable's actual value from the IO table
            return _io->var(_conditions[i].ref).b();
        }
    };

}}

#endif // ALARMS_ALARMS_H
