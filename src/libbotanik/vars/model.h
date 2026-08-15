#ifndef VARS_MODEL_H
#define VARS_MODEL_H

/**
 * @file model.h
 * @brief The RAM half of the variable model: the value slot, the handle over
 *        (descriptor, slot), and the Bank that owns the table.
 *
 * The const half - the fields a variable is declared along and the Variable
 * descriptor itself - is descriptor.h, included here, so a consumer still needs
 * only this one header.
 */

#include <cstdint>                  // uint8_t/uint16_t/int64_t

#include "libbotanik/config/vars.h"  // Config::VAR_MAX / VAR_SEGMENTS / STR_POOL_BYTES
#include "libbotanik/core/component.h"  // Component - the Bank registers its own sweeps
#include "libbotanik/vars/descriptor.h" // the const half: Variable and its fields
#include "libbotanik/vars/scalar.h" // Vars::Scalar / VarType

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Tasks { struct Tick; } }

namespace Botanik { namespace Vars {

    /** @brief One value slot, parallel to the descriptor table. No `def`: the
     *  default is a typed literal in flash.
     *
     *  While @c ovr_on, @c ovr is what every reader sees instead of @c v - it
     *  SHADOWS, so clearing snaps back to live. @c ovr_valid is separate so
     *  "force this to 20 C" and "read as a dead sensor" are both expressible.
     *  @warning Both halves are PERSISTED, so a forgotten override survives the
     *  reboot someone reaches for - hence the CRITICAL on each restore. */
    struct Value {
        Scalar   v;
        Scalar   ovr;        ///< forced value, while ovr_on
        uint64_t stamp_ms;   ///< uptime ms of the last good update
        /** @brief T_STR only: this row's slot in the site's pool, or
         *  STR_NO_SLOT when it did not fit. Assigned by Bank::init(), never
         *  declared. Ahead of the bools for its 2-byte alignment: behind them it
         *  bought a pad byte that pushed the row past 32. */
        uint16_t str_off;
        /** @brief The live value's quality - valid, or invalid and why.
         *  @see Quality */
        Quality  qual;
        bool     ovr_on;     ///< an override is in force
        bool     ovr_valid;  ///< what valid() reports while overridden
        uint8_t  vflags;     ///< ValueFlags
        /** @brief Which control program last claimed this row (its registry
         *  index), WRITER_NONE until one does. Names the writer on the MONITOR
         *  page, and catches TWO programs driving one output - a defect that
         *  otherwise shows only as an actuator chattering at the beat rate. */
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

