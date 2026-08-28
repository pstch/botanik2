#ifndef VARS_HANDLE_H
#define VARS_HANDLE_H

/**
 * @file handle.h
 * @brief The RAM slot, and the handle over (descriptor, slot) that everything
 *        reads a row through.
 * @note The const half is descriptor.h. The TABLE is model.h, which includes
 *       this. See docs/dev/vars.md.
 */

#include <cstdint>

#include "libbotanik/config/vars.h"
#include "libbotanik/vars/descriptor.h"
#include "libbotanik/vars/ref.h"
#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Vars {

    /** @brief One value slot, parallel to the descriptor table. No `def`: the
     *  default is a typed literal in flash.
     *  @note While @c ovr_on, @c ovr SHADOWS @c v, so clearing snaps back to
     *  live. @c ovr_valid is separate so "force to 20 C" and "read as a dead
     *  sensor" are both expressible.
     *  @warning Both halves are PERSISTED - hence the CRITICAL on each restore. */
    struct Slot {
        Scalar   v;
        Scalar   ovr;        ///< forced value, while ovr_on
        uint64_t stamp_ms;   ///< uptime ms of the last good update
        /** @brief T_STR only: this row's pool slot, or STR_NO_SLOT when it did
         *  not fit. Assigned by Bank::init(), never declared. Ahead of the bools
         *  for its 2-byte alignment. */
        uint16_t str_off;
        /** @brief The live value's quality - valid, or invalid and why.
         *  @see Quality */
        Quality  qual;
        bool     ovr_on;     ///< an override is in force
        bool     ovr_valid;  ///< what valid() reports while overridden
        uint8_t  vflags;     ///< ValueFlags
        /** @brief Which control program last claimed this row (registry index),
         *  WRITER_NONE until one does. Catches TWO programs driving one output,
         *  which otherwise shows only as an actuator chattering at beat rate. */
        uint8_t  writer;
        /** @brief Index into the bank's wire array for an AI/AO row, WIRE_NONE
         *  otherwise. Carved out of the spare byte, so the slot stays 32. */
        uint8_t  wire_off;
    };

    /** @brief No program has claimed this row. Not 0, which is a real program
     *  index - the same trap VAR_NONE exists to avoid. */
    static const uint8_t WRITER_NONE = 0xFFu;

    /** @brief This row has no wire value of its own: its value IS what the wire
     *  carries, or it has no binding at all. */
    static const uint8_t WIRE_NONE = 0xFFu;
    // Every field here costs RAM on every row of every site, so the size is
    // pinned rather than left to drift.
    static_assert(sizeof(Slot) == 32, "Slot must stay 32 bytes (RAM per variable)");

    /** @brief A text row that found no room in the pool. Not 0, which is a REAL
     *  slot: an overflowed row parked there keeps its declared capacity and
     *  would be written over the first text row. */
    static const uint16_t STR_NO_SLOT = 0xFFFFu;

    /** @brief The one handle: a non-owning (descriptor, value slot) pair, made
     *  on the fly and passed by value. */
    class Var {
        const Descriptor* _d;
        Slot*          _v;
    public:
        Var(const Descriptor* d, Slot* v) : _d(d), _v(v) {}

        /** @name Read
         *  Three layers and their resolution, each a Value - so a number and
         *  its quality are fetched together and cannot disagree. An invalid row
         *  reads its declared default. @{ */

        /** @brief What the row would read with nothing forced. */
        Value live() const { return Value{ _under(), _d, _d->type, _v->qual }; }

        /** @brief The row's DECLARED value, as a Value ready to convert.
         *
         *  What a control program reads in place of a row that is not
         *  believable: a number somebody chose and reviewed, rather than a
         *  reading of unbounded age. Q_GOOD because the substitution is not in
         *  doubt - whether the ROW is, is what valid() answers. */
        Value declared() const { return Value{ _d->def, _d, _d->type, Q_GOOD }; }

        /** @brief The force, or Q_NONE when there is none. Recorded, not
         *  necessarily driving: CONTROL_WITHHELD MUTES an override without
         *  erasing it, and a muted one is still there and still worth showing. */
        Value override() const {
            if (!_v->ovr_on) return Value{ _d->def, _d, _d->type, Q_NONE };
            return Value{ _v->ovr, _d, _d->type, _v->ovr_valid ? Q_GOOD : Q_FORCED_BAD };
        }

        /** @brief The value in force: the override when one is DRIVING, else
         *  live. The one place an override is applied, and the only place the
         *  mute is consulted - nothing outside has to know about it. */
        Value value() const { return _forced() ? override() : live(); }

        /** @brief value() as @p T, saturating. A digital reads 0/1, so no
         *  consumer branches. */
        template <typename T> T as() const { return value().as<T>(); }
        float    as_f32()  const { return value().as_f32(); }
        bool     as_bool() const { return value().as_bool(); }
        int64_t  as_i64()  const { return value().as_i64(); }
        bool     valid()   const { return value().good(); }

        /** @brief Where this row's text sits in the pool (T_STR only; 0
         *  otherwise, and text_slot().cap is 0 there so nothing writes). */
        uint16_t str_off() const { return _v->str_off; }
        uint64_t stamp() const { return _v->stamp_ms; }

        /** @} */

        /** @name Write
         *  Overloads rather than a template, so set(false) and set(hz(...)) both
         *  resolve. An integer literal is ambiguous; use set_i(). @{ */
        void set(bool v)      { _drive(from_bool(_d->type, v)); }
        void set(float v)     { _drive(from_f32 (_d->type, v)); }
        void set_i(int64_t v) { _drive(from_i64 (_d->type, v)); }

        /** @brief What a DRIVER calls for a 1:1 row: value, timestamp and
         *  validity in one step, so the staleness clock cannot be forgotten.
         *  Judges the declared band here, where the reading arrives - a fresh
         *  value can still be nonsense, and a dead loop reading full scale is
         *  what no timeout catches. Does NOT mark the field dirty: this is a
         *  reading coming IN. @see Bank::publish_wire() for a scaled row. */
        void publish(Scalar v, uint64_t now_ms) {
            _v->v = v; _v->stamp_ms = now_ms;
            _v->qual = value_implausible(*_d, v) ? Q_IMPLAUSIBLE : Q_GOOD;
        }
        /** @brief The quantity, without claiming anything MEASURED it. For a
         *  simulated field: stamping Q_GOOD would keep the row good for ever and
         *  no timeout could catch a device going quiet. */
        void set_physical(Scalar v) { _v->v = v; }
        /** @brief Mark the reading bad, and say why. For a driver rejecting a
         *  well-formed but unbelievable value publish() cannot judge. */
        void invalidate(Quality q) { _v->qual = q; }
        /** @brief Drop to the declared safe state (what a fault does). */
        void safe() { _v->qual = Q_SAFE; _touch_field(); }
        /** @} */

        /** @name Field synchronisation
         *
         *  Whether this output's demand still has to reach the wire. The driver
         *  owns both: it collects what is dirty and clears the mark once a
         *  transaction has carried it.
         *  @{ */

        bool field_dirty() const { return (_v->vflags & VF_FIELD_DIRTY) != 0; }
        void clear_field_dirty() { _v->vflags = (uint8_t)(_v->vflags & ~VF_FIELD_DIRTY); }
        /** @brief Owe this demand to the field again without touching the demand
         *  itself, for a slave that dropped off the bus and came back through its
         *  own power-on defaults. */
        void mark_field_dirty() { _touch_field(); }

        /** @} */

        /** @name Durability
         *
         *  Whether the live value has reached flash. Read-only here: both bits
         *  are the Bank's to set and clear, since only it holds the store.
         *  @{ */

        /** @brief This parameter has been edited but not saved. */
        bool param_unsaved() const { return (_v->vflags & VF_PARAM_UNSAVED) != 0; }
        /** @brief This override is in force but not saved. */
        bool override_unsaved() const { return (_v->vflags & VF_OVR_UNSAVED) != 0; }

        /** @} */

        /** @brief Whether this row is read-modify-written by more than one
         *  program BY DECLARATION (VAR_IN_OUT), so it records no single writer.
         *  @see Bank::claim_writer() */
        bool shared() const { return (_v->vflags & VF_SHARED) != 0; }

        const Descriptor* meta() const { return _d; }
        const char*     name() const { return _d->name; }
        const char*     unit() const { return _d->unit; }
        const char*     description() const { return _d->desc; }
        const char*     notes()       const { return _d->notes; }
        VarType         type() const { return _d->type; }
        VarRole         role() const { return _d->role; }

    private:
        /** @name Override mutation - Bank's, not yours
         *  @warning Each has a storage side only the Bank can reach. A set
         *  skipping the unsaved mark could never be committed; a clear skipping
         *  the store would come BACK. @{ */
        friend class Bank;

        /** @brief Force the variable to @p v, reading as @p valid. Marks the
         *  field dirty: forcing an actuator by hand must move it. */
        void override_set(Scalar v, bool valid = true) {
            _v->ovr = v; _v->ovr_valid = valid; _v->ovr_on = true; _touch_field();
        }
        /** @brief Release the override; the live value takes over. Marks the
         *  field dirty, or the actuator would stay where it was left. */
        void override_clear() { _v->ovr_on = false; _touch_field(); }
        /** @} */

        /** @brief Take a new demand, owing it to the field only if it MOVED: a
         *  control law re-states its whole demand every activation, and dirtying
         *  each one starves the reads behind them. Q_SAFE counts as moved. */
        void _drive(Scalar v) {
            const bool moved = (_v->v.u != v.u) || (_v->qual != Q_GOOD);
            _v->v    = v;
            _v->qual = Q_GOOD;
            if (moved) _touch_field();
        }

        /** @brief What the row reads with no override: the ONE statement of the
         *  fallback rule, shared by raw() and live().
         *  @note A STALE or UNCERTAIN row KEEPS its value - a sensor that
         *  answered a minute ago still knows what the plant was doing, and so
         *  does a law still deriving from one. Q_NO_DATA, Q_SAFE and Q_IMPLAUSIBLE fall
         *  back, because there is nothing behind them to keep. */
        Scalar _under() const {
            return (_v->qual == Q_GOOD || _v->qual == Q_STALE ||
                    _v->qual == Q_UNCERTAIN) ? _v->v : _d->def;
        }

        /** @brief Whether the override reaches this reader: in force and not
         *  muted. CONTROL_WITHHELD mutes an OUTPUT's without erasing it; other
         *  roles are unaffected. */
        bool _forced() const {
            return _v->ovr_on;
        }

        /** @brief Note that a ROLE_OUTPUT's demand has moved. One compare on a
         *  path that is already writing; every other role ignores it. */
        void _touch_field() {
            if (_d->role == ROLE_OUTPUT) _v->vflags = (uint8_t)(_v->vflags | VF_FIELD_DIRTY);
        }
    };

    /** @brief What a write attempt did. A failed PERSIST is not a rejected
     *  EDIT: the value is live either way. */
    enum SetResult : uint8_t {
        SET_OK,             ///< applied (set), or written out (save)
        SET_INVALID,        ///< rejected - malformed, out of range, read-only
        SET_NOT_PERSISTED   ///< live in RAM, but storage refused the write
    };

    /** @brief Where a variable's value survives a reboot. Implementation in
     *  params.h; the Bank needs only the interface, so a test can stub it.
     *  @note Overrides get their own keys: clearing one must not disturb the
     *  parameter setting beneath it. */
    struct VarStore {
        virtual ~VarStore() {}
        /** @brief Load @p d's stored value into @p v, or into @p text for a
         *  T_STR. @p text / @p cap is the row's slot, already bounded by the bank.
         *  @return false when the key is absent, which is normal. */
        virtual bool load(const Descriptor& d, Slot& v, char* text, uint16_t cap) = 0;
        /** @brief Persist @p d's current value. */
        virtual bool save(const Descriptor& d, const Slot& v,
                          const char* text, uint16_t cap) = 0;

        /** @brief Load @p d's stored OVERRIDE into @p v (value + the validity it
         *  reads as). @return false when none is stored, which is the normal
         *  case. */
        virtual bool load_override(const Descriptor& d, Slot& v) = 0;
        /** @brief Persist @p d's current override. */
        virtual bool save_override(const Descriptor& d, const Slot& v) = 0;
        /** @brief Forget @p d's stored override, on every release, so a cleared
         *  one cannot come back at the next boot. */
        virtual bool clear_override(const Descriptor& d) = 0;

        /** @brief Drop @p d's stored value, so it reads its factory default
         *  again after a reboot. @return false when storage refused. */
        virtual bool clear(const Descriptor& d) = 0;

        /** @brief Load @p d's RETAINED value (F_RETAIN); false when none is
         *  stored. Its own key, like an override: a row can have both. */
        virtual bool load_retained(const Descriptor& d, Slot& v) = 0;
        /** @brief Persist @p d's current value as its retained one. */
        virtual bool save_retained(const Descriptor& d, const Slot& v) = 0;
    };

    /** @brief How a Ref's type reaches the slot. Specialised below the Bank,
     *  which its bodies need complete; an unsupported type fails to compile. */
    template <typename T> struct RefIo;

}}  // namespace Botanik::Vars

#endif // VARS_HANDLE_H
