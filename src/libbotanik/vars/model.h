#ifndef VARS_MODEL_H
#define VARS_MODEL_H

/**
 * @file model.h
 * @brief The Bank, which owns the table, and Var, the handle every row-level
 *        operation hangs off.
 * @note The slot is handle.h and the const half descriptor.h, both included
 *       here, so a consumer needs only this one header. See @ref dev_vars.
 */

#include <cstdint>

#include "libbotanik/config/vars.h"
#include "libbotanik/core/component.h"
#include "libbotanik/vars/descriptor.h"
#include "libbotanik/vars/handle.h"
#include "libbotanik/vars/ref.h"
#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Logs { class Logger; } }
namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Vars {

    class Var;

    /** @brief What a handle with no row reads: zeros, no unit, no band. Shared
     *  by the Bank's out-of-range index and by a Var built with no bank at all,
     *  so "the sink" is one object rather than one per holder. */
    extern const Descriptor sink_descriptor;

    /** @brief What everything is handed to reach the variables: the generated
     *  table, one slot per row, and the string and wire pools beside them.
     *
     *  It holds no store. Making an edit durable is `Vars::Persist`, a component
     *  of its own, which does the RAM half through the bank inside it.
     *  @note A bad index - VAR_NONE included - resolves to an inert sink, never
     *  CLAMPING: clamping would alias it onto a real row, so a typo could drive
     *  an actual relay. */
    class Bank : public Botanik::Component {

        /** @name The table
         *  Borrowed: the descriptors and the name order are the site's
         *  flash-resident generated arrays. @{ */

        const Descriptor* _rows  = nullptr;
        /** @brief Row indices in name order, generated sorted, so find() binary
         *  searches a const array and nothing is built at boot. Null for an
         *  ad-hoc table with no order beside it, which find() then walks. */
        const uint16_t*   _order = nullptr;
        uint16_t          _count = 0;
        /** @brief The K_PARAM run, contiguous by construction; the bounds are
         *  generated constants, so nothing scans for them. */
        uint16_t          _param_first = 0;
        uint16_t          _param_count = 0;

        /** @}
         *  @name The bank's own RAM
         *  Mutable so var() and the readers stay const: `const Bank&` gates the
         *  BANK's API, not what a Var may drive. @{ */

        mutable Slot   _slots[Config::VAR_MAX];
        mutable char   _pool[Config::STR_POOL_BYTES];
        /** @brief The count on the wire for each analog row, indexed by the
         *  slot's wire_off. Only those rows need one: every other row's wire
         *  value IS its value. */
        mutable Scalar _wire[Config::ANALOG_ROWS_MAX];
        uint16_t       _pool_used  = 0;
        uint8_t        _wire_count = 0;

        /** @brief The inert sink a bad index resolves to. Cleared on every
         *  access: scratch, never shared state. */
        mutable Slot   _sink;

        /** @} */

    public:
        /** @brief The expiry sweep, at VAR_EXPIRY_MS in PHASE_INPUT: a row must
         *  age out BEFORE anything reads it, so one beat is coherent. */
        void register_tasks() override;

        Bank(const SetupContext& ctx = SetupContext()) : Component(ctx, "vars") {}

        /** @brief Take the generated table and lay the RAM out over it: seed
         *  every slot from its declared default, hand the text rows their pool
         *  slots and the analog rows their wire cells.
         *  @param order row indices in name order, @p count of them; null for a
         *         table with none, which find() then walks */
        void init(const Descriptor* rows, const uint16_t* order, uint16_t count,
                  uint16_t param_first, uint16_t param_count);

        /** @name The rows
         *  @{ */

        uint16_t count() const { return _count; }

        /** @brief The descriptor for row @p i, or the SINK's for an index that
         *  is not one - VAR_NONE included, which is what an unresolved
         *  reference carries. */
        const Descriptor& descriptor(uint16_t i) const {
            return (i < _count) ? _rows[i] : sink_descriptor;
        }

        /** @brief Row @p i's slot, or the sink's - which is cleared here, so a
         *  write to a bad index is discarded rather than remembered. */
        Slot& slot(uint16_t i) const {
            if (i < _count) return _slots[i];
            _sink = Slot();
            return _sink;
        }

        /** @brief Resolve an index. Never fails; a bad one lands on the sink. */
        Var var(uint16_t i) const;

        /** @brief Find a variable by name, case-insensitively; VAR_NONE when
         *  absent. Binary search over the generated name order. The CONSOLE's
         *  route - everything else indexes through a `Row::` constant. */
        uint16_t find(const char* name) const;
        /** @brief Length-bounded form, for a shell argument. */
        uint16_t find(const char* name, uint16_t len) const;

        /** @brief Parameter @p k, 0-based over the K_PARAM block. */
        Var      param_at(uint16_t k) const;
        uint16_t param_count() const { return _param_count; }

        /** @} */
        /** @name Pools
         *  @{ */

        char*   pool() const { return _pool; }
        Scalar& wire_at(uint8_t off) const { return _wire[off]; }

        /** @brief Where the audit line a write produces goes; may be null. An
         *  edit and a force are things a person did, and the record of them
         *  belongs beside the write rather than in each caller. */
        Logs::Logger* log() const { return _logger; }

        /** @} */
        /** @name Tallies the pages show
         *  @{ */

        /** @brief Parameters edited but not yet saved - what `params save` with
         *  no name would write, and what a display marks. */
        uint16_t unsaved_count() const;
        /** @brief Parameters a program has written and the cadence has not yet
         *  committed. Not counted above: the params page marks what a PERSON
         *  typed, and a watering date is not that. */
        uint16_t owed_count() const;
        /** @brief Overrides in force but not yet saved. Different fact,
         *  different key. */
        uint16_t unsaved_override_count() const;

        /** @} */

        /** @brief Drop every K_INPUT whose last good read is older than its
         *  declared timeout to Q_NONE. A timeout of 0 never expires, for a value
         *  a driver publishes only on change. */
        void expire_stale(uint64_t now_ms);

    private:
        static void _expire_task(Component* c, const Tasks::Tick& t);
        /** @brief The one lookup both find() overloads run; @p len 0 means
         *  NUL-terminated. @return the row index, or VAR_NONE. */
        uint16_t _lookup(const char* name, uint16_t len) const;
        /** @brief How many slots carry @p bit. */
        /** @brief Slots in [@p from, @p from + @p n) carrying @p bit. */
        uint16_t _tally(uint8_t bit, uint16_t from, uint16_t n) const;
    };


    /** @brief One row: the bank and its index, and every operation a row has.
     *
     *  Four bytes on the M7, made on the fly and passed by value. The companion
     *  is reached only through its row - `h.quality_var()` - so nothing outside
     *  this class names the `_qlt` suffix. */
    class Var {
        const Bank* _b;
        uint16_t    _i;

        /** @brief The slot behind this handle, or the no-bank sink - cleared on
         *  every access, so a write to a handle that points at nothing is
         *  discarded rather than remembered. */
        Slot& _s() const;

    public:
        /** @brief A handle with no bank: the sink, and what a default-built one
         *  is. Reads zeros with quality Q_NONE and drives nothing. */
        Var() : _b(nullptr), _i(VAR_NONE) {}
        Var(const Bank* b, uint16_t i) : _b(b), _i(i) {}

        /** @name Identity
         *  @{ */

        /** @brief This row's index, or VAR_NONE for the sink. */
        uint16_t row() const { return null() ? VAR_NONE : _i; }
        /** @brief Whether this handle points at the sink rather than at a row. */
        bool null() const { return _b == nullptr || _i >= _b->count(); }
        const Bank* bank() const { return _b; }

        const Descriptor& meta() const {
            return (_b != nullptr) ? _b->descriptor(_i) : sink_descriptor;
        }
        const char* name() const        { return meta().name; }
        const char* unit() const        { return meta().unit; }
        const char* description() const { return meta().desc; }
        const char* notes() const       { return meta().notes; }
        Kind        kind() const        { return meta().kind; }
        VarType     type() const        { return meta().type; }

        /** @} */
        /** @name Read
         *  Three layers and their resolution, each a Value. Quality is not one
         *  of them: it is a row, and quality() is the way to it. @{ */

        /** @brief What the row would read with nothing forced. The value SLOT,
         *  whatever it is worth: an implausible reading shows the number that
         *  was implausible, and quality says so beside it. Substituting the
         *  declared value is Ref::get()'s job, once, for the laws. */
        Value live() const {
            const Descriptor& d = meta();
            return Value{ _s().v, &d, d.type };
        }

        /** @brief The row's DECLARED value, as a Value ready to convert. What a
         *  law reads in place of a row with nothing behind it: a number somebody
         *  chose and reviewed, rather than a reading of unbounded age. */
        Value declared() const { return Value{ meta().def, &meta(), type() }; }

        /** @brief The value in force: the force when one is on, else live. The
         *  forced number has no accessor of its own - ask override_on() first
         *  and this IS it, so an unforced row's stale `ovr` field cannot be
         *  read at all. */
        Value value() const {
            const Slot& s = _s();
            const Descriptor& d = meta();
            return Value{ (s.flags & S_OVR_ON) ? s.ovr : s.v, &d, d.type };
        }

        /** @brief The count on the wire, before any conversion. Not PRESENT for
         *  a row with no binding - a parameter has no wire. For a 1:1 row the
         *  wire IS the value; for an analog row it is what arrived, or what
         *  would be sent. */
        Value wire() const;

        template <typename T> T as() const { return value().as<T>(); }
        float   as_f32() const { return value().as_f32(); }
        bool    as_bool() const { return value().as_bool(); }
        int64_t as_i64() const { return value().as_i64(); }

        /** @brief Where this row's text sits in the pool (T_STR only), or null.
         *  The ONLY route to a row's text: the declared capacity is in flash and
         *  says nothing about whether the row got a slot. */
        char* text(uint16_t* cap_out = nullptr) const;

        /** @} */
        /** @name Quality
         *  @{ */

        /** @brief This row's companion, or the sink when it has none. The one
         *  route to it: forcing a quality is `h.quality_var().override_set(...)`. */
        Var quality_var() const { return Var(_b, quality_row()); }
        /** @brief The companion's index, or VAR_NONE. Adjacency IS the link. */
        uint16_t quality_row() const {
            return (!null() && has_quality(meta())) ? (uint16_t)(_i + 1) : VAR_NONE;
        }

        /** @brief WHY this row reads as it does: its companion's value, or
         *  Q_GOOD when it has none - a row with no companion is good, always.
         *  The companion's own force applies, which is the whole of "make this
         *  read like a dead sensor". */
        Quality quality() const;

        bool usable() const   { return Vars::usable(quality()); }
        bool measured() const { return Vars::measured(quality()); }
        /** @brief Whether something MEASURED the row. What a law gating on
         *  seeing the plant asks. */
        bool valid() const    { return measured(); }

        /** @brief Say what the live value is worth. A row with no companion has
         *  no quality to write, so this silently does nothing there -
         *  deliberately: a driver should not have to ask whether the site
         *  declared one. */
        void set_quality(Quality q) const;

        /** @} */
        /** @name Age
         *  @{ */

        /** @brief Uptime ms at the last good update, low 32 bits. Meaningless
         *  until written(). */
        uint32_t stamp() const { return _s().stamp_ms; }
        /** @brief Whether anything has ever written this slot. What the stamp's
         *  zero cannot say. */
        bool written() const { return _flag(S_WRITTEN); }
        /** @brief Milliseconds since the last write, by unsigned subtraction, so
         *  the 49-day wrap costs nothing. 0 when never written. */
        uint32_t age_ms(uint64_t now_ms) const {
            return written() ? (uint32_t)((uint32_t)now_ms - _s().stamp_ms) : 0;
        }

        /** @} */
        /** @name Bounds
         *  @{ */

        /** @brief This row's band with both ends RESOLVED - an end that names a
         *  row reads that row's value NOW, so a limit tracks the parameter
         *  holding it. An analog row's band is its calibration's.
         *  @warning Everything that displays or checks a band comes through
         *  here: the declared band shows the firmware default rather than the
         *  parameter the operator just edited. */
        Band band() const;
        /** @brief Whether band() means anything. */
        /** @brief This row's calibration, resolved now; all zeros for a row with
         *  none, so a caller that forgot to check gets an inert conversion. */
        AnalogValues cal() const;

        /** @brief Is @p v a believable reading? Non-finite first (a NaN is in no
         *  range, and calling that "out of band" would send an operator to a
         *  limit that is fine), then the live band. */
        bool implausible(const Scalar& v) const;

        /** @} */
        /** @name Driver writes
         *  @{ */

        /** @brief Value, timestamp and quality in one step, so the staleness
         *  clock cannot be forgotten. Judges the band here, where the reading
         *  arrives - a fresh value can still be nonsense, and a dead loop
         *  reading full scale is what no timeout catches. Does NOT mark the
         *  field dirty: this is a reading coming IN. */
        void publish(Scalar v, uint64_t now_ms) const;

        /** @brief What a driver hands over for an ANALOG row: the count off the
         *  wire. The calibration is applied here, the band judged on the
         *  engineering value, and the count kept for wire(). A row with no
         *  calibration is 1:1 and this is publish(). */
        void publish_wire(Scalar raw, uint64_t now_ms) const;

        /** @brief The quantity, without claiming anything MEASURED it. For a
         *  simulated field: stamping Q_GOOD would keep the row good for ever and
         *  no timeout could catch a device going quiet. */
        void set_physical(Scalar v) const { _s().v = v; _touch(); }

        /** @brief Drop to the declared safe state, with nothing behind it. A
         *  VALUE write, so every reader sees the number the site chose rather
         *  than a code they each have to interpret. */
        void safe() const { _drive(meta().def); set_quality(Q_NONE); }

        /** @} */
        /** @name Law writes
         *  Overloads rather than a template, so set(false) and set(hz(...)) both
         *  resolve. An integer literal is ambiguous; use set_i(). @{ */

        void set(bool v) const      { _drive(from_bool(type(), v)); }
        void set(float v) const     { _drive(from_f32 (type(), v)); }
        void set_i(int64_t v) const { _drive(from_i64 (type(), v)); }

        /** @} */
        /** @name Parameter edits, RAM side
         *  Making one durable is Persist's. @{ */

        /** @brief Apply @p text to this parameter, live but unsaved. A
         *  non-parameter is SET_INVALID - use override_set() for those. */
        SetResult param_set(const char* text) const;
        /** @brief Apply an ALREADY-TYPED value.
         *  @warning @p v must have been built for this row's type - a Scalar
         *  carries no tag saying which arm is live, so one built for another
         *  reads back as a bit pattern. The two overloads below cannot get it
         *  wrong; an integer literal is ambiguous between them, so use
         *  param_set_i(). */
        SetResult param_set(Scalar v) const;
        SetResult param_set(bool v) const  { return param_set(from_bool(type(), v)); }
        SetResult param_set(float v) const { return param_set(from_f32(type(), v)); }
        SetResult param_set_i(int64_t v) const { return param_set(from_i64(type(), v)); }
        /** @brief Whether @p v WOULD be accepted, writing nothing. For a caller
         *  committing several rows as one unit (a multi-register write). */
        SetResult param_check(Scalar v) const;
        /** @brief The same question for TEXT, so a channel that assembles a
         *  value from elsewhere validates it the way the console does. */
        SetResult param_check(const char* text) const;

        /** @brief Reclassify this row's outstanding write as a PROGRAM's, owed
         *  to the retain cadence rather than to the operator. */
        void param_owe() const;
        /** @brief Take the unsaved and owed marks off; what Persist calls once a
         *  write has landed. */
        void param_saved() const { _clear((uint8_t)(S_PARAM_UNSAVED | S_PARAM_OWED)); }

        bool param_unsaved() const { return _flag(S_PARAM_UNSAVED); }
        bool param_owed() const    { return _flag(S_PARAM_OWED); }

        /** @} */
        /** @name Override, RAM side
         *  Releasing the STORED copy is Persist's. @{ */

        /** @brief Force this row to @p text until override_clear(). T_STR is
         *  refused; everything else, an output included, is allowed - staging a
         *  hand-drive is a real workflow. */
        SetResult override_set(const char* text) const;
        /** @brief The same, with the value already typed. Also forces the
         *  companion to Q_GOOD: a forced row is one somebody stands behind. */
        void override_set(Scalar v) const;
        /** @brief Release the force; the live value takes over. Releases the
         *  companion's too. */
        void override_clear() const;

        /** @brief This retained row has moved since the store last took it. */
        bool retain_owed() const      { return _flag(S_RETAIN_OWED); }
        /** @brief The store has it. */
        void retain_saved() const     { _clear(S_RETAIN_OWED); }
        bool override_on() const      { return _flag(S_OVR_ON); }
        bool override_unsaved() const { return _flag(S_OVR_UNSAVED); }
        /** @brief Mark the force durable; what Persist calls once it lands. */
        void override_saved() const { _clear(S_OVR_UNSAVED); }
        void override_unsave() const { _set(S_OVR_UNSAVED); }

        /** @} */
        /** @name Field synchronisation
         *  Whether this output's demand still has to reach the wire. The driver
         *  owns both: it collects what is dirty and clears the mark once a
         *  transaction has carried it. @{ */

        bool field_dirty() const { return _flag(S_FIELD_DIRTY); }
        void clear_field_dirty() const { _clear(S_FIELD_DIRTY); }
        /** @brief Owe this demand to the field again without touching the demand
         *  itself, for a slave that dropped off the bus and came back through
         *  its own power-on defaults. */
        void mark_field_dirty() const { _touch_field(); }

        /** @} */

    private:
        bool _flag(uint8_t b) const { return (_s().flags & b) != 0; }
        void _set(uint8_t b) const   { Slot& s = _s(); s.flags = (uint8_t)(s.flags | b); }
        void _clear(uint8_t b) const { Slot& s = _s(); s.flags = (uint8_t)(s.flags & ~b); }

        /** @brief Take a new demand, owing it to the field only if it MOVED: a
         *  control law re-states its whole demand every activation, and dirtying
         *  each one starves the reads behind them.
         *  @note Does NOT touch quality. A row whose quality a law authors is
         *  written twice, and making a value write claim Q_GOOD would mean the
         *  two had to be in the right ORDER inside the body. */
        void _drive(Scalar v) const {
            Slot& s = _s();
            const bool moved = (s.v.u != v.u);
            s.v = v;
            // Through the reference, not _touch()/_touch_field(): _s() is out of
            // line, and this runs once per ST assignment per scan.
            uint8_t f = (uint8_t)(s.flags | S_WRITTEN);
            if (moved) {
                const Descriptor& d = meta();
                if (d.kind == K_OUTPUT)  f = (uint8_t)(f | S_FIELD_DIRTY);
                else if (retained(d))    f = (uint8_t)(f | S_RETAIN_OWED);
            }
            s.flags = f;
        }
        /** @brief Note that the slot has been written. */
        void _touch() const { _set(S_WRITTEN); }
        void _touch_field() const { if (kind() == K_OUTPUT) _set(S_FIELD_DIRTY); }
        /** @brief One end of a band or a calibration, resolved. */
        Scalar _tunable(Scalar fixed, uint16_t row) const;
    };


    inline Var Bank::var(uint16_t i) const { return Var(this, i); }
    inline Var Bank::param_at(uint16_t k) const {
        return var((uint16_t)(_param_first + k));
    }


    /** @name Typed row access
     *  One specialisation per type a Ref may carry. @{ */

    template <typename T> struct RefIo;

    template <> struct RefIo<bool> {
        static bool read(Var h)          { return h.as_bool(); }
        static bool of(Value v)          { return v.as_bool(); }
        static void write(Var h, bool v) { h.set(v); }
        static SetResult edit(Var h, bool v) { return h.param_set(from_bool(h.type(), v)); }
    };

    template <> struct RefIo<float> {
        static float read(Var h)          { return h.as_f32(); }
        static float of(Value v)          { return v.as_f32(); }
        static void  write(Var h, float v) { h.set(v); }
        static SetResult edit(Var h, float v) { return h.param_set(from_f32(h.type(), v)); }
    };

    // The eight widths now differ in nothing but the width itself.
    #define BOTANIK_REF_WHOLE(TYPE)                                            \
        template <> struct RefIo<TYPE> {                                       \
            static TYPE read(Var h)          { return h.value().as<TYPE>(); }  \
            static TYPE of(Value v)          { return v.as<TYPE>(); }          \
            static void write(Var h, TYPE v) { h.set_i((int64_t)v); }          \
            static SetResult edit(Var h, TYPE v)                               \
                { return h.param_set(from_i64(h.type(), (int64_t)v)); }        \
        }

    BOTANIK_REF_WHOLE(int8_t);
    BOTANIK_REF_WHOLE(int16_t);
    BOTANIK_REF_WHOLE(int32_t);
    BOTANIK_REF_WHOLE(int64_t);
    BOTANIK_REF_WHOLE(uint8_t);
    BOTANIK_REF_WHOLE(uint16_t);
    BOTANIK_REF_WHOLE(uint32_t);
    BOTANIK_REF_WHOLE(uint64_t);

    #undef BOTANIK_REF_WHOLE

    /** @brief A reference to a `_qlt` row - the quality of the row before it. */
    template <> struct RefIo<Quality> {
        static Quality read(Var h)          { return (Quality)h.as<uint8_t>(); }
        static Quality of(Value v)          { return (Quality)v.as<uint8_t>(); }
        static void    write(Var h, Quality q) { h.set_i((int64_t)q); }
    };

    /** @} */

    // Ref's bodies, out of line because a reference is declared before the table
    // it reads. Every one tolerates a null bank: a build with no table still
    // compiles the laws.

    template <typename T>
    inline T Ref<T>::get() const {
        if (runtime_bank == nullptr) return T();
        const Var h = runtime_bank->var(i);
        return h.usable() ? RefIo<T>::read(h) : RefIo<T>::of(h.declared());
    }

    template <typename T>
    inline bool Ref<T>::valid() const {
        return runtime_bank != nullptr && runtime_bank->var(i).measured();
    }

    template <typename T>
    inline void Ref<T>::drive(T v) const {
        if (runtime_bank == nullptr) return;
        // ST has ONE write macro, so the ROW is what decides which write it
        // means: a parameter is state the plant has to keep - a watering date, a
        // spent one-shot force - anything else is this scan's answer.
        const Var h = runtime_bank->var(i);
        if (h.kind() != K_PARAM) { RefIo<T>::write(h, v); return; }
        // OWED, not written through: a law stamps a date inside a scan, and an
        // erase there is both a stall and, repeated, a dead part.
        if (RefIo<T>::edit(h, v) == SET_OK) h.param_owe();
    }

    /** @brief A quality is this scan's answer whatever kind of row carries it,
     *  so it never takes drive()'s parameter path - owing a diagnosis to flash
     *  is not a thing a law can mean. */
    template <> inline void Ref<Quality>::drive(Quality q) const {
        if (runtime_bank != nullptr) RefIo<Quality>::write(runtime_bank->var(i), q);
    }


}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
