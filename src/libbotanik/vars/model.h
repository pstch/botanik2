#ifndef VARS_MODEL_H
#define VARS_MODEL_H

/**
 * @file model.h
 * @brief The RAM half of the variable model: the value slot, the handle over
 *        (descriptor, slot), and the Bank that owns the table.
 * @note The const half is descriptor.h, included here, so a consumer needs only
 *       this one header. See docs/dev/vars.md.
 */

#include <cstdint>

#include "libbotanik/config/vars.h"
#include "libbotanik/core/component.h"
#include "libbotanik/vars/descriptor.h"
#include "libbotanik/vars/ref.h"
#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Tasks { struct Tick; } }

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

    /** @} */

    /** @name Handle
     *  @{ */

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

        /** @brief The force, or Q_NONE when there is none. Recorded, not
         *  necessarily driving: CONTROL_DISABLED MUTES an override without
         *  erasing it, and a muted one is still there and still worth showing. */
        Value override() const {
            if (!_v->ovr_on) return Value{ _d->def, _d, _d->type, Q_NONE };
            return Value{ _v->ovr, _d, _d->type, _v->ovr_valid ? Q_GOOD : Q_FORCED_BAD };
        }

        /** @brief The value in force: the override when one is DRIVING, else
         *  live. The one place an override is applied, and the only place the
         *  mute is consulted - nothing outside has to know about it. */
        Value value() const { return _forced() ? override() : live(); }

        /** @name Shortcuts
         *  value() in one type. A digital reads 0/1, so no consumer branches.
         *  @{ */
        float    as_f32() const { return value().as_f32(); }
        bool     b()      const { return value().as_bool(); }
        int64_t  i()      const { return value().as_i64(); }
        bool     valid()  const { return value().good(); }
        /** @} */
        /** @} */
        /** @brief Where this row's text sits in the pool (T_STR only; 0
         *  otherwise, and text_slot().cap is 0 there so nothing writes). */
        uint16_t str_off() const { return _v->str_off; }
        uint64_t stamp() const { return _v->stamp_ms; }
        /** @} */

        /** @name Override (maintenance)
         *  See Slot's override note. Writes keep landing in the underlying
         *  slot while one is in force, so clearing it resumes the live value.
         *  @{ */

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
         *  muted. CONTROL_DISABLED mutes an OUTPUT's without erasing it; other
         *  roles are unaffected. */
        bool _forced() const {
            return _v->ovr_on && (_v->vflags & VF_OVR_MUTED) == 0;
        }

        /** @brief Note that a ROLE_OUTPUT's demand has moved. One compare on a
         *  path that is already writing; every other role ignores it. */
        void _touch_field() {
            if (_d->role == ROLE_OUTPUT) _v->vflags = (uint8_t)(_v->vflags | VF_FIELD_DIRTY);
        }
    };

    /** @} */

    /** @name Table
     *  @{ */

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

    /** @brief What everything is handed to reach the variables.
     *  @note A bad index - VAR_NONE included - resolves to an inert sink, never
     *  CLAMPING: clamping would alias it onto a real row, so a typo could drive
     *  an actual relay.
     *
     *  ## Naming
     *
     *  Three shapes, and which one a new method takes is not a matter of taste:
     *
     *  - `<noun>_<verb>` for a FAMILY of operations on one concept, so the
     *    family sorts together and reads the way the shell says it:
     *    `override_set` / `override_clear` / `override_clear_all`,
     *    matching `overrides set` / `overrides clear`.
     *  - `set_<property>` for a single property with no family behind it:
     *    `set_store`, `set_control_mode`, `set_health_hold`. Turning these
     *    around would give `health_hold_set`, which is why they are not the
     *    same shape as the group above.
     *  - `<thing>_count()` for a tally, always, and never `num_` or `n_`:
     *    `param_count`, `unsaved_count`, `refused_count`,
     *    `unresolved_count`, `param_stray_count`.
     *
     *  `set()` alone means EDIT A PARAMETER. Anything that writes a row by
     *  another route says so in its name (`override_set`, `publish` on Var), so
     *  that a caller cannot reach for `set` and get a different rule. */
    class Bank : public Botanik::Component {
    public:
        /** @brief The two sweeps this class owns, both GROUP_BACKGROUND in
         *  PHASE_INPUT: a row must age out BEFORE anything reads it, so one beat
         *  is coherent. */
        void register_tasks() override;

    protected:
        /** @brief Load the stored image: parameters, overrides, retained. */
        void on_setup(uint64_t now_ms) override;
        /** @brief Write the retained rows out, at most once per
         *  `sys_retain_sec`. */
        void on_config(uint64_t now_ms) override;

    public:

    private:
        /** @name Registration handlers
         *  Static MEMBERS: each cast sits on one line beside what it calls.
         *  @{ */
        static void _expire_task(Component* c, const Tasks::Tick& t);
        static void _config_task(Component* c, const Tasks::Tick& t);
        /** @} */

        /** @name The rows
         *  Borrowed, never owned: each segment is somebody's flash-resident
         *  array. One flat index space runs across them in registration order,
         *  so a VarRef stays a bare row number and nothing outside knows there
         *  are segments. @{ */

        const Descriptor* _seg[Config::VAR_SEGMENTS];
        /** @brief First global row number of each segment; ascending. */
        uint16_t        _seg_base[Config::VAR_SEGMENTS];
        uint16_t        _seg_len[Config::VAR_SEGMENTS];
        uint8_t         _segments = 0;
        uint16_t        _count    = 0;    ///< rows across every segment
        /** @brief Rows a register_vars() call had no room for: Config::VAR_MAX
         *  is too small, and every name in them resolves to the sink. */
        uint16_t        _refused  = 0;
        /** @brief ROLE_PARAM rows outside the contiguous block - the segments
         *  were registered in the wrong order. Each named as a critical at
         *  freeze(); the POST fails on the count. */
        uint16_t        _param_stray = 0;

        /** @}
         *  @name The bank's own storage
         *  One slot per row, the name index and the string pool - all sized by
         *  config, which is why a site declares no arrays. @{ */

        /** @brief Mutable so var() and pool() stay const: `const Bank&` gates the
         *  BANK's API, not what a Var may drive. */
        mutable Slot _slots[Config::VAR_MAX];
        mutable char  _pool[Config::STR_POOL_BYTES];
        uint16_t      _index[Config::VAR_MAX];

        /** @brief Logical row -> the segment row it reads, built at freeze().
         *  Identity unless a name was REDECLARED: a site may restate a library
         *  row to change any of its attributes, and the later declaration wins.
         *  The shadowed row is compacted out here rather than flagged, so every
         *  reader - find(), the pages, telemetry, the alarm scan, the register
         *  map - simply never sees it, with no skip test to add and none to
         *  forget. @see _resolve_shadows() */
        uint16_t      _row[Config::VAR_MAX];
        uint64_t      _last_retain_ms = 0;   ///< last retained write; 0 = none
        uint8_t       _writer_now = WRITER_NONE;  ///< whose activation is open

        /** @brief One AnalogCal block and the rows its six tunables name.
         *  Cached as INDICES, not values: the index is fixed once the table is
         *  frozen, while the value must stay live so a recalibration lands the
         *  moment the parameter commits. Nothing to invalidate. */
        /** @brief The count on the wire for each AI/AO row, indexed by the
         *  slot's wire_off. Only those rows need one: every other row's wire
         *  value IS its value. */
        Scalar        _wire[Config::ANALOG_ROWS_MAX];
        uint8_t       _wire_count = 0;

        struct CalRefs { const AnalogCal* block; VarRef ref[6]; };
        CalRefs       _cal[Config::ANALOG_BLOCKS_MAX];
        uint8_t       _cal_count = 0;
        /** @brief Rows dropped as redeclared. Kept because it is what tells
         *  _row_of() whether the logical and physical spaces have parted; each
         *  one is also NAMED at freeze(), since a duplicate name used to be
         *  impossible and is now a feature, so an accidental collision would
         *  otherwise delete a row in silence. */
        uint16_t      _shadowed = 0;

        /** @}
         *  @name Parameter block
         *  The ROLE_PARAM rows MUST be one contiguous run, which is what lets
         *  the pages address them with a dense 0-based index. Found by scanning
         *  the table, never declared. @{ */
        uint16_t      _param_first = 0;
        uint16_t      _param_count = 0;
        /** @} */
        /** @brief Where ROLE_PARAM writes are persisted (null -> RAM only, and
         *  set() reports SET_NOT_PERSISTED). */
        VarStore*     _store  = nullptr;
        /** @brief What is IN FORCE, pushed in by Plc::ControlAuthority - which
         *  owns the two sources behind it. Starts DISABLED, for the same reason
         *  the system's hold starts raised: outputs are not ours to move until
         *  setup() has finished. */
        ControlMode   _mode = CONTROL_DISABLED;
        /** @brief Name lookups that found nothing: non-zero after bring-up means
         *  something names a row that is not in vars.h. Mutable, being
         *  diagnostics behind a const find(). */
        mutable uint16_t _unresolved = 0;
        /** @brief Rows written by more than one program, and the first such row.
         *  @see claim_writer() */
        uint16_t _conflicts   = 0;
        VarRef   _conflict_row = { VAR_NONE };
        /** @brief Whether the name index holds a usable sorted mapping. False
         *  until freeze(), and find() then walks the rows.
         *  @see _build_index() */
        bool          _indexed = false;
        /** @name String pool
         *  Filled by init(), which lays the T_STR slots out in table order.
         *  @{ */
        uint16_t      _pool_used     = 0;
        /** @brief Text rows that did not fit: the site's pool is too small, and
         *  those rows read empty and refuse every write. */
        uint16_t      _pool_overflow = 0;
        /** @} */
    public:
        /** @brief An EMPTY bank. Rows arrive through register_vars(), and
         *  freeze() is what makes them addressable. */
        Bank(const SetupContext& ctx = SetupContext()) : Component(ctx, "vars") {}

        /** @name Registration
         *  @{ */

        /** @brief Add a segment of flash rows, borrowed for the bank's lifetime.
         *  Rows land in call order, so REGISTRATION ORDER IS TABLE ORDER.
         *  @return false if it did not fit, in which case NONE of it was added. */
        bool register_vars(const Descriptor* rows, uint16_t count);

        /** @brief Close registration: lay out the string pool, seed the slots,
         *  build the name index, locate the parameter block. Nothing may resolve
         *  a name before this, nor register after it. */
        void freeze();

        /** @brief Rows dropped for want of room. Zero on a healthy site; the
         *  POST fails otherwise. */
        uint16_t refused_count() const { return _refused; }

        /** @brief Parameters left outside the parameter block by a bad
         *  registration order. Zero on a healthy site. @see freeze() */
        uint16_t param_stray_count() const { return _param_stray; }

        /** @} */

        /** @brief Bind the parameter store and the log a refused write goes to.
         *  Wired once at bring-up, before load_params(); the logger may be null. */
        void set_store(VarStore* s, Logs::Logger* logger = nullptr) {
            _store = s; _logger = logger;
        }

        /** @brief Seed every slot from its declared default, copy string
         *  defaults into the pool, set the initial validity per role, and clear
         *  every override. */
        void init();

        /** @brief Load every ROLE_PARAM from the store, over the defaults
         *  init() seeded. A missing key is normal (the default stands). */
        void load_params();

        /** @brief Restore the overrides in force at the last shutdown - every
         *  role, ROLE_OUTPUT included.
         *  @warning A restored output override is DRIVING the plant from this
         *  moment, so each restore is a CRITICAL naming the row.
         *  @return how many were restored. */
        uint16_t load_overrides();

        /** @name Retained values (F_RETAIN)
         *
         *  The rows whose value IS the history - litres delivered, hours run,
         *  the day a tray was last watered.
         *  @{ */

        /** @brief Whether @p d goes to the retained store: `F_RETAIN` on
         *  `ROLE_COMPUTED` only. A parameter already persists, an input is
         *  overwritten by the next scan, and restoring an output would energise
         *  a contactor before anything looked at the plant. */
        static bool retainable(const Descriptor& d) {
            return (d.flags & F_RETAIN) != 0 && d.role == ROLE_COMPUTED;
        }

        /** @brief The flag's other half: `F_RETAIN` on a `ROLE_PARAM` row,
         *  which persists through its own key already. Such a row is editable
         *  and saved like any parameter, and param_reset() refuses it. */
        static bool param_retained(const Descriptor& d) {
            return (d.flags & F_RETAIN) != 0 && d.role == ROLE_PARAM;
        }

        /** @brief Restore every retained row from the store. Called at bring-up
         *  after load_params(). @return how many were restored. */
        uint16_t load_retained();
        /** @brief Write every retained row out. Called on a cadence and on the
         *  way down - never per write, which would be a flash erase per beat.
         *  @return how many were written. */
        uint16_t save_retained();

        /** @brief Write the retained rows NOW and restart the interval. The
         *  cadence bounds what an unexpected cut costs; this is for a stop that
         *  is not unexpected. */
        uint16_t save_retained(uint64_t now_ms) {
            _last_retain_ms = now_ms;
            return save_retained();
        }

        /** @} */

        char*            pool() const   { return _pool; }
        uint16_t         count() const  { return _count; }

        /** @brief The descriptor for LOGICAL row @p i, already bounds-checked.
         *  One hop through the redeclaration map, then the segment walk. */
        const Descriptor& descriptor(uint16_t i) const { return _desc_phys(_row[i]); }

        /** @brief Resolve an index. Never fails; a bad one lands on the sink. */
        Var var(uint16_t i) const {
            if (i < _count) return Var(&descriptor(i), &_slots[i]);
            _sink_v = Slot();          // fresh: a sink never accumulates state
            return Var(&_sink_d, &_sink_v);
        }
        Var var(VarRef r) const { return var(r.index); }
        /** @brief Resolve by name: how site tables and programs address a row.
         *  An unknown name lands on the sink and bumps the unresolved count. */
        Var var(const char* name) const { return var(find(name)); }

        /** @brief Parameter @p k, 0-based over the ROLE_PARAM block. */
        Var      param_at(uint16_t k) const { return var((uint16_t)(_param_first + k)); }
        uint16_t param_count() const     { return _param_count; }
        /** @brief Table index of the first ROLE_PARAM row (0 when there are
         *  none). */

        /** @brief Where @p h's text lives in the pool, or null. The ONLY route to
         *  a row's text: the declared capacity is in flash and says nothing about
         *  whether the row got a slot. @p cap_out takes the usable capacity. */
        char* text(Var h, uint16_t* cap_out = nullptr) const;

        /** @brief Find a variable by name; VAR_NONE when absent, which resolves to
         *  the sink. Binary search over _index. */
        VarRef find(const char* name) const;
        /** @brief Length-bounded form, for a shell argument. Operator input, so a
         *  miss is a typo rather than a defect: it does NOT count. */
        VarRef find(const char* name, uint16_t len) const;

        /** @brief Name lookups that found nothing. Zero on a correctly wired
         *  site; the test suite asserts it. */
        /** @brief What a DRIVER hands over for an analog row: the count off the
         *  wire. The model applies the calibration, judges the plausibility
         *  band, keeps the count for wire() and stores the engineering value
         *  for live(). A row with no calibration is 1:1, and this is then the
         *  same as publish_live(). */
        void publish_wire(VarRef r, Scalar raw, uint64_t now_ms);

        /** @brief The count on the wire, before any scaling. Q_NONE for a row
         *  with no binding at all - a parameter has no wire. For a 1:1 row the
         *  wire IS the value. For an analog INPUT it is what arrived; for an
         *  analog OUTPUT, what would be sent - nothing publishes a count
         *  inward for one. */
        Value wire(VarRef r) const;

        /** @brief The six rows @p c's tunables name, or null when the block is
         *  unknown (a table past ANALOG_BLOCKS_MAX, or a bank never frozen).
         *  A fixed tunable has VAR_NONE. */
        const VarRef* analog_refs(const AnalogCal* c) const {
            for (uint8_t k = 0; k < _cal_count; k++)
                if (_cal[k].block == c) return _cal[k].ref;
            return nullptr;
        }

        uint16_t unresolved_count() const { return _unresolved; }

        /** @name Write attribution
         *  Who among the control programs drives a row, recorded by drive()
         *  from whoever the dispatcher's window names. @{ */

        /** @brief Record @p who as the writer of @p r; false when it already
         *  belonged to another. First claim wins, and the write still goes
         *  through: refusing it would leave the plant where the loser left it. */
        bool claim_writer(VarRef r, uint8_t who);

        /** @name Writer window
         *
         *  Whose writes count as theirs. The DISPATCHER opens the window around
         *  one program's activation and closes it after, so a program cannot
         *  name itself and a write outside any activation claims nothing.
         *
         *  Ambient state, and safe only because the beat is synchronous and
         *  single-threaded: under preemption this would need to move onto the
         *  writer.
         *  @{ */
        void begin_writer(uint8_t who) { _writer_now = who; }
        void end_writer()              { _writer_now = WRITER_NONE; }
        /** @} */

        /** @name Driving
         *  What a control PROGRAM calls: claim the row for whoever the window
         *  names, then write. Distinct from param_set(), which edits a
         *  parameter against its band.
         *  @{ */
        void drive(VarRef r, bool v)      { claim_writer(r, _writer_now); var(r).set(v); }
        void drive(VarRef r, float v)     { claim_writer(r, _writer_now); var(r).set(v); }
        /** @} */

        /** @name Typed access
         *  The same index and the same accessor as the untyped pair above, with
         *  the row's type and its declaration section in the reference: reading
         *  a digital as a float, or driving a VAR_INPUT, does not compile.
         *  @{ */

        /** @brief Read @p h. Every direction reads - a POU may read its own
         *  output. */
        template <typename T, Dir D>
        T get(Ref<T, D> h) const { return RefIo<T>::read(*this, h.r); }

        /** @brief Drive @p h. @p V is CHECKED against @p T, not converted, so a
         *  mismatch names itself instead of reporting a deduction conflict.
         *  @note A VAR_IN_OUT row skips the exclusive claim. @see claim_writer() */
        template <typename T, Dir D, typename V>
        void drive(Ref<T, D> h, V v) {
            static_assert(D != Dir::IN, "a VAR_INPUT row cannot be driven");
            static_assert(SameType<T, V>::value, "value type must be the row's own type");
            claim_writer(h.r, _writer_now);
            RefIo<T>::write(*this, h.r, (T)v);
        }

        /** @} */

        /** @name Binding
         *
         *  One call per variable a program declares. A name resolving to no row,
         *  or to a row of another type, lands on the sink and counts as
         *  unresolved - which `tests/test_site.cpp` fails on.
         *
         *  Declaring a row as an OUTPUT also CLAIMS it for whoever the writer
         *  window names, which a program's bind() runs inside. So two programs
         *  producing one row is a bring-up failure rather than something noticed
         *  the first time both write.
         *  @{ */

        template <typename T> RefIn<T> resolve_in(const char* name) {
            return RefIn<T>(_resolve_typed<T>(name), this);
        }
        template <typename T> RefOut<T> resolve_out(const char* name) {
            const VarRef r = _resolve_typed<T>(name);
            claim_writer(r, _writer_now);
            return RefOut<T>(r, this);
        }
        /** @brief The same, and MARKS the row shared: a VAR_IN_OUT row is
         *  read-modify-written by more than one writer BY DECLARATION, so it
         *  records no single owner and the conflict scan leaves it alone. */
        template <typename T> RefInOut<T> resolve_inout(const char* name) {
            const VarRef r = _resolve_typed<T>(name);
            if (r.index < _count) _slots[r.index].vflags |= VF_SHARED;
            return RefInOut<T>(r, this);
        }

        /** @} */

        /** @brief The program that claimed @p r, or WRITER_NONE. */
        uint8_t writer_of(VarRef r) const;

        /** @brief How many ROWS two programs have both written, and the first.
         *  Rows, not collisions: two programs collide every beat, but the count
         *  of doubly-authored rows is fixed by the site's design. */
        uint16_t writer_conflicts() const { return _conflicts; }
        VarRef   first_conflict() const   { return _conflict_row; }

        /** @} */

        /** @brief Whether the name index is in use. False before freeze(), and
         *  every find() is then a full scan. */

        /** @brief Rows that found no pool left. Must be zero: such a row reads
         *  empty and refuses every write. */
        uint16_t pool_overflow() const { return _pool_overflow; }

        /** @name Control authority
         *  The bank ENACTS a verdict it is handed; it does not decide one. Who
         *  decided, and why, is Plc::ControlAuthority's - which is also the only
         *  caller of set_effective_control(). See docs/dev/scan.md. @{ */

        /** @brief Take @p now as the mode in force: mute or unmute the output
         *  overrides, and on the way out of ENABLED drop every output to its
         *  safe state. @p erase_overrides additionally WIPES them, stored copy
         *  included - the system's hold does that, the operator's disable does
         *  not. Idempotent when nothing moved. */
        void set_effective_control(ControlMode now, bool erase_overrides);

        /** @brief What is actually in force. */
        ControlMode control_mode() const { return _mode; }

        /** @brief Whether the field may be driven (the scan is gated on it). */
        bool control_enabled() const { return control_mode() == CONTROL_ENABLED; }

        /** @brief The effective mode as a word, for the console and the logs. */
        static const char* control_mode_name(ControlMode m) {
            return (m == CONTROL_ENABLED)   ? "enabled"
                 : (m == CONTROL_INHIBITED) ? "inhibited"
                                            : "disabled";
        }

        /** @} */

        /** @brief Invalidate ROLE_INPUTs whose last good read is older than
         *  their declared timeout. A timeout of 0 never expires, for a value a
         *  driver publishes only on change. */
        void expire_stale(uint64_t now_ms);

        /** @name Writing
         *  set() edits a PARAMETER against its declared band; override() forces
         *  ANY variable, type-checked only. NEITHER writes to flash: applying and
         *  committing are separate steps. @{ */

        /** @brief Apply @p text to parameter @p h, live but unsaved. A
         *  non-parameter is SET_INVALID - use override() for those. */
        SetResult param_set(Var h, const char* text);

        /** @brief Whether @p v WOULD be accepted, writing nothing. For a caller
         *  committing several rows as one unit (a multi-register write). */
        SetResult param_check(Var h, Scalar v) const;
        /** @brief The same question for TEXT: would this edit be accepted?
         *  Same rule as param_set(Var, const char*), which is the point - a
         *  channel that assembles a value from somewhere else (a register pair
         *  carrying a dotted quad) validates it the way the console does. */
        SetResult param_check(Var h, const char* text) const;

        /** @brief Apply a value, coerced INTO the row's own type.
         *
         *  The safe pair, and what a caller should reach for: a Scalar carries
         *  no tag saying which arm is live, so one built for the wrong type
         *  reads back as a bit pattern rather than as a number. These build it
         *  from @c h's declared type, exactly as Var::set() does.
         *
         *  @note An integer literal is ambiguous between the two; use
         *  param_set_i(). Same reason Scalar(int) is deleted. @{ */
        SetResult param_set(Var h, bool v)  { return param_set_scalar(h, from_bool(h.type(), v)); }
        SetResult param_set(Var h, float v) { return param_set_scalar(h, from_f32(h.type(), v)); }
        SetResult param_set_i(Var h, int64_t v) { return param_set_scalar(h, from_i64(h.type(), v)); }
        /** @} */

        /** @brief Apply an ALREADY-TYPED value, going round the coercion above.
         *  @warning @p v must have been built for @c h's type - `from_wire(
         *  h.type(), ...)` or a Scalar factory matching it. For a driver that
         *  has the row's type and the wire bytes; anywhere else, use the
         *  param_set() overloads, which cannot get it wrong.
         *  @note Going via text instead would apply the row's DISPLAY rounding,
         *  storing 0.083 as 0.08. T_STR is refused. */
        SetResult param_set_scalar(Var h, Scalar v);

        /** @brief Commit @p h's value and clear its unsaved mark; harmless on a
         *  durable row. @return SET_OK, or SET_NOT_PERSISTED. */
        SetResult param_save(Var h);

        /** @brief Put @p h back to its factory default, live and durably: `def`
         *  returns and the stored copy is dropped, so a reboot cannot undo it.
         *  A RETAINED parameter is refused (@see param_retained).
         *  @return SET_OK; SET_INVALID on a non-parameter or a retained one;
         *  SET_NOT_PERSISTED when the store kept it (the live value is still
         *  reset). */
        SetResult param_reset(Var h);

        /** @brief Forget @p h's UNSAVED edit: the row goes back to exactly what
         *  a reboot would give it - the stored copy, or the factory default if
         *  nothing was ever saved - and the unsaved mark clears.
         *
         *  The stored copy is left alone, which is the whole difference from
         *  param_reset(): `clear` walks back an edit you have not committed,
         *  `reset` throws away the committed one too. A row with nothing
         *  outstanding is SET_OK and untouched. */
        SetResult param_clear(Var h);

        /** @brief Commit every parameter still marked unsaved.
         *  @return how many were written; @p refused, when given, takes the
         *  count the store rejected. */
        uint16_t save_params(uint16_t* refused = nullptr);

        /** @brief Parameters edited but not yet saved - what `params save` with
         *  no name would write, and what a display marks. */
        uint16_t unsaved_count() const;

        /** @brief Overrides in force but not yet saved. Separate from
         *  unsaved_count(): different facts, different keys. */
        uint16_t unsaved_override_count() const;

        /** @name Making edits durable
         *  Nothing reaches flash on its own: an edit is LIVE at once and DURABLE
         *  when saved, so a setting that breaks the console it was typed on is
         *  undone by a power cycle. @{ */

        /** @brief Force @p h to @p text until override_clear(). T_STR is refused;
         *  everything else, ROLE_OUTPUT included, is allowed whatever control
         *  authority says - staging a hand-drive is a real workflow. */
        SetResult override_set(Var h, const char* text, bool valid = true);

        /** @brief The same, with the value already typed. Forcing only the
         *  QUALITY is `override_set(h, h.value().v, false)`, which is unsaved like
         *  any other override. */
        void override_set(Var h, Scalar v, bool valid = true);


        /** @brief Commit @p h's override so it survives a reboot.
         *  @return SET_INVALID when nothing is forced on @p h, SET_NOT_PERSISTED
         *  when the store refused, SET_OK otherwise. */
        SetResult save_override(Var h);

        /** @brief Commit every override still marked unsaved.
         *  @return how many were written; @p refused takes the count the store
         *  rejected. */
        uint16_t save_overrides(uint16_t* refused = nullptr);
        /** @brief Release @p h's override and forget the stored copy. Always via
         *  the bank: Var::override_clear() cannot reach the store. */
        void override_clear(Var h);

        /** @brief Release every override in one go. @return how many were on. */
        uint16_t clear_overrides();


        /** @} */

    private:
        /** @brief Apply @p v WITHOUT marking the row unsaved. PRIVATE: set() is
         *  its only caller. It was public for a writer pacing the flash side
         *  itself, but the Modbus persist queue never took it up, and a public
         *  set() that skips the unsaved mark is a way to lose a setting.
         *  @warning Pair every SET_OK with a save(), or the setting is lost. */
        SetResult set_deferred(Var h, Scalar v);

        /** @brief The descriptor at PHYSICAL row @p p, before the redeclaration
         *  map. The one place segments are visible; their bases ascend, so
         *  walking down from the last costs one compare in the usual case. */
        const Descriptor& _desc_phys(uint16_t p) const {
            uint8_t s = (uint8_t)(_segments - 1);
            while (s > 0 && p < _seg_base[s]) --s;
            return _seg[s][p - _seg_base[s]];
        }

        /** @brief Collect the distinct AnalogCal blocks and resolve each
         *  tunable's row, once. */
        void _resolve_cals();

        /** @brief Retain cadence in ms from `sys_retain_sec`; 0 = only on the
         *  way down and on `save`. */
        uint32_t _retain_ms() const;

        /** @brief Push `sys_log_level` into the logger, and each sink's floor
         *  from the row it names. Here because this is the object already
         *  holding both the rows and a logger - `vars/` depends on `logs/`
         *  already, since a refused write is reported through one. The logger
         *  cannot read it for itself: that would make `logs/` depend back on
         *  `vars/`, and the lowest service in the tree would stop building
         *  without the variable model. */
        void _apply_log_level();

        /** @brief Build _row, dropping every row a later one redeclares. Runs
         *  FIRST in freeze(), before init() seeds a slot for a row about to
         *  disappear. @see _row */
        void _resolve_shadows();

        /** @brief Sort _index into name order, once, at freeze(). A no-op
         *  (leaving _indexed false) when the site supplied no scratch array. */
        void _build_index();
        /** @brief Row order for the sort: by name, ties broken on row number, so
         *  a duplicated name resolves to the first of the two. */
        bool _order_before(uint16_t a, uint16_t b) const;
        /** @brief The one lookup both find() overloads run; @p len 0 means
         *  NUL-terminated. Does not count a miss - the caller's call.
         *  @return the row index, or VAR_NONE. */
        uint16_t _lookup(const char* name, uint16_t len) const;

        /** @brief find(), then reject a row @p T cannot bind to. A right name
         *  with a wrong type is still a wiring defect, so it counts as
         *  unresolved rather than reading the wrong union arm. */
        template <typename T>
        VarRef _resolve_typed(const char* name) const {
            const VarRef r = find(name);
            if (r.index >= _count) return r;
            if (RefIo<T>::accepts(descriptor(r.index).type)) return r;
            _unresolved++;
            return VarRef{ VAR_NONE };
        }

        /** @brief @p h's row index, or VAR_NONE when it points at the sink. */
        uint16_t _row_of(Var h) const;
        /** @brief The same from the descriptor: which row this address IS.
         *  VAR_NONE for the sink or another bank's row. */
        uint16_t _row_of(const Descriptor* d) const;
        /** @brief The value slot behind an editable row, or null. The one
         *  admission check set() and check() share. */
        Slot* _editable_slot(const Descriptor* d) const;
        /** @brief Write @p h's override through to the store; false when it
         *  refused (already logged) or there is no store. */
        bool _persist_override(Var h);
        /** @brief Mark @p h's override live-but-unsaved. */
        void _mark_override_unsaved(Var h);
        /** @brief Write an edited parameter through to the store, loudly when it
         *  refuses. Shared by both set() overloads. */
        SetResult _persist_param(Var h, const Descriptor& d, Slot& slot);

        /** @brief The one output sweep both control edges run. @p before tells a
         *  transition from a re-assertion; @p erase_overrides is the system's
         *  destructive edge. Outputs only. */
        void _apply_control(ControlMode before, bool erase_overrides);   // set_effective_control()'s body
        /** @brief Bring row @p i's mute bit in step with the current mode; true
         *  when it moved. ROLE_OUTPUT only. The one owner of "an output's
         *  override reaches the field iff the mode is not CONTROL_DISABLED". */
        bool _sync_override_mute(uint16_t i);

        /** @brief Locate the ROLE_PARAM run. A parameter outside it is
         *  unreachable as one, the pages addressing the block by offset. */
        void _find_param_block();

        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access: scratch, never shared state. */
        static const Descriptor _sink_d;
        static Slot          _sink_v;
    };

    /** @} */

    /** @name Typed row access
     *  One specialisation per type a Ref may carry. `accepts()` is what a
     *  binding is checked against: a whole-number Ref takes any integer row and
     *  a T_ENUM, never a T_BOOL or a T_F32 - reading one as the other is the
     *  confusion the typed reference exists to catch.
     *  @{ */

    /** @brief Whether @p t is a whole-number row. */
    inline bool ref_whole(VarType t) {
        return t != T_BOOL && t != T_F32 && t != T_STR;
    }

    template <> struct RefIo<bool> {
        static bool accepts(VarType t)             { return t == T_BOOL; }
        static bool read(const Bank& b, VarRef r)  { return b.var(r).b(); }
        static void write(Bank& b, VarRef r, bool v) { b.var(r).set(v); }
        static SetResult edit(Bank& b, VarRef r, bool v) { return b.param_set(b.var(r), v); }
    };

    template <> struct RefIo<float> {
        static bool  accepts(VarType t)              { return t == T_F32; }
        static float read(const Bank& b, VarRef r)   { return b.var(r).as_f32(); }
        static void  write(Bank& b, VarRef r, float v) { b.var(r).set(v); }
        static SetResult edit(Bank& b, VarRef r, float v) { return b.param_set(b.var(r), v); }
    };

    // The eight widths differ only in the saturating accessor they read through.
    #define BOTANIK_REF_WHOLE(TYPE, AS)                                        \
        template <> struct RefIo<TYPE> {                                       \
            static bool accepts(VarType t)            { return ref_whole(t); } \
            static TYPE read(const Bank& b, VarRef r) { return b.var(r).value().AS(); } \
            static void write(Bank& b, VarRef r, TYPE v) { b.var(r).set_i((int64_t)v); } \
            static SetResult edit(Bank& b, VarRef r, TYPE v)                   \
                { return b.param_set_i(b.var(r), (int64_t)v); }                \
        }

    BOTANIK_REF_WHOLE(int8_t,   as_i8);
    BOTANIK_REF_WHOLE(int16_t,  as_i16);
    BOTANIK_REF_WHOLE(int32_t,  as_i32);
    BOTANIK_REF_WHOLE(int64_t,  as_i64);
    BOTANIK_REF_WHOLE(uint8_t,  as_u8);
    BOTANIK_REF_WHOLE(uint16_t, as_u16);
    BOTANIK_REF_WHOLE(uint32_t, as_u32);
    BOTANIK_REF_WHOLE(uint64_t, as_u64);

    #undef BOTANIK_REF_WHOLE

    /** @} */

    /** @name Ref accessors
     *  Out of line, because a reference is declared before the table it reads.
     *  @{ */

    template <typename T, Dir D>
    inline T Ref<T, D>::get() const { return io->get(*this); }

    template <typename T, Dir D>
    inline void Ref<T, D>::set(T v, bool current) const {
        static_assert(D != Dir::IN, "a VAR_INPUT row cannot be driven");
        io->drive(*this, v);
        if (!current) io->var(r).invalidate(Q_UNCERTAIN);
    }

    template <typename T, Dir D>
    inline bool Ref<T, D>::valid() const { return io->var(r).valid(); }

    template <typename T, Dir D>
    inline bool Ref<T, D>::edit(T v) const {
        static_assert(D == Dir::INOUT, "only a VAR_IN_OUT row is edited");
        if (RefIo<T>::edit(*io, r, v) != SET_OK) return false;
        io->param_save(io->var(r));
        return true;
    }

    template <typename T, Dir D>
    inline void Ref<T, D>::invalidate() const {
        static_assert(D != Dir::IN, "a VAR_INPUT row cannot be invalidated");
        io->var(r).invalidate(Q_NO_DATA);
    }

    template <typename T, Dir D>
    inline void Ref<T, D>::hold() const {
        static_assert(D != Dir::IN, "a VAR_INPUT row cannot be held");
        io->var(r).invalidate(Q_UNCERTAIN);
    }

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
