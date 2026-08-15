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
    struct Value {
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
        uint8_t  _rsv[1];
    };

    /** @brief No program has claimed this row. Not 0, which is a real program
     *  index - the same trap VAR_NONE exists to avoid. */
    static const uint8_t WRITER_NONE = 0xFFu;
    // Every field here costs RAM on every row of every site, so the size is
    // pinned rather than left to drift.
    static_assert(sizeof(Value) == 32, "Value must stay 32 bytes (RAM per variable)");

    /** @brief A reference to any variable: a bare index into the one table.
     *
     *  A struct rather than a typedef so a stray integer cannot become a
     *  reference by accident. */
    struct VarRef { uint16_t index; };

    /** @brief The "no variable" index. Deliberately NOT 0: a zero-initialised
     *  VarRef - which is how the platform-health alarms spell "this one reads no
     *  variable" - must not alias the first real row. */
    static const uint16_t VAR_NONE = 0xFFFFu;

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
        const Variable* _d;
        Value*          _v;
    public:
        Var(const Variable* d, Value* v) : _d(d), _v(v) {}

        /** @name Read
         *  An invalid variable reads its declared default. The override is
         *  applied HERE, the one place every reader goes through. @{ */
        /** @brief What this row reads, override applied. @see _under() */
        Scalar   raw()   const { return _forced() ? _v->ovr : _under(); }
        /** @brief As a float; a digital reads 0/1, so no consumer branches on
         *  the type. */
        float    value() const { return as_f32(_d->type, raw()); }
        bool     b()     const { return as_bool(_d->type, raw()); }
        int64_t  i()     const { return as_i64(_d->type, raw()); }
        bool     valid() const { return _forced() ? _v->ovr_valid : live_valid(); }
        /** @name Under the override
         *  What the row would read with nothing forced, and its real quality.
         *  Diagnostics only - control logic reads raw()/valid(). @{ */
        Scalar   live()       const { return _under(); }
        bool     live_valid() const { return _v->qual == Q_GOOD; }
        /** @brief ...and WHY. An override does not change it. */
        Quality  live_quality() const { return _v->qual; }
        /** @} */
        /** @brief Where this row's text sits in the pool (T_STR only; 0
         *  otherwise, and text_slot().cap is 0 there so nothing writes). */
        uint16_t str_off() const { return _v->str_off; }
        uint64_t stamp() const { return _v->stamp_ms; }
        /** @} */

        /** @name Override (maintenance)
         *  See Value's override note. Writes keep landing in the underlying
         *  slot while one is in force, so clearing it resumes the live value.
         *  @{ */

        /** @brief True while this variable is forced. */
        bool overridden() const { return _v->ovr_on; }

        /** @} */

        /** @name Write
         *  Overloads rather than a template, so set(false) and set(hz(...)) both
         *  resolve. An integer literal is ambiguous; use set_i(). @{ */
        void set(bool v)      { _drive(from_bool(_d->type, v)); }
        void set(float v)     { _drive(from_f32 (_d->type, v)); }
        void set_i(int64_t v) { _drive(from_i64 (_d->type, v)); }

        /** @brief What a DRIVER calls: value, timestamp and validity in one
         *  step, so the staleness clock cannot be forgotten. Does NOT mark the
         *  field dirty - this is a reading coming IN. */
        void publish(Scalar v, uint64_t now_ms) {
            _v->v = v; _v->stamp_ms = now_ms; _v->qual = Q_GOOD;
        }
        /** @brief The quantity, without claiming anything MEASURED it. For a
         *  simulated field: stamping Q_GOOD would keep the row good for ever and
         *  no timeout could catch a device going quiet. */
        void set_physical(Scalar v) { _v->v = v; }
        /** @brief Mark the reading bad, and say why. For a driver rejecting a
         *  well-formed but unbelievable value - an analog reading outside a band
         *  Bank::reject_implausible() cannot judge, it living in the params. */
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

        const Variable* meta() const { return _d; }
        const char*     name() const { return _d->name; }
        const char*     unit() const { return _d->unit; }
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
        /** @brief Freeze at whatever it reads right now, keeping its quality. */
        void override_freeze() { override_set(raw(), valid()); }
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
         *  @note A STALE row KEEPS its value - a sensor that answered a minute
         *  ago still knows what the plant was doing. Q_NO_DATA, Q_SAFE and
         *  Q_IMPLAUSIBLE fall back. */
        Scalar _under() const {
            return (_v->qual == Q_GOOD || _v->qual == Q_STALE) ? _v->v : _d->def;
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
        virtual bool load(const Variable& d, Value& v, char* text, uint16_t cap) = 0;
        /** @brief Persist @p d's current value. */
        virtual bool save(const Variable& d, const Value& v,
                          const char* text, uint16_t cap) = 0;

        /** @brief Load @p d's stored OVERRIDE into @p v (value + the validity it
         *  reads as). @return false when none is stored, which is the normal
         *  case. */
        virtual bool load_override(const Variable& d, Value& v) = 0;
        /** @brief Persist @p d's current override. */
        virtual bool save_override(const Variable& d, const Value& v) = 0;
        /** @brief Forget @p d's stored override, on every release, so a cleared
         *  one cannot come back at the next boot. */
        virtual bool clear_override(const Variable& d) = 0;

        /** @brief Drop @p d's stored value, so it reads its factory default
         *  again after a reboot. @return false when storage refused. */
        virtual bool clear(const Variable& d) = 0;

        /** @brief Load @p d's RETAINED value (F_RETAIN); false when none is
         *  stored. Its own key, like an override: a row can have both. */
        virtual bool load_retained(const Variable& d, Value& v) = 0;
        /** @brief Persist @p d's current value as its retained one. */
        virtual bool save_retained(const Variable& d, const Value& v) = 0;
    };

    /** @brief What everything is handed to reach the variables.
     *  @note A bad index - VAR_NONE included - resolves to an inert sink, never
     *  CLAMPING: clamping would alias it onto a real row, so a typo could drive
     *  an actual relay. */
    class Bank : public Botanik::Component {
    public:
        /** @brief The two sweeps this class owns, both GROUP_BACKGROUND in
         *  PHASE_INPUT: a row must age out BEFORE anything reads it, so one beat
         *  is coherent. */
        void register_tasks() override;

    private:
        /** @name Registration handlers
         *  Static MEMBERS: each cast sits on one line beside what it calls.
         *  @{ */
        static void _expire_task(Component* c, const Tasks::Tick& t);
        static void _reject_task(Component* c, const Tasks::Tick& t);
        /** @} */

        /** @name The rows
         *  Borrowed, never owned: each segment is somebody's flash-resident
         *  array. One flat index space runs across them in registration order,
         *  so a VarRef stays a bare row number and nothing outside knows there
         *  are segments. @{ */

        const Variable* _seg[Config::VAR_SEGMENTS];
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
        mutable Value _values[Config::VAR_MAX];
        mutable char  _pool[Config::STR_POOL_BYTES];
        uint16_t      _index[Config::VAR_MAX];

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
        /** @brief The operator's standing choice. Survives a health fault
         *  coming and going, which is the point of keeping it separate. */
        ControlMode   _op_mode = CONTROL_ENABLED;
        /** @brief The system's hold: bring-up, a health fault, a reset we have
         *  decided to take. Starts SET - outputs are not ours to move yet. */
        bool          _health_hold = true;
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
        bool register_vars(const Variable* rows, uint16_t count);

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
         *  The rows whose value IS the history - litres delivered, hours run.
         *  @{ */

        /** @brief Whether @p d is retained: `F_RETAIN` on `ROLE_COMPUTED` only.
         *  A parameter already persists, an input is overwritten by the next
         *  scan, and restoring an output would energise a contactor before
         *  anything looked at the plant. */
        static bool retainable(const Variable& d) {
            return (d.flags & F_RETAIN) != 0 && d.role == ROLE_COMPUTED;
        }

        /** @brief Restore every retained row from the store. Called at bring-up
         *  after load_params(). @return how many were restored. */
        uint16_t load_retained();
        /** @brief Write every retained row out. Called on a cadence and on the
         *  way down - never per write, which would be a flash erase per beat.
         *  @return how many were written. */
        uint16_t save_retained();

        /** @} */

        char*            pool() const   { return _pool; }
        uint16_t         count() const  { return _count; }

        /** @brief The descriptor for row @p i, already bounds-checked. The one
         *  place segments are visible; their bases ascend, so walking down from
         *  the last costs one compare in the usual case. */
        const Variable& desc(uint16_t i) const {
            uint8_t s = (uint8_t)(_segments - 1);
            while (s > 0 && i < _seg_base[s]) --s;
            return _seg[s][i - _seg_base[s]];
        }

        /** @brief Resolve an index. Never fails; a bad one lands on the sink. */
        Var var(uint16_t i) const {
            if (i < _count) return Var(&desc(i), &_values[i]);
            _sink_v = Value();          // fresh: a sink never accumulates state
            return Var(&_sink_d, &_sink_v);
        }
        Var var(VarRef r) const { return var(r.index); }
        /** @brief Resolve by name: how site tables and programs address a row.
         *  An unknown name lands on the sink and bumps the unresolved count. */
        Var var(const char* name) const { return var(find(name)); }

        /** @brief Parameter @p k, 0-based over the ROLE_PARAM block. */
        Var      param(uint16_t k) const { return var((uint16_t)(_param_first + k)); }
        uint16_t param_count() const     { return _param_count; }
        /** @brief Table index of the first ROLE_PARAM row (0 when there are
         *  none). */
        uint16_t param_first() const     { return _param_first; }

        /** @brief Where @p h's text lives in the pool, or null. The ONLY route to
         *  a row's text: the declared capacity is in flash and says nothing about
         *  whether the row got a slot. @p cap_out takes the usable capacity. */
        char* text_at(Var h, uint16_t* cap_out = nullptr) const;

        /** @brief Find a variable by name; VAR_NONE when absent, which resolves to
         *  the sink. Binary search over _index. */
        VarRef find(const char* name) const;
        /** @brief Length-bounded form, for a shell argument. Operator input, so a
         *  miss is a typo rather than a defect: it does NOT count. */
        VarRef find(const char* name, uint16_t len) const;

        /** @brief Name lookups that found nothing. Zero on a correctly wired
         *  site; the test suite asserts it. */
        uint16_t unresolved_count() const { return _unresolved; }

        /** @name Write attribution
         *  Who among the control programs drives a row, recorded on the way
         *  through Plc::Io. @{ */

        /** @brief Record @p who as the writer of @p r; false when it already
         *  belonged to another. First claim wins, and the write still goes
         *  through: refusing it would leave the plant where the loser left it. */
        bool claim_writer(VarRef r, uint8_t who);

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
        bool name_index_built() const { return _indexed; }

        /** @brief Pool bytes claimed, and rows that found none left. The second
         *  must be zero: such a row reads empty and refuses every write. */
        uint16_t pool_used() const     { return _pool_used; }
        uint16_t pool_overflow() const { return _pool_overflow; }

        /** @name Control authority
         *  Two INDEPENDENT sources decide whether the field may be driven and the
         *  STRICTER wins: the operator's standing choice and the system's hold.
         *  Starts held by the system. See docs/dev/scan.md. @{ */

        /** @brief Set the OPERATOR's standing mode (the console's `control`). */
        void set_control_mode(ControlMode mode);

        /** @brief The operator's standing choice, whatever the system is doing. */
        ControlMode operator_mode() const { return _op_mode; }

        /** @brief Raise or release the SYSTEM's hold: bring-up, a health fault, a
         *  reset we have decided to take. Forces CONTROL_DISABLED.
         *  @warning Unlike the operator's own disable this ERASES every output
         *  override, stored copy included. */
        void set_health_hold(bool held);

        /** @brief Whether the system is holding control down, so the console can
         *  say WHY a `control enable` did nothing. */
        bool health_hold() const { return _health_hold; }

        /** @brief What is actually in force: the stricter of the two sources. */
        ControlMode control_mode() const {
            return _health_hold ? CONTROL_DISABLED
                                : _op_mode;
        }

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

        /** @brief Invalidate any ROLE_INPUT outside its declared band, and any
         *  T_F32 gone non-finite (checked separately: a NaN trips no band). What
         *  F_BOUNDED is FOR: a shorted 4-20 mA loop reads full scale, which no
         *  timeout catches. Analog rows are skipped, their band being editable.
         *  @return how many readings this pass rejected. */
        uint16_t reject_implausible();

        /** @name Writing
         *  set() edits a PARAMETER against its declared band; override() forces
         *  ANY variable, type-checked only. NEITHER writes to flash: applying and
         *  committing are separate steps. @{ */

        /** @brief Apply @p text to parameter @p h, live but unsaved. A
         *  non-parameter is SET_INVALID - use override() for those. */
        SetResult set(Var h, const char* text);

        /** @brief Whether @p v WOULD be accepted, writing nothing. For a caller
         *  committing several rows as one unit (a multi-register write). */
        SetResult check(Var h, Scalar v) const;

        /** @brief The same, value already typed. Going via text would apply the
         *  row's DISPLAY rounding, storing 0.083 as 0.08. T_STR is refused. */
        SetResult set(Var h, Scalar v);

        /** @brief Apply @p v WITHOUT marking the row unsaved, for a writer that
         *  paces the flash side itself (the Modbus persist queue).
         *  @warning Pair every SET_OK with a save(), or the setting is lost. */
        SetResult set_deferred(Var h, Scalar v);

        /** @brief Commit @p h's value and clear its unsaved mark; harmless on a
         *  durable row. @return SET_OK, or SET_NOT_PERSISTED. */
        SetResult save(Var h);

        /** @brief Put @p h back to its factory default, live and durably: `def`
         *  returns and the stored copy is dropped, so a reboot cannot undo it.
         *  @return SET_OK; SET_INVALID on a non-parameter; SET_NOT_PERSISTED
         *  when the store kept it (the live value is still reset). */
        SetResult clear_param(Var h);

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
         *  QUALITY is `override_set(h, h.raw(), false)`, which is unsaved like
         *  any other override. */
        void override_set(Var h, Scalar v, bool valid = true);

        /** @brief Force @p h to what it reads right now, keeping its validity -
         *  for pinning a signal while you work on what depends on it. */
        void override_freeze(Var h);

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

        /** @brief Variables currently overridden. */
        uint16_t override_count() const;

        /** @} */

    private:
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

        /** @brief @p h's row index, or VAR_NONE when it points at the sink. */
        uint16_t _row_of(Var h) const;
        /** @brief The same from the descriptor: which row this address IS.
         *  VAR_NONE for the sink or another bank's row. */
        uint16_t _row_of(const Variable* d) const;
        /** @brief The value slot behind an editable row, or null. The one
         *  admission check set() and check() share. */
        Value* _editable_slot(const Variable* d) const;
        /** @brief Write @p h's override through to the store; false when it
         *  refused (already logged) or there is no store. */
        bool _persist_override(Var h);
        /** @brief Mark @p h's override live-but-unsaved. */
        void _mark_override_unsaved(Var h);
        /** @brief Write an edited parameter through to the store, loudly when it
         *  refuses. Shared by both set() overloads. */
        SetResult _persist_param(Var h, const Variable& d, Value& slot);

        /** @brief The one output sweep both control edges run. @p before tells a
         *  transition from a re-assertion; @p erase_overrides is the system's
         *  destructive edge. Outputs only. */
        void _apply_control(ControlMode before, bool erase_overrides);
        /** @brief Bring row @p i's mute bit in step with the current mode; true
         *  when it moved. ROLE_OUTPUT only. The one owner of "an output's
         *  override reaches the field iff the mode is not CONTROL_DISABLED". */
        bool _sync_override_mute(uint16_t i);

        /** @brief Locate the ROLE_PARAM run. A parameter outside it is
         *  unreachable as one, the pages addressing the block by offset. */
        void _find_param_block();

        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access: scratch, never shared state. */
        static const Variable _sink_d;
        static Value          _sink_v;
    };

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
