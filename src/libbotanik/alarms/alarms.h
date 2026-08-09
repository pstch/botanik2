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
 * Conditions are built by scanning the variable table for SCOPE_A rows.
 * .
 */

#include <cstdint>                          // uint8_t/uint32_t/uint64_t
#include <cstdio>                           // snprintf
#include <cstring>                          // std::memset

#include "libbotanik/config/alarms.h"     // Config::ALARM_* / HEALTH_*
#include "libbotanik/config/net.h"        // Config::NET_* / DNS_* / NTP_* / MQTT_*

#include "libbotanik/alarms/publisher.h"    // Alarms::Publisher
#include "libbotanik/core/component.h"      // Component
#include "libbotanik/logs/logger.h"         // Logger
#include "libbotanik/utils/assert.h"        // BOTANIK_ASSERT
#include "libbotanik/utils/text.h"          // Utils::compare_str
#include "libbotanik/vars/model.h"          // Vars::VarRef / Bank

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

    /** @brief One alarm condition: static metadata + model-owned dynamic state. */
    struct AlarmCondition {
        /** @name Static metadata
         *  @{ */

        const char* code;           ///< short display code (max 9: AlarmsPage::CODE_W)
        const char* name;           ///< the variable this reads, by name
        const char* desc;           ///< human-readable description
        const char* notes;          ///< alarm remediation instructions

        int32_t     on_s;           ///< time ON  before announcing (-1: off)
        int32_t     off_s;          ///< time OFF before announcing (-1: off)
        int32_t     recent_s;       ///< duration the event is "recent" (-1: forever)
        int32_t     hold_on_s;      ///< hold ON this long after raw clears (0: none)
        int32_t     min_on_s;       ///< minimum ON time from onset (0: no latch)

        /** @} */

        /** @name Dynamic state
         *  @{ */

        Vars::VarRef ref;           ///< the variable carrying this condition
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
        /** @brief Advance one activation.
         *
         *  @p forced says the row is being held by an operator override. A
         *  forced condition follows the force immediately: the hold-on and
         *  minimum-on latches are for smoothing a real signal that flaps, and a
         *  hand-held row does not flap. Without it, forcing a platform alarm on
         *  and back off left it up for its whole 300 s hold. */
        void step(bool raw_on, bool forced, uint32_t dt, uint64_t now,
                  const AlarmSink& sink);

    private:
        /** @brief The ON event is due: unannounced, enabled (on_s >= 0), and
         *         @p dwell_ms has reached the threshold. */
        bool due_on(uint64_t dwell_ms) const {
            return !on_sent && on_s >= 0 &&
                   dwell_ms >= (uint64_t)on_s * 1000u;
        }

        /** @brief The OFF event is due: unannounced, enabled (off_s >= 0), and
         *         @p dwell_ms has reached the threshold. */
        bool due_off(uint64_t dwell_ms) const {
            return !off_sent && off_s >= 0 &&
                   dwell_ms >= (uint64_t)off_s * 1000u;
        }
    };

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
        /** @brief A condition ended without a matching ON event (a FLAP). */
        virtual void notify_flap(const AlarmCondition& a) = 0;
    };

    // TODO: move to notifier.h
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
            if (logger) logger->warning(Logs::ALARM_BEG, "%s", label);
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
     * step(now) runs once per loop: it reads each condition's variable, advances
     * dwell, and raises log lines / notifications. No aggregate bitset is built
     * or stored.
     */
    class AlarmController : public Botanik::Component {
    private:
        /** @name State
         *  @{ */
        /// model-owned working array, in table order
        AlarmCondition  _conditions[Config::ALARM_SLOTS];
        uint8_t         _count;          // conditions scanned out of the table
        /** @brief Condition numbers sorted by CODE, built once by the
         *  constructor. The other spelling needs no mapping of its own: a name
         *  goes through the bank's index to a row, and the conditions were
         *  scanned in table order, so their refs are already sorted. */
        uint8_t         _by_code[Config::ALARM_SLOTS];
        /** the variable table the condition refs resolve against (null -> every
         *  condition reads false, which is what an unbound model is) */
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
        /** @brief Build the condition table by scanning @p io for SCOPE_A rows.
         *
         *  Every alarm, whoever declared it: a site's own and the library's
         *  platform-health ones alike are rows of that one table (see
         *  alarms/platform.h), so this is one scan in one order. */
        AlarmController(const Vars::Bank* io, Logs::Logger* logger)
            : Component(logger, "AlarmController"),
              _count(0), _io(io), _notifier_count(0),
              _last_now(0), _first_step(true), _active_count(0) {

            // zero the alarms table
            std::memset(_conditions, 0, sizeof(_conditions));

            uint8_t n = 0;
            if (io != nullptr) {
                // One bound, because there is one table: the site's rows and the
                // library's are scanned together, so there is no reservation for
                // an over-long site table to eat into. It loses its own tail,
                // and BOTANIK_ASSERT below says so on a debug build.
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
                    n++;
                }
                // A table with more alarms than slots loses its tail silently,
                // which is the one failure a count cannot show after the fact.
                BOTANIK_ASSERT(n < Config::ALARM_SLOTS);
            }
            _count = n;
            for (uint8_t i = 0; i < _count; i++)     // boot state: settled OFF
                _conditions[i].off_sent = true;
            _build_code_index();
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

        /** @name Acknowledgement
         *  @{ */

        /** @brief Mark an active condition acknowledged, by code or by variable
         *  name (see index_of). */
        bool ack(const char* name) {
            // find the alarm's index
            int i = index_of(name);
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

        /** @brief Whether condition @p i's variable is being FORCED, and to
         *  what.
         *
         *  An alarm is a row like any other, so an override on it is how you
         *  exercise the logic watching it - and it reaches the model through
         *  Var::raw(), so a forced condition behaves exactly like a real one.
         *  That is correct and is precisely why a display has to be able to say
         *  so: on the page an operator reads during a fault, the two are
         *  otherwise indistinguishable.
         *
         *  @return false when the row is live; on true, @p on says what it is
         *  held at. */
        bool forced(uint8_t i, bool* on = nullptr) const {
            if (_io == nullptr || i >= _count) return false;
            const Vars::Var v = _io->var(_conditions[i].ref);
            if (!v.overridden()) return false;
            if (on != nullptr) *on = v.b();
            return true;
        }

        /** @brief Index of the condition @p key names, or -1. @p key may be
         *  either spelling: the short display CODE (`SYS_RST`) or the variable
         *  NAME (`alarm_system_reset`).
         *
         *  Both, because both are on screen. The alarm bar, the ALARMS page and
         *  `alarms ack <code>`'s own help show the code; `alarms list` and the
         *  variable table show the name. Matching only one meant the spelling
         *  the operator was looking at was the one that did not work.
         *
         *  Two binary searches, no scan: the codes through the mapping built at
         *  construction, the names through the bank's own name index and then
         *  through the refs, which are sorted because the conditions were
         *  scanned in table order. */
        int index_of(const char* key) const {
            if (key == nullptr) return -1;
            const int by_code = _find_code(key);
            if (by_code >= 0) return by_code;
            if (_io == nullptr) return -1;
            // The length-bounded find(), deliberately: this is operator input,
            // so a miss is a typo at the keyboard, not a table defect, and it
            // must not be counted as an unresolved name.
            return _find_ref(_io->find(key, (uint16_t)strlen(key)));
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
                // step it on whatever its variable currently reads
                c.step(_raw(i), _io != nullptr && _io->var(c.ref).overridden(),
                       dt, now, sink);
                // count active conditions
                if (c.active) _active_count++;
            }
        }

    private:

        /** @brief Sort _by_code, once. Binary insertion, over at most
         *  ALARM_SLOTS entries: the comparisons are strcmps and the shuffling is
         *  uint8_t moves. Same shape as the bank's name index, same reason -
         *  the table is built here and never changes again. */
        void _build_code_index() {
            for (uint8_t i = 0; i < _count; i++) _by_code[i] = i;
            for (uint8_t i = 1; i < _count; i++) {
                const uint8_t c = _by_code[i];
                uint8_t lo = 0, hi = i;
                while (lo < hi) {
                    const uint8_t mid = (uint8_t)(lo + (hi - lo) / 2);
                    if (_code_cmp(_conditions[c].code,
                                  _conditions[_by_code[mid]].code) < 0) hi = mid;
                    else                                                lo = (uint8_t)(mid + 1);
                }
                for (uint8_t k = i; k > lo; k--) _by_code[k] = _by_code[k - 1];
                _by_code[lo] = c;
            }
        }

        /** @brief strcmp, with a null code sorting after everything and matching
         *  nothing. */
        static int _code_cmp(const char* a, const char* b) {
            if (a == nullptr) return (b == nullptr) ? 0 : 1;
            if (b == nullptr) return -1;
            return std::strcmp(a, b);
        }

        /** @brief Condition whose CODE is @p key, or -1. */
        int _find_code(const char* key) const {
            uint8_t lo = 0, hi = _count;
            while (lo < hi) {
                const uint8_t mid = (uint8_t)(lo + (hi - lo) / 2);
                if (_code_cmp(_conditions[_by_code[mid]].code, key) < 0)
                    lo = (uint8_t)(mid + 1);
                else
                    hi = mid;
            }
            if (lo >= _count) return -1;
            const uint8_t i = _by_code[lo];
            return (_code_cmp(_conditions[i].code, key) == 0) ? (int)i : -1;
        }

        /** @brief Condition reading row @p r, or -1. The conditions were built by
         *  scanning the table in order, so their refs ascend: a binary search,
         *  with nothing to build. */
        int _find_ref(Vars::VarRef r) const {
            if (r.index == Vars::VAR_NONE) return -1;
            uint8_t lo = 0, hi = _count;
            while (lo < hi) {
                const uint8_t mid = (uint8_t)(lo + (hi - lo) / 2);
                if (_conditions[mid].ref.index < r.index) lo = (uint8_t)(mid + 1);
                else                                      hi = mid;
            }
            return (lo < _count && _conditions[lo].ref.index == r.index)
                     ? (int)lo : -1;
        }

        /** @brief Condition @p i's variable, as a boolean. An override on that
         *  variable is what "force this alarm" means now - it reaches here the
         *  same way it reaches everything else, through Var::raw(). */
        bool _raw(uint8_t i) const {
            if (_io == nullptr) return false;
            return _io->var(_conditions[i].ref).b();
        }
    };

}}

#endif // ALARMS_ALARMS_H
