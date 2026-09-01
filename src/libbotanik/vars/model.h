#ifndef VARS_MODEL_H
#define VARS_MODEL_H

/**
 * @file model.h
 * @brief The Bank: the one object that owns the table, and gets, sets and
 *        overrides through it.
 * @note The slot and the handle are handle.h and the const half descriptor.h,
 *       both included here, so a consumer needs only this one header.
 *       See docs/dev/vars.md.
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


    /** @brief What everything is handed to reach the variables.
     *  @note A bad index - VAR_NONE included - resolves to an inert sink, never
     *  CLAMPING: clamping would alias it onto a real row, so a typo could drive
     *  an actual relay. Method naming: @ref dev_vars_naming. */
    class Bank : public Botanik::Component {
    public:
        /** @brief The two sweeps this class owns, both at VAR_EXPIRY_MS in
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
        /** @brief Name lookups that found nothing: non-zero after bring-up means
         *  something names a row that is not in vars.h. Mutable, being
         *  diagnostics behind a const find(). */
        mutable uint16_t _unresolved = 0;
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

        /** @} */

        /** @} */

        /** @name Driving
         *  What a control PROGRAM calls: claim the row for whoever the window
         *  names, then write. Distinct from param_set(), which edits a
         *  parameter against its band.
         *  @{ */
        void drive(VarRef r, bool v)      { var(r).set(v); }
        void drive(VarRef r, float v)     { var(r).set(v); }
        /** @} */

        /** @name Typed access
         *  The same index and the same accessor as the untyped pair above, with
         *  the row's type and its declaration section in the reference: reading
         *  a digital as a float, or driving a VAR_INPUT, does not compile.
         *  @{ */

        /** @brief Read @p h, or the row's DECLARED value when it is not
         *  believable. Every direction reads - a POU may read its own output.
         *
         *  Var::_under() already substitutes for the qualities with nothing
         *  behind them at all - Q_NO_DATA, Q_SAFE, Q_IMPLAUSIBLE. What this
         *  ADDS is Q_STALE, the one that keeps a value. So a law computes on a
         *  number somebody chose rather than on a reading of unbounded age,
         *  while the pages, the telemetry and the register map read through
         *  Var and an operator looking for what a dead probe LAST said still
         *  finds it.
         *  @note An override reads as the force: valid() is asked of the value
         *  in effect, so a forced row is believable and never substituted. */
        template <typename T, Dir D>
        T get(Ref<T, D> h) const {
            const Var v = var(h.r);
            return v.valid() ? RefIo<T>::read(*this, h.r)
                             : RefIo<T>::of(v.declared());
        }

        /** @brief Drive @p h. @p V is CHECKED against @p T, not converted, so a
         *  mismatch names itself instead of reporting a deduction conflict.
         */
        template <typename T, Dir D, typename V>
        void drive(Ref<T, D> h, V v) {
            static_assert(D != Dir::IN, "a VAR_INPUT row cannot be driven");
            static_assert(SameType<T, V>::value, "value type must be the row's own type");
            RefIo<T>::write(*this, h.r, (T)v);
        }

        /** @} */

        /** @name Binding
         *  One call per variable a program declares. A name matching no row, or
         *  a row of another type, lands on the sink and counts as unresolved.
         *  Declaring an OUTPUT also CLAIMS it for the open writer window.
         *  @see @ref dev_vars_writers
         *  @{ */

        template <typename T> RefIn<T> resolve_in(const char* name) {
            return RefIn<T>(_resolve_typed<T>(name), this);
        }
        template <typename T> RefOut<T> resolve_out(const char* name) {
            const VarRef r = _resolve_typed<T>(name);
            return RefOut<T>(r, this);
        }
        /** @brief The same, and MARKS the row shared: a VAR_IN_OUT row is
         *  read-modify-written by more than one writer BY DECLARATION, so it
         *  records no single owner and the conflict scan leaves it alone. */
        template <typename T> RefInOut<T> resolve_inout(const char* name) {
            const VarRef r = _resolve_typed<T>(name);
            return RefInOut<T>(r, this);
        }

        /** @} */



        /** @} */

        /** @brief Whether the name index is in use. False before freeze(), and
         *  every find() is then a full scan. */

        /** @brief Rows that found no pool left. Must be zero: such a row reads
         *  empty and refuses every write. */
        uint16_t pool_overflow() const { return _pool_overflow; }

        /** @brief Invalidate ROLE_INPUTs whose last good read is older than
         *  their declared timeout. A timeout of 0 never expires, for a value a
         *  driver publishes only on change. */
        void expire_stale(uint64_t now_ms);

        /** @name Writing
         *  param_set() edits a PARAMETER against its declared band;
         *  override_set() forces ANY variable, type-checked only. NEITHER
         *  writes to flash: applying and committing are separate steps. @{ */

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
        /** @} */

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

        /** @brief Put @p h back to its factory default, live and durably: the
         *  stored copy is dropped, so a reboot cannot undo it. A RETAINED
         *  parameter is refused (@see param_retained).
         *  @return SET_INVALID on a non-parameter or a retained one;
         *  SET_NOT_PERSISTED when the store kept it, the live value reset. */
        SetResult param_reset(Var h);

        /** @brief Forget @p h's UNSAVED edit: the row goes back to what a
         *  reboot would give it, and the unsaved mark clears. The STORED copy
         *  is left alone - that is the whole difference from param_reset().
         *  @return SET_OK, and untouched, when nothing was outstanding. */
        SetResult param_clear(Var h);

        /** @brief Commit every parameter still outstanding, owed either way.
         *  @return how many were written; @p refused, when given, takes the
         *  count the store rejected. */
        uint16_t save_params(uint16_t* refused = nullptr);

        /** @brief Reclassify @p h's outstanding write as a PROGRAM's, owed to
         *  the retain cadence rather than to the operator. A write that changed
         *  nothing is left alone, so restating a value costs no erase.
         *  @see VF_PARAM_OWED */
        SetResult param_owe(Var h);

        /** @brief Commit the parameters a PROGRAM owes, leaving an operator's
         *  unsaved edits alone. What the retain cadence calls.
         *  @return how many were written. */
        uint16_t save_owed_params(uint16_t* refused = nullptr);

        /** @brief Parameters edited but not yet saved - what `params save` with
         *  no name would write, and what a display marks. */
        uint16_t unsaved_count() const;

        /** @brief Parameters a program has written and the cadence has not yet
         *  committed. Not counted by unsaved_count(): the params page marks
         *  what a PERSON typed, and a watering date is not that. */
        uint16_t owed_count() const;

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

        /** @brief Flip whether @p h's override READS valid, freezing the live
         *  value first when nothing was forced yet: "make this sensor read
         *  failed" is one act rather than force-then-invalidate.
         *  @return true if the override now reads VALID. */
        bool override_flip_valid(Var h);

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
        uint16_t override_clear_all();

        /** @brief How long after boot control is held, in ms, from
         *  `sys_startup_sec`. Falls back to Config::STARTUP_HOLD_MS where the
         *  row is absent, so a table without it still holds rather than
         *  silently running from the first beat. */
        uint32_t startup_hold_ms() const;

        /** @brief How long the laws stay stopped before the ending, in ms,
         *  from `sys_shutdown_sec`. @see startup_hold_ms */
        uint32_t shutdown_hold_ms() const;


        /** @} */

    private:
        /** @name Bring-up, in this order
         *  The three restores on_setup() runs, and the ORDER is the contract:
         *  parameters first, then the overrides that shadow them, then the
         *  retained totals, which neither touches. Private because calling one
         *  out of turn loads a setting over the override forcing it.
         *  @{ */
        void     load_params();
        uint16_t load_overrides();
        uint16_t load_retained();
        /** @} */

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

        /** @brief Push each sink's floor into it from the row that sink
         *  names. Here because this is the object already
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


        /** @brief Locate the ROLE_PARAM run. A parameter outside it is
         *  unreachable as one, the pages addressing the block by offset. */
        void _find_param_block();

        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access: scratch, never shared state. */
        static const Descriptor _sink_d;
        static Slot          _sink_v;
    };

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
        static bool read(const Bank& b, VarRef r)  { return b.var(r).as_bool(); }
        static bool of(Value v)                    { return v.as_bool(); }
        static void write(Bank& b, VarRef r, bool v) { b.var(r).set(v); }
        static SetResult edit(Bank& b, VarRef r, bool v) { return b.param_set(b.var(r), v); }
    };

    template <> struct RefIo<float> {
        static bool  accepts(VarType t)              { return t == T_F32; }
        static float read(const Bank& b, VarRef r)   { return b.var(r).as_f32(); }
        static float of(Value v)                     { return v.as_f32(); }
        static void  write(Bank& b, VarRef r, float v) { b.var(r).set(v); }
        static SetResult edit(Bank& b, VarRef r, float v) { return b.param_set(b.var(r), v); }
    };

    // The eight widths now differ in nothing but the width itself.
    #define BOTANIK_REF_WHOLE(TYPE)                                            \
        template <> struct RefIo<TYPE> {                                       \
            static bool accepts(VarType t)            { return ref_whole(t); } \
            static TYPE read(const Bank& b, VarRef r) { return b.var(r).value().as<TYPE>(); } \
            static TYPE of(Value v)                   { return v.as<TYPE>(); } \
            static void write(Bank& b, VarRef r, TYPE v) { b.var(r).set_i((int64_t)v); } \
            static SetResult edit(Bank& b, VarRef r, TYPE v)                   \
                { return b.param_set_i(b.var(r), (int64_t)v); }                \
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

    /** @} */

    // Ref's bodies, out of line because a reference is declared before the
    // table it reads. Grouped at their declarations, in ref.h.

    template <typename T, Dir D>
    inline T Ref<T, D>::get() const { return io->get(*this); }

    template <typename T, Dir D>
    inline bool Ref<T, D>::valid() const { return io->var(r).valid(); }

    template <typename T, Dir D>
    inline T Ref<T, D>::held() const { return RefIo<T>::read(*io, r); }

    template <typename T, Dir D>
    inline bool Ref<T, D>::edit(T v) const {
        static_assert(D == Dir::INOUT, "only a VAR_IN_OUT row is edited");
        if (RefIo<T>::edit(*io, r, v) != SET_OK) return false;
        // OWED, not written through. A law stamps a watering date inside a
        // scan, and an erase there is both a stall and, repeated, a dead part.
        // The retain cadence is what carries it to the store.
        io->param_owe(io->var(r));
        return true;
    }

    template <typename T, Dir D>
    inline void Ref<T, D>::drive(T v) const {
        // ST has ONE write macro, so the ROW is what decides which write it
        // means: a parameter is state the plant has to keep, anything else is
        // this scan's answer.
        if (io->descriptor(r.index).role != ROLE_PARAM) { io->drive(*this, v); return; }
        if (RefIo<T>::edit(*io, r, v) != SET_OK) return;
        io->param_owe(io->var(r));
    }

    template <typename T, Dir D>
    inline void Ref<T, D>::invalidate() const {
        static_assert(D != Dir::IN, "a VAR_INPUT row cannot be invalidated");
        io->var(r).invalidate(Q_NO_DATA);
    }


}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
