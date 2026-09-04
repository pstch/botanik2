#ifndef ALARMS_ALARMS_H
#define ALARMS_ALARMS_H

/**
 * @file alarms.h
 * @brief Alarm model - per-condition dwell/latch state machine.
 *
 * A condition is a `SCOPE_A` row in the variable table: the boolean that raises
 * it plus the announce policy it runs on. Per condition the model tracks:
 *  - the raw input
 *  - the active state
 *  - the dwell since the last edge
 *  - what this episode already announced, which is what detects a FLAP
 *  - the operator's acknowledgement, cleared on each fresh ON edge.
 *
 * Conditions are built by scanning the variable table for SCOPE_A rows.
 */

#include <cstdint>
#include <cstring>

#include "libbotanik/alarms/source.h"
#include "libbotanik/config/alarms.h"

#include "libbotanik/core/component.h"
#include "libbotanik/logs/logger.h"
#include "libbotanik/utils/assert.h"

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Alarms {

    /** @brief @p id as a 64-bit mask */
    inline uint64_t alarm_bit(uint8_t id) {
        return (uint64_t)1u << id;
    }

    // A hard ceiling, not a sizing preference: widen the mask (and
    // AlarmBar::unmasked_set) before raising the site-overridable slot count.
    static_assert(Config::ALARM_SLOTS <= 64,
                  "ALARM_SLOTS cannot exceed 64: alarm_bit() shifts into a uint64_t");

    /** @brief countdown drain, @p rem - @p dt, floored at 0. */
    inline uint32_t drain_ms(uint32_t rem, uint32_t dt) {
        return rem > dt ? rem - dt : 0;
    }

    // forward declaration for the alarm's side effect sink
    struct AlarmSink;

    /** @brief One alarm condition: the row it reads, plus the model-owned
     *  dynamic state.
     *
     *  The row is BORROWED, not copied: its text and announce policy are
     *  already flash-resident on the `SCOPE_A` descriptor, so the accessors
     *  below read straight through and there is no second copy to disagree. */
    struct AlarmCondition {
        /** @name The row
         *  @{ */

        /** @brief Where this condition comes from; null on an unfilled slot.
         *  Borrowed, and outlives the controller. */
        const AlarmSourceTable* src;
        uint16_t idx;               ///< this condition's index in @c src

        /** @} */

        /** @name Row metadata
         *  Read through @c var, so there is no second copy to keep in step.
         *  @{ */

        /** @brief Short display code, max 9 (AlarmsPage::CODE_W). */
        const char* code() const { return _spec().code; }
        /** @brief What carries this condition, by name. */
        const char* name()  const { return (src != nullptr) ? src->name(idx)  : nullptr; }
        /** @brief Human-readable description. */
        const char* desc()  const { return (src != nullptr) ? src->desc(idx)  : nullptr; }
        /** @brief Remediation instructions. */
        const char* notes() const { return (src != nullptr) ? src->notes(idx) : nullptr; }

        /** @brief Time ON before announcing (-1: never announce). */
        int16_t on_s()      const { return _spec().on_s; }
        /** @brief Time OFF before announcing (-1: never announce). */
        int16_t off_s()     const { return _spec().off_s; }
        /** @brief How long the event stays "recent" (0: the default window,
         *  <0: forever). */
        int32_t recent_s()  const { return _spec().recent_s; }
        /** @brief Stay active this long past the last raw true (0: none). */
        int16_t hold_s()    const { return _spec().hold_s; }

        /** @} */

        /** @name Dynamic state
         *  @{ */

        bool        active;         ///< alarm active or latched

        bool        on_sent;        ///< ON  transition announced (log + notify)
        bool        off_sent;       ///< OFF transition announced (log + notify)

        bool        user_ack;       ///< alarm acknowledged by the user
        uint64_t    ack_ms;         ///< uptime the user ack'd it (valid if user_ack)
        uint64_t    edge_ms;        ///< time since last transition
        uint64_t    started_ms;     ///< time since last ON transition

        uint32_t    hold_rem_ms;    ///< countdown past the last raw true; 0 = elapsed
        /** @brief How long the RAW condition has been continuously true. What
         *  the ON dwell is measured against: hold_s EXTENDS an episode, so a
         *  dwell taken from the episode would be unreachable on any row
         *  declaring both, and a one-beat blip would annunciate in full.
         *  Saturating: no dwell is measured in weeks. */
        uint32_t    raw_ms;

        /** @} */

        /** @brief Advance one activation: drain the hold, stamp any edge,
         *  recompute the active state and announce a transition whose dwell
         *  threshold has elapsed.
         *
         *  @p forced says the row is being held by an operator override. A
         *  forced condition follows the force immediately: the hold is for
         *  smoothing a real signal that flaps, and a hand-held row does not
         *  flap. */
        void step(bool raw_on, bool forced, uint32_t dt, uint64_t now,
                  const AlarmSink& sink);

    private:
        /** @brief The announce policy off the row; all-zero on an empty slot,
         *  which reads as "announce immediately, no latch" and never fires
         *  because such a slot has no variable to raise it. */
        AlarmSpec _spec() const {
            return (src != nullptr) ? src->spec(idx)
                                    : AlarmSpec{nullptr, 0, 0, 0, 0};
        }

        /** @brief The ON event is due: unannounced, enabled (on_s >= 0), and
         *         the RAW condition has held for the threshold. */
        bool due_on(uint64_t dwell_ms) const {
            const int16_t t = on_s();
            return !on_sent && t >= 0 && dwell_ms >= (uint64_t)t * 1000u;
        }

        /** @brief The OFF event is due: unannounced, enabled (off_s >= 0), and
         *         @p dwell_ms has reached the threshold. */
        bool due_off(uint64_t dwell_ms) const {
            const int16_t t = off_s();
            return !off_sent && t >= 0 && dwell_ms >= (uint64_t)t * 1000u;
        }
    };

    /** @brief Abstract alarm-transition sink the model fans out to.
     *
     *  Shaped like Logs::LogWriter, with a hook per transition: ON, OFF, FLAP.
     *  Concrete sinks live elsewhere (@see alarms/notifier.h), so the model
     *  depends on nothing but the variable table. */
    struct AlarmNotifier {
        virtual ~AlarmNotifier() {}

        /** @brief A condition crossed its ON dwell threshold. */
        virtual void notify_on(const AlarmCondition& a) = 0;
        /** @brief A condition ended cleanly (its ON event had been raised). */
        virtual void notify_off(const AlarmCondition& a) = 0;
        /** @brief A condition ended without a matching ON event (a FLAP). */
        virtual void notify_flap(const AlarmCondition& a) = 0;
    };

    /** @brief Handed to AlarmCondition::step() so it can log events and fan out
     *  to the notifiers registered in the controller. */
    struct AlarmSink {
        Logs::Logger*         logger;          ///< ALARM-topic sink (null = no logging)
        AlarmNotifier* const* notifiers;       ///< borrowed notifier array (may be null)
        uint8_t               notifier_count;  ///< entries in @c notifiers

        /** @name Transition log lines
         *
         *  The label is the row's own desc, so the message is site DATA and not
         *  a literal anyone can fold at. test_site_names.cpp caps it at the 40
         *  characters logs/format.h asks of a first line.
         *  @{ */
        void log_start(const char* label) const {
            if (logger) logger->warning(Logs::ALARM_BEG, "%s", label);
        }
        void log_end(const char* label) const {
            if (logger) logger->info(Logs::ALARM_END, "%s", label);
        }
        void log_flap(const char* label) const {
            if (logger) logger->notice(Logs::ALARM_FLAP, "%s", label);
        }
        /** @} */
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
    public:
        void register_tasks() override;
    private:

    private:
        /** @name State
         *  @{ */
        /// model-owned working array, in table order
        AlarmCondition  _conditions[Config::ALARM_SLOTS];
        uint8_t         _count;          // conditions scanned out of the table
        /** @brief SCOPE_A rows the table DECLARES, which is @c _count unless the
         *  table overran the slots. The pair is what makes a lost tail visible;
         *  @c _count alone saturates and cannot say. */
        uint16_t        _declared;
        /** @brief Condition numbers sorted by CODE, built once by the
         *  constructor. Names need no mapping of their own: they go through the
         *  bank's index to a row, and the refs are already sorted. */
        uint8_t         _by_code[Config::ALARM_SLOTS];
        /** where the conditions come from (null -> every condition reads false,
         *  which is what an unbound model is). Non-const: forcing a condition
         *  is the source's business, since only it knows where the boolean
         *  lives. */
        AlarmSourceTable* _src;
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
        /** @brief Bind where the conditions come from.
         *
         *  The scan itself is in on_setup(), NOT here: a bank-backed source has
         *  nothing to offer until the table is frozen. */
        AlarmController(const SetupContext& ctx, AlarmSourceTable* src)
            : Component(ctx, "AlarmController"),
              _count(0), _declared(0), _src(src), _notifier_count(0),
              _last_now(0), _first_step(true), _active_count(0) {
            std::memset(_conditions, 0, sizeof(_conditions));
        }


        /** @brief Take one slot per alarm the source declares, in its order, so
         *  condition *i* reads source entry *i*. */
        void on_setup(uint64_t now_ms) override {
            (void)now_ms;
            uint8_t  n     = 0;
            uint16_t found = (_src != nullptr) ? _src->count() : 0;
            for (uint16_t i = 0; i < found && n < Config::ALARM_SLOTS; i++) {
                AlarmCondition& c = _conditions[n];
                c.src = _src;
                c.idx = i;
                n++;
            }
            // More alarms than slots loses the tail silently.
            BOTANIK_ASSERT(found <= Config::ALARM_SLOTS);
            _count    = n;
            _declared = found;
            for (uint8_t i = 0; i < _count; i++)     // boot state: settled OFF
                _conditions[i].off_sent = true;
            _build_code_index();
        }

        /** @name Notifiers
         *  @{ */

        /** @brief Register a transition sink; false if failed.
         *  @note May fail if @p n is null, already registered, or the list is full. */
        bool add_notifier(AlarmNotifier* n) {
            // A notifier that did not register means transitions reach nobody:
            // the alarm still raises, and no one is told.
            BOTANIK_ASSERT(n == nullptr || _notifier_count < Config::ALARM_NOTIFIERS_MAX);
            if (n == nullptr || _notifier_count >= Config::ALARM_NOTIFIERS_MAX) return false;
            for (uint8_t i = 0; i < _notifier_count; i++) if (_notifiers[i] == n) return false;
            _notifiers[_notifier_count++] = n;
            return true;
        }

        /** @brief Unregister a previously added notifier; false if it was not
         *  registered. */
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

        /** @brief Whether at least one notifier is registered. */
        bool has_notifiers() const { return _notifier_count > 0; }

        /** @} */

        /** @name Acknowledgement
         *  @{ */

        /** @brief Mark an active condition acknowledged, by code or by variable
         *  name (see index_of). */
        bool ack(const char* name) {
            const int i = index_of(name);
            if (i < 0 || !_conditions[i].active) return false;
            _conditions[i].user_ack = true;
            _conditions[i].ack_ms   = _last_now;
            return true;
        }

        /** @brief Take an acknowledgement back, by code or by variable name.
         *  The same gesture both ways: an ack given to the wrong row is a
         *  mis-keystroke, not a decision to live with. */
        bool unack(const char* name) {
            const int i = index_of(name);
            if (i < 0 || !_conditions[i].user_ack) return false;
            _conditions[i].user_ack = false;
            return true;
        }

        /** @brief Acknowledge every active condition; returns how many. */
        uint8_t ack_all() {
            uint8_t n = 0;
            for (uint8_t i = 0; i < _count; i++) {
                if (!_conditions[i].active) continue;
                _conditions[i].user_ack = true;
                _conditions[i].ack_ms   = _last_now;
                n++;
            }
            return n;
        }

        /** @} */

        /** @name Forcing
         *  A condition's boolean can be HELD by an operator; the model reads it
         *  through the source either way, so these only pass the gesture on.
         *  False means it did not happen - a source that cannot be forced, or
         *  nothing forced to release or save.
         *  @{ */

        /** @brief Flip what condition @p i is held at: unforced, it is held at
         *  the opposite of what it reads now. */
        bool force_toggle(uint8_t i) {
            if (_src == nullptr || i >= _count) return false;
            return _src->force(i, !_raw(i));
        }

        /** @brief Release condition @p i's force; the live signal takes over. */
        bool force_clear(uint8_t i) {
            if (_src == nullptr || i >= _count) return false;
            return _src->force_clear(i);
        }

        /** @brief Persist condition @p i's force, so it survives a reboot. */
        bool force_save(uint8_t i) {
            if (_src == nullptr || i >= _count) return false;
            return _src->force_save(i);
        }

        /** @} */

        /** @name State queries
         *  @{ */

        /** @brief Number of table entries scanned, i.e. conditions this model
         *  actually drives. */
        uint8_t  count() const { return _count; }

        /** @brief SCOPE_A rows the variable table declares. Above count() means
         *  the table overran ALARM_SLOTS and the tail is not being driven. */
        uint16_t declared_count() const { return _declared; }

        /** @brief Active conditions this step. */
        uint8_t  active_count() const { return _active_count; }

        /** @brief Condition @p i (0 <= i < count()). */
        const AlarmCondition& condition(uint8_t i) const { return _conditions[i]; }

        /** @brief Whether condition @p i's variable is being FORCED, and to
         *  what.
         *
         *  A forced condition behaves exactly like a real one (the override
         *  reaches the model through Var::raw()), which is why a display has to
         *  be able to say so.
         *
         *  @return false when the row is live; on true, @p on says what it is
         *  held at. */
        bool forced(uint8_t i, bool* on = nullptr) const {
            if (_src == nullptr || i >= _count) return false;
            if (!_src->forced(i)) return false;
            if (on != nullptr) *on = _src->raw(i);
            return true;
        }

        /** @brief Index of the condition @p key names, or -1. @p key may be
         *  either spelling: the short display CODE (`SYS_RST`) or the variable
         *  NAME (`alarm_system_reset`).
         *
         *  Both, because both are on screen: the alarm bar and the ALARMS page
         *  show the code, `alarms list` and the variable table the name.
         *
         *  Two binary searches, no scan: codes through the mapping built at
         *  construction, names through the bank's name index and then the refs
         *  (sorted, since the conditions were scanned in table order). */
        int index_of(const char* key) const {
            if (key == nullptr) return -1;
            const int by_code = _find_code(key);
            if (by_code >= 0) return by_code;
            if (_src == nullptr) return -1;
            // The length-bounded find(), deliberately: this is operator input,
            // so a miss is a typo and must not count as an unresolved name.
            const int by_name = (int)_src->find(key, (uint16_t)strlen(key));
            // ...and bounded against the CONDITIONS, not the source. A source
            // may declare more alarms than the controller has slots for - that
            // is its documented contract, and what count() reports - so a name
            // past the last slot resolves in the table and has nothing here to
            // acknowledge. Every caller indexes _conditions[] with this.
            return (by_name >= 0 && by_name < (int)_count) ? by_name : -1;
        }

        /** @} */

    protected:

        /** @brief Read each condition's raw input, combine to the local override
         *         and step through the condition's handling code. */

        void on_service(uint64_t now) override {
            const uint32_t dt = _first_step ? 0 : (uint32_t)(now - _last_now);
            _first_step   = false;
            _last_now     = now;
            _active_count = 0;

            AlarmSink sink{_logger, _notifiers, _notifier_count};
            for (uint8_t i = 0; i < _count; i++) {
                AlarmCondition& c = _conditions[i];
                c.step(_raw(i), _src != nullptr && _src->forced(i), dt, now, sink);
                if (c.active) _active_count++;
            }
        }

    private:

        /** @brief Sort _by_code, once. Binary insertion over at most
         *  ALARM_SLOTS entries; the table never changes again. */
        void _build_code_index() {
            for (uint8_t i = 0; i < _count; i++) _by_code[i] = i;
            for (uint8_t i = 1; i < _count; i++) {
                const uint8_t c = _by_code[i];
                uint8_t lo = 0, hi = i;
                while (lo < hi) {
                    const uint8_t mid = (uint8_t)(lo + (hi - lo) / 2);
                    if (_code_cmp(_conditions[c].code(),
                                  _conditions[_by_code[mid]].code()) < 0) hi = mid;
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
                if (_code_cmp(_conditions[_by_code[mid]].code(), key) < 0)
                    lo = (uint8_t)(mid + 1);
                else
                    hi = mid;
            }
            if (lo >= _count) return -1;
            const uint8_t i = _by_code[lo];
            return (_code_cmp(_conditions[i].code(), key) == 0) ? (int)i : -1;
        }

        /** @brief Condition @p i's raw input. Forcing it is the source's
         *  business: with a variable table that is an override on the row, and
         *  it arrives here already applied. */
        bool _raw(uint8_t i) const {
            return (_src != nullptr) && _src->raw(i);
        }
    };

}}

#endif // ALARMS_ALARMS_H