    /** @brief A text row that found no room in the pool.
     *
     *  Not 0, which is a REAL slot (the first text row's): an overflowed row
     *  parked at offset 0 keeps its declared capacity, so writers would write a
     *  full slot on top of that first row. "No slot" must be sayable for a
     *  writer to refuse. */
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
         *  An invalid variable reads its declared default, so logic that does
         *  not care about quality still gets a sane number. The override is
         *  applied HERE, the one place every reader goes through, so no path
         *  sees around it. @{ */
        /** @brief What this row reads, override applied. @see _under() for
         *  the fallback rule. */
        Scalar   raw()   const { return _forced() ? _v->ovr : _under(); }
        /** @brief As a float; a digital reads 0/1, so a consumer that displays
         *  or publishes never branches on the type. */
        float    value() const { return as_f32(_d->type, raw()); }
        bool     b()     const { return as_bool(_d->type, raw()); }
        int64_t  i()     const { return as_i64(_d->type, raw()); }
        bool     valid() const { return _forced() ? _v->ovr_valid : live_valid(); }
        /** @name Under the override
         *  What the row would read with nothing forced, and its real quality.
         *  Diagnostics only - control logic reads raw()/valid(). `get` shows
         *  both, so an operator can tell whether a sensor reads sane again
         *  before releasing one. @{ */
        Scalar   live()       const { return _under(); }
        bool     live_valid() const { return _v->qual == Q_GOOD; }
        /** @brief ...and WHY, when it is not good. An override does not change
         *  it: the row underneath keeps its quality. */
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
         *  field dirty - this is a reading coming IN, and marking it would ask
         *  the driver to write back what it just read. */
        void publish(Scalar v, uint64_t now_ms) {
            _v->v = v; _v->stamp_ms = now_ms; _v->qual = Q_GOOD;
        }
        /** @brief The quantity, without claiming anything MEASURED it. For a
         *  simulated field, which IS the device: stamping Q_GOOD here would keep
         *  the row good for ever and no timeout could catch a device going
         *  quiet. Quality and staleness stay the reader's business. */
        void set_physical(Scalar v) { _v->v = v; }
        /** @brief Mark the reading bad, and say why. For a driver that decided a
         *  well-formed value is not a believable one - an analog reading outside
         *  its band, which Bank::reject_implausible() cannot judge because the
         *  band lives in the parameter table. */
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
        /** @brief Owe this demand to the field again, without touching the
         *  demand itself.
         *
         *  For a driver that has learned the device no longer necessarily has
         *  what it was last sent: a slave that dropped off the bus and came back
         *  has been through its own power-on defaults. The value stands, only
         *  its delivery is owed again. */
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
         *
         *  @warning Private, and load-bearing: each has a storage side only the
         *  Bank can reach. A set skipping the unsaved mark could never be
         *  committed; a clear skipping the store would come BACK. @{ */
        friend class Bank;

        /** @brief Force the variable to @p v, reading as @p valid. Marks the
         *  field dirty like any other write: an operator forcing an actuator by
         *  hand expects it to move. */
        void override_set(Scalar v, bool valid = true) {
            _v->ovr = v; _v->ovr_valid = valid; _v->ovr_on = true; _touch_field();
        }
        /** @brief Freeze at whatever it reads right now, keeping its quality. */
        void override_freeze() { override_set(raw(), valid()); }
        /** @brief Release the override; the live value takes over. Marks the
         *  field dirty too, or the actuator would stay where it was left. */
        void override_clear() { _v->ovr_on = false; _touch_field(); }
        /** @} */

        /** @brief Take a new demand, owing it to the field only if it MOVED. A
         *  control law re-states its whole demand every activation, and dirtying
         *  on each one leaves every output permanently owed - which starves the
         *  reads behind them. Q_SAFE counts as moved. */
        void _drive(Scalar v) {
            const bool moved = (_v->v.u != v.u) || (_v->qual != Q_GOOD);
            _v->v    = v;
            _v->qual = Q_GOOD;
            if (moved) _touch_field();
        }

        /** @brief What the row reads with no override: the ONE statement of the
         *  fallback rule, shared by raw() and live() so they cannot drift.
         *
         *  A STALE row KEEPS its value - a sensor that answered a minute ago
         *  still knows what the plant was doing. Q_NO_DATA, Q_SAFE and
         *  Q_IMPLAUSIBLE fall back: nothing to keep, a deliberate safe state,
         *  and a number already rejected for being wrong. */
        Scalar _under() const {
            return (_v->qual == Q_GOOD || _v->qual == Q_STALE) ? _v->v : _d->def;
        }

        /** @brief Whether the override reaches this reader: in force and not
         *  muted. CONTROL_DISABLED mutes an OUTPUT's without erasing it, so the
         *  field sees the safe state while the forced value waits. Other roles
         *  are unaffected - muting a forced sensor would erase the
         *  diagnosis. */
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
     *  EDIT: the value is live either way, and calling it invalid would send the
     *  operator to fix the wrong thing. */
    enum SetResult : uint8_t {
        SET_OK,             ///< applied (set), or written out (save)
        SET_INVALID,        ///< rejected - malformed, out of range, read-only
        SET_NOT_PERSISTED   ///< live in RAM, but storage refused the write
    };

    /** @brief Where a variable's value survives a reboot. The HAL::Storage
     *  implementation is in params.h; the Bank needs only the interface, so a
     *  test can hand it a stub. Overrides get their own keys: a different fact
     *  about the same row, and clearing one must not disturb the other. */
    struct VarStore {
        virtual ~VarStore() {}
        /** @brief Load @p d's stored value into @p v, or into @p text for a
         *  T_STR. @return false when the key is absent, which is normal - the
         *  caller keeps the declared default. @p text / @p cap is the row's
         *  slot, already bounded by the bank: a buffer rather than the pool and
         *  an offset, since the store cannot prove its own arithmetic. */
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
     *
     *  A bad index - VAR_NONE included - resolves to an inert sink, never
     *  faulting and never CLAMPING: clamping would alias it onto a real row, so
     *  a typo could drive an actual relay. A mistake shows up as a signal that
     *  never moves. */
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

        /** @brief Mutable so var() and pool() stay const, as they were when
         *  these were borrowed pointers: `const Bank&` gates the BANK's API, and
         *  never promised the image cannot be driven through a Var. */
        mutable Value _values[Config::VAR_MAX];
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
        /** @brief The operator's standing choice. Survives a health fault
         *  coming and going, which is the point of keeping it separate. */
        ControlMode   _op_mode = CONTROL_ENABLED;
        /** @brief The system's hold: bring-up, a health fault, a reset we have
         *  decided to take. Starts SET - outputs are not ours to move yet. */
        bool          _health_hold = true;
        /** @brief Name lookups that found nothing. A typo resolves to the sink
         *  and reads zero forever, so the count is what makes it visible:
         *  non-zero after bring-up means something names a row that is not in
         *  vars.h. Mutable: find() is const and this is diagnostics. */
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

        /** @brief Add a segment of rows, in flash, borrowed for the bank's
         *  lifetime. Once per group a caller keeps separate. Rows land in the
         *  flat index space in call order, so REGISTRATION ORDER IS TABLE ORDER.
         *  @return false if it did not fit, in which case NONE of it was added
         *  and refused_count() records the loss. */
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
         *  Wired once at bring-up, before load_params(); the logger may be null.
         *  A write the store refused is the one thing the bank must not
         *  swallow. */
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
         *  role, ROLE_OUTPUT included, since an override means one thing
         *  wherever it sits.
         *  @warning A restored output override is DRIVING the plant from this
         *  moment, so each restore is a CRITICAL naming the row; do not quiet
         *  it. @return how many were restored. */
        uint16_t load_overrides();

        /** @name Retained values (F_RETAIN)
         *
         *  The rows whose value IS the history - litres delivered, hours run.
         *  @{ */

        /** @brief Whether @p d is retained: `F_RETAIN` on `ROLE_COMPUTED`, and
         *  nothing else. A parameter already persists, an input is overwritten
         *  by the next scan, and restoring an output from flash would energise a
         *  contactor before anything looked at the plant. One predicate, so load
         *  and save cannot disagree. */
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

        /** @brief The descriptor for LOGICAL row @p i, already bounds-checked by
         *  the caller. One hop through the redeclaration map, then the segment
         *  walk. */
        const Variable& desc(uint16_t i) const { return _desc_phys(_row[i]); }

        /** @brief Resolve an index. Never fails; a bad one lands on the sink. */
        Var var(uint16_t i) const {
            if (i < _count) return Var(&desc(i), &_values[i]);
            _sink_v = Value();          // fresh: a sink never accumulates state
            return Var(&_sink_d, &_sink_v);
        }
        Var var(VarRef r) const { return var(r.index); }
        /** @brief Resolve by name. This is how site tables and control programs
         *  address a variable; an unknown name lands on the sink and bumps the
         *  unresolved count. */
        Var var(const char* name) const { return var(find(name)); }

        /** @brief Parameter @p k, 0-based over the ROLE_PARAM block. */
        Var      param(uint16_t k) const { return var((uint16_t)(_param_first + k)); }
        uint16_t param_count() const     { return _param_count; }
        /** @brief Table index of the first ROLE_PARAM row (0 when there are
         *  none). */
        uint16_t param_first() const     { return _param_first; }

        /** @brief Where @p h's text lives in the pool, or null (not a T_STR, no
         *  pool, or no room at init()). The ONLY route to a row's text: the
         *  declared capacity is in flash and says nothing about whether the row
         *  got a slot. @p cap_out takes the usable capacity. */
        char* text_at(Var h, uint16_t* cap_out = nullptr) const;

        /** @brief Find a variable by name; VAR_NONE when absent, which resolves
         *  to the sink - so a caller may skip the check, at the cost of a silent
         *  miss that unresolved_count() surfaces. Binary search over _index. */
        VarRef find(const char* name) const;
        /** @brief Length-bounded form, for a shell argument that is not
         *  NUL-terminated. Operator input, so a miss here is a typo at the
         *  keyboard rather than a build defect: it does NOT count. */
        VarRef find(const char* name, uint16_t len) const;

        /** @brief How many name lookups have found nothing. Zero on a
         *  correctly wired site; the test suite asserts that after resolving
         *  every table and running every control program. */
        uint16_t unresolved_count() const { return _unresolved; }

        /** @name Write attribution
         *
         *  Who among the control programs drives a row. Recorded on the way
         *  through `Plc::Io`, which is the only route a program has to write -
         *  that is what makes this knowable at all.
         *  @{ */

        /** @brief Record @p who as the writer of @p r; false when @p r already
         *  belonged to another program.
         *
         *  First claim wins, silently. A SECOND writer is a design defect, not a
         *  race: the output takes whichever ran last and chatters at the scan
         *  rate. Latched, since programs on different rungs collide rarely. The
         *  write still goes through - refusing it would leave the plant wherever
         *  the losing program left it. */
        bool claim_writer(VarRef r, uint8_t who);

        /** @brief The program that claimed @p r, or WRITER_NONE. */
        uint8_t writer_of(VarRef r) const;

        /** @brief How many ROWS two programs have both written, and the first.
         *  Counted, not logged: the write path has no logger. Rows rather than
         *  collisions, since two programs collide every beat while the count of
         *  doubly-authored rows is fixed by the site's design. */
        uint16_t writer_conflicts() const { return _conflicts; }
        VarRef   first_conflict() const   { return _conflict_row; }

        /** @} */

        /** @brief Whether the name index is in use. False before freeze(), and
         *  every find() is then a full scan. */
        bool name_index_built() const { return _indexed; }

        /** @brief Pool bytes the text rows claimed, and how many found none
         *  left. The second must be zero (the site test asserts it): a row with
         *  no slot reads empty and silently refuses to be set. */
        uint16_t pool_used() const     { return _pool_used; }
        uint16_t pool_overflow() const { return _pool_overflow; }

        /** @name Control authority
         *
         *  Whether the firmware may drive the field, in three modes. Two
         *  INDEPENDENT sources decide and the STRICTER wins: the operator's
         *  standing choice and the system's hold - so `control disable` survives
         *  a health fault coming and going, and a recovered firmware cannot talk
         *  over the operator. Starts held by the system.
         *  @{ */

        /** @brief Set the OPERATOR's standing mode (the console's `control`
         *  command). Independent of the system's hold; the stricter of the two
         *  is what takes effect. */
        void set_control_mode(ControlMode mode);

        /** @brief The operator's standing choice, whatever the system is doing.
         *  What `control` reports back so a mode that is not in effect still
         *  reads as the operator's. */
        ControlMode operator_mode() const { return _op_mode; }

        /** @brief Raise or release the SYSTEM's hold: bring-up, a health fault,
         *  a reset we have decided to take. Forces CONTROL_DISABLED whatever the
         *  operator asked, and unlike their own disable it **erases** every
         *  output override, stored copy included - the operator is present and a
         *  fault is not, and an actuator held energised by hand through one is
         *  what the safe state exists to prevent. */
        void set_health_hold(bool held);

        /** @brief Whether the system is holding control down, so the console can
         *  say WHY a `control enable` did not take effect. */
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

        /** @brief Invalidate any ROLE_INPUT that has left its declared band, and
         *  any T_F32 gone non-finite (checked separately: a NaN compares false
         *  against everything and trips no band).
         *
         *  What F_BOUNDED is FOR on an input: a shorted 4-20 mA loop reads full
         *  scale, which no timeout catches. Analog rows are skipped - their band
         *  is operator-editable and needs the parameter table.
         *  @return how many readings this pass rejected. */
        uint16_t reject_implausible();

        /** @name Writing
         *  set() edits a PARAMETER against its declared band; override() forces
         *  ANY variable, checked against the type only, so a reading can be
         *  pushed past its band to exercise the alarm watching for it. **Neither
         *  writes to flash**: applying and committing are separate, and an
         *  unsaved row is marked rather than left looking committed. @{ */

        /** @brief Apply @p text to parameter @p h, live but unsaved. Rejects a
         *  non-parameter (SET_INVALID): writing a sensor or an output here would
         *  be overwritten by the next scan, which is what override() is for. */
        SetResult set(Var h, const char* text);

        /** @brief Whether @p v WOULD be accepted, writing nothing. The admission
         *  half of set(), for a caller committing several rows as one unit (the
         *  Modbus slave's multi-register write). The rules live here only, so a
         *  later one cannot be missed on the check. */
        SetResult check(Var h, Scalar v) const;

        /** @brief The same, value already typed (a Modbus write). Going via text
         *  would apply the row's DISPLAY rounding, storing 0.083 as 0.08 in a
         *  2-decimal parameter. T_STR is refused: its text is in the pool. */
        SetResult set(Var h, Scalar v);

        /** @brief Apply @p v now WITHOUT marking the row unsaved, for a writer
         *  that commits and paces the flash side itself (the Modbus slave's
         *  persist queue). Unmarked because the row is not waiting for an
         *  operator, it is already queued.
         *  @warning Pair every SET_OK with a save(), or the setting is lost at
         *  the next reboot. */
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
         *  unsaved_count() for the same reason the flags are: an override and
         *  the setting under it are different facts, under different keys. */
        uint16_t unsaved_override_count() const;

        /** @name Making edits durable
         *  Nothing reaches flash on its own: an edit is LIVE at once and DURABLE
         *  when saved, so a setting that breaks the console it was typed on was
         *  never written and a power cycle brings the last saved one back. The
         *  cost is a state that can lose work, which is why the shell reports
         *  the unsaved count. @{ */

        /** @brief Force @p h to @p text until override_clear(). T_STR is refused
         *  (a hostname is configuration, not a signal); everything else,
         *  ROLE_OUTPUT included, is fair game whatever control authority says -
         *  staging a hand-drive is a real workflow. */
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
         *  the bank: Var::override_clear() cannot reach the store, so one would
         *  come back at the next boot. */
        void override_clear(Var h);

        /** @brief Release every override in one go (what a maintainer wants
         *  after a session, and what init() does). @return how many were on. */
        uint16_t clear_overrides();

        /** @brief Variables currently overridden - surfaced so a forgotten
         *  override cannot hide. */
        uint16_t override_count() const;

        /** @} */

    private:
        /** @brief The descriptor at PHYSICAL row @p p, i.e. before the
         *  redeclaration map. The one place segments are visible; their bases
         *  ascend, so walking down from the last costs one compare in the usual
         *  case. */
        const Variable& _desc_phys(uint16_t p) const {
            uint8_t s = (uint8_t)(_segments - 1);
            while (s > 0 && p < _seg_base[s]) --s;
            return _seg[s][p - _seg_base[s]];
        }

        /** @brief Build _row, dropping every row a later one redeclares. Runs
         *  FIRST in freeze(), before init() seeds a slot for a row that is about
         *  to disappear. @see _row */
        void _resolve_shadows();

        /** @brief Sort _index into name order, once, at freeze(). The table is
         *  const and never grows, so nothing invalidates it. A no-op (leaving
         *  _indexed false) when the site supplied no scratch array. */
        void _build_index();
        /** @brief Row order for the sort: by name, ties broken on row number, so
         *  a duplicated name resolves to the first of the two. */
        bool _order_before(uint16_t a, uint16_t b) const;
        /** @brief The one lookup both find() overloads run; @p len 0 means
         *  NUL-terminated. @return the row index, or VAR_NONE. Does not count a
         *  miss - the caller's call, since a shell typo is not a defect. */
        uint16_t _lookup(const char* name, uint16_t len) const;

        /** @brief @p h's row index, or VAR_NONE when it points at the sink. */
        uint16_t _row_of(Var h) const;
        /** @brief The same from the descriptor: which row this address IS.
         *  VAR_NONE for the sink or another bank's row. */
        uint16_t _row_of(const Variable* d) const;
        /** @brief The value slot behind an editable row, or null (not a
         *  parameter, read-only, or not ours). The one admission check set() and
         *  check() share. */
        Value* _editable_slot(const Variable* d) const;
        /** @brief Write @p h's override through to the store; false when it
         *  refused (already logged) or there is no store. */
        bool _persist_override(Var h);
        /** @brief Mark @p h's override live-but-unsaved. */
        void _mark_override_unsaved(Var h);
        /** @brief Write an edited parameter through to the store, and say so
         *  loudly when the store refuses. Shared by both set() overloads. */
        SetResult _persist_param(Var h, const Variable& d, Value& slot);

        /** @brief The one output sweep both control edges run. @p before tells a
         *  transition from a re-assertion; @p erase_overrides is the system's
         *  destructive edge. Outputs only: sweeping a parameter would revert the
         *  network settings on a scan overrun. */
        void _apply_control(ControlMode before, bool erase_overrides);
        /** @brief Bring row @p i's mute bit in step with the current mode; true
         *  when it moved. ROLE_OUTPUT only; an out-of-range index is a no-op.
         *  The one owner of "an output's override reaches the field iff the mode
         *  is not CONTROL_DISABLED" - every path raising @c ovr_on goes through
         *  it, so none can state the invariant differently. */
        bool _sync_override_mute(uint16_t i);

        /** @brief Locate the ROLE_PARAM run: first row, then how many follow
         *  without a break. A parameter outside it is unreachable as one, since
         *  the pages address the block by offset; the site test asserts none. */
        void _find_param_block();

        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access: scratch, never shared state. */
        static const Variable _sink_d;
        static Value          _sink_v;
    };

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
