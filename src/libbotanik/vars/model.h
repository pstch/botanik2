#ifndef VARS_MODEL_H
#define VARS_MODEL_H

/**
 * @file model.h
 * @brief The variable model: one descriptor, one value slot, one handle, one
 *        table.
 *
 * Everything the firmware exposes is a *variable* - a Modbus meter reading, a
 * relay coil, a computed global, a configuration parameter. They differ in three
 * independent ways, and each is a FIELD rather than a type:
 *
 *  - @ref VarType  what the value IS (bool, u16, f32, text, ...)
 *  - @ref Transport how it reaches the field (or that nothing drives it)
 *  - @ref VarRole   what it is FOR, which is what drives behaviour: who expires,
 *                   who drops to safe on a fault, who persists, who is editable
 *
 * The predecessor encoded the first two in the type system, as four descriptor
 * structs, two value structs, four handle classes and six accessors. That put
 * "analog versus digital" in nineteen separate places and made three quarters of
 * the handle code copies of each other. Here there is one of each, and adding a
 * transport or a role touches a switch rather than a hierarchy.
 *
 * @section model_split Descriptor and value
 *
 * A variable is split in two, deliberately: its **descriptor** is `const` and
 * lives in flash (generated from the site's project), and its **value** lives in
 * a parallel RAM array indexed the same way. That split is what lets the tables
 * stay in flash while the values move.
 *
 * Plain aggregates only, no constructors and no virtuals: a type with a vptr is
 * not a literal type in C++11 and would land the whole table in .bss behind a
 * startup initialiser. The Scalar members are the one exception and are
 * deliberately safe - see scalar.h.
 *
 * @section model_default The default does three jobs
 *
 * @c def is an input's substitute when a read is invalid, an output's safe state
 * on a fault, and a parameter's factory value. One field, three uses, so there
 * is no separate fallback or safe-state concept to keep in sync. It is a typed
 * literal, not text: a bad default is a compile error rather than a boot-time
 * parse that quietly yields zero.
 */

#include <cstdint>

#include "scalar.h"

namespace Botanik { namespace Logs { class Logger; } }

namespace Botanik { namespace Vars {

    /** @name Descriptor
     *  @{ */

    /** @brief Where a variable's value comes from. This is the axis the model
     *  is organised on - not analog/digital, which is a property of the wire and
     *  of the value type respectively. */
    enum Transport : uint8_t {
        XP_NONE,            ///< nothing drives it: a computed global or a parameter
        XP_LOCAL,           ///< an internal digital/analogic/relay input/output
        XP_MODBUS           ///< a Modbus RTU digital/analogic/relay input/output
    };

    /** @brief What a variable is FOR. Drives behaviour that the type system used
     *  to encode, so the rules live in one switch instead of in six tables. */
    enum VarRole : uint8_t {
        ROLE_INPUT,      ///< read from the field; goes invalid past its timeout
        ROLE_OUTPUT,     ///< driven to the field; dropped to `def` on a fault
        ROLE_COMPUTED,   ///< computed; never expires, never swept
        ROLE_PARAM       ///< persisted, editable, and always valid
    };

    /**
     * @brief What a variable IS, declared rather than inferred.
     *
     * `Transport` and `VarRole` stay the axes BEHAVIOUR turns on - who expires,
     * who drops to safe, who persists. This is the third thing a reader wants
     * and neither of them answers: which of the dozen concrete kinds of row
     * this is. It used to be reconstructed from (transport, role, type) at every
     * display site, which meant three fields had to agree and a new kind was a
     * new case in each of them.
     *
     * Declared, so it is also what a table ITERATES on: "every alarm", "every
     * relay", "every Modbus reading" is one comparison rather than a predicate
     * over three fields.
     *
     * Each value has exactly one factory in declare.h, named after it, and that
     * factory takes only the arguments its scope can use - which is what makes a
     * coil with a plausibility band, or an alarm with a register, unspellable.
     *
     * Two distinctions worth spelling out, because neither is visible in the
     * name:
     *
     *  - **MI/MO versus AI/AO** is whether a CONVERSION happens. An MI row's
     *    register maps 1:1 onto the variable's type; an AI row is a raw count
     *    run through `scale`/`offset`.
     *  - **SV versus G** is who can SEE it. A status variable is published -
     *    MQTT telemetry and the Modbus TCP slave - so its name and its scaling
     *    are an external contract. A computed value is HMI-only and can be
     *    renamed freely.
     *
     * Local I/O is not a separate scope: a local row is one whose Modbus device
     * address is 0. That kept the enum from doubling to say a thing the binding
     * already says.
     */
    enum VarScope : uint8_t {
        SCOPE_A = 0,   ///< alarm: the boolean an AlarmCondition reads
        SCOPE_P,       ///< parameter: persisted, editable
        SCOPE_G,       ///< computed value, HMI only
        SCOPE_SV,      ///< status value, published (MQTT + Modbus TCP slave)
        SCOPE_MI,      ///< direct input, register 1:1 (device 0 = local)
        SCOPE_AI,      ///< analog input, raw count via scale/offset
        SCOPE_DI,      ///< digital input
        SCOPE_MO,      ///< direct output, register 1:1
        SCOPE_AO,      ///< analog output, engineering value via scale/offset
        SCOPE_DO,      ///< digital output
        SCOPE_RO,      ///< relay output (a coil)
        SCOPE_COUNT
    };

    /** @brief Per-variable behaviour bits. */
    enum VarFlags : uint8_t {
        F_INVERT  = 1u << 0,   ///< active-low wiring, folded in so logic reads true = asserted
        F_BOUNDED = 1u << 1,   ///< min/max are a real band, not "unset"
        F_RDONLY  = 1u << 2    ///< a parameter shown but not editable
    };

    /** @brief How a driver reaches a variable.
     *
     *  @c wire is the type ON THE WIRE, deliberately distinct from the
     *  variable's own VarType: a Modbus REAL feeding an f32, or a raw u32 count
     *  feeding an f32 engineering value via `value = wire * scale + offset`. The
     *  predecessor conflated the two in a single `raw` field. */
    struct Binding {
        Transport transport;
        VarType   wire;
        uint8_t   device;      ///< Modbus unit id / expansion module
        uint8_t   fc;          ///< Modbus function code
        uint16_t  reg;         ///< register address, coil, or local channel
        uint16_t  regs;        ///< registers spanned (2 for a 32-bit value)
        float     scale, offset;
    };

    /** @brief A numeric row's band. Two jobs by role: a parameter's EDIT BOUNDS
     *  and an input's PLAUSIBILITY BAND (a reading outside it is implausible - a
     *  disconnected sensor usually reads a rail - and marks the variable invalid
     *  rather than feeding rubbish downstream). Meaningful only with
     *  F_BOUNDED. */
    struct NumBand { Scalar min, max; };

    /** @brief What a T_STR row DECLARES about its pool slot: how big it is and
     *  what a new value is checked against.
     *
     *  Not where it sits. The offset used to be written per row in the site's
     *  table, which meant hand-adding STR_CAP each time and re-typing every
     *  offset after it when a text parameter was inserted. Bank::init() lays the
     *  slots out instead, in table order, and puts the result in the Value - so
     *  there is one fewer number a site can get wrong, and it is the one that
     *  silently overlapped two settings when it was wrong. */
    struct TextSlot {
        uint16_t cap;    ///< capacity including the NUL; 0 = no slot
        StrKind  kind;   ///< edit validation
    };

    /** @brief An alarm row's announce policy: the short display code plus the
     *  dwell thresholds the AlarmModel runs it on.
     *
     *  Seconds as int16 rather than int32 because that is what fits: it keeps
     *  the arm inside the 16 bytes NumBand already costs, so carrying the alarm
     *  policy in the variable table is free. Nine hours of dwell is past any
     *  plausible setting, and the sentinels (-1 = never announce, recent_s 0 =
     *  the default window) are unaffected. */
    struct AlarmSpec {
        const char* code;         ///< short display code, max 8 chars
        int16_t     on_s;         ///< time ON before announcing (-1: off)
        int16_t     off_s;        ///< time OFF before announcing (-1: off)
        int16_t     recent_s;     ///< RECENT window (0: default, <0: forever)
        int16_t     off_delay_s;  ///< hold ON this long after the raw clears
        int16_t     min_on_s;     ///< minimum ON time from onset
    };

    /** @brief A number a row declares: a fixed value, or the parameter that
     *  holds it.
     *
     *  Calibration is the operator's, not the firmware's: a divider ratio or a
     *  4-20 mA span is measured on the bench and changes when the hardware does,
     *  so pinning it in flash means a rebuild to correct a sensor. Pointing the
     *  field at a parameter lets it be corrected from the PARAMS page.
     *
     *  @c k is not merely the alternative - it is also the FALLBACK. A named
     *  parameter that does not exist reads as @c k, so a mistyped name degrades
     *  to the declared constant rather than to zero (and the bank counts the
     *  miss, which the site test asserts is never). */
    struct Tunable {
        float       k;      ///< the value, when @c param is null or absent
        const char* param;  ///< the parameter to read instead (null = fixed)
    };

    /** @brief One analog channel's six tunables: the two-point calibration plus
     *  the plausibility band.
     *
     *  The endpoints say how the wire maps to engineering units - raw_min/max in
     *  the WIRE's unit (mV, uA), eng_min/max in the variable's. @c scale and
     *  @c offset are deliberately not stored: they are derived from the
     *  endpoints, so a recalibration takes effect the moment the parameter
     *  commits and there is no second copy to update.
     *
     *  min/max are a separate pair on purpose. The endpoints are where the
     *  sensor was calibrated; the band is what counts as a believable reading,
     *  and it is usually wider - a value just past the top of the calibrated
     *  span is still a measurement, not a dead sensor.
     *
     *  Blocks are SHARED by pointer: two identical dividers name one block, and
     *  then they share its parameters too, which is what "identical" should
     *  mean. Give a channel its own block the moment it needs its own trim. */
    struct AnalogCal {
        Tunable raw_min, raw_max;   ///< wire units at the two calibration points
        Tunable eng_min, eng_max;   ///< engineering units at those same points
        Tunable min, max;           ///< plausibility band, engineering units
    };

    /** @brief Which arm of Payload a row is using. Three kinds of row, three
     *  shapes, one question - asked here rather than open-coded, so a new arm
     *  cannot be added without every reader seeing it. */
    enum PayloadArm : uint8_t { PAY_NUM, PAY_TEXT, PAY_ALARM, PAY_ANALOG };

    /** @brief The half of a row that depends on whether it holds text.
     *
     *  A band is meaningless for a string, a pool slot is meaningless for a
     *  number, and an alarm wants neither - it wants its announce policy. No
     *  two ever coexist, so one slot serves all three. The discriminant is
     *  already on the row: see payload_arm().
     *
     *  Constexpr constructors rather than an aggregate, because C++11 aggregate
     *  initialisation can only reach a union's FIRST member - the same reason
     *  Scalar has them (see scalar.h). Reach the arms through num_band() /
     *  text_slot(), never directly: those check the discriminant, and a T_STR
     *  read as a band would return two pointers-as-doubles, while a number read
     *  as a slot would hand `copy_bounded` an arbitrary pool offset to write
     *  through. */
    union Payload {
        NumBand   num;
        TextSlot  text;
        AlarmSpec alarm;
        /** @brief Borrowed, and shareable: several rows may name one block.
         *  A pointer rather than the block itself because six Tunables are 48
         *  bytes and this union is 16 - inlining them would grow EVERY row to
         *  serve the handful that are analog. */
        const AnalogCal* cal;
        /// An unset band: what a row with no meaningful bounds carries.
        constexpr Payload() : num{Scalar(), Scalar()} {}
        constexpr Payload(Scalar lo, Scalar hi) : num{lo, hi} {}
        constexpr Payload(uint16_t cap, StrKind kind) : text{cap, kind} {}
        constexpr Payload(const char* code, int16_t on_s, int16_t off_s, int16_t recent_s,
                          int16_t off_delay_s, int16_t min_on_s)
            : alarm{code, on_s, off_s, recent_s, off_delay_s, min_on_s} {}
        constexpr Payload(const AnalogCal* c) : cal(c) {}
    };

    /** @brief Every variable, whatever its kind.
     *
     *  Field order is load-bearing: @c pay is 8-aligned, so putting the 4-byte
     *  @c str pointer before it would buy 4 bytes of padding and give the whole
     *  union back. After it, @c str lands in the tail hole and the row is 72
     *  bytes on the M7 rather than 80.
     *
     *  Built through the declare.h factories, not brace-initialised: each scope
     *  has one, and it takes only the arguments that scope can use. */
    struct Variable {
        Scalar      def;        ///< invalid-read fallback / safe state / factory value
        const char* name;       ///< symbolic name; the telemetry leaf; the storage key
        const char* unit;       ///< "C", "%", "" for none
        const char* desc;       ///< description
        const char* notes;      ///< additional notes
        Payload     pay;        ///< band or pool slot, per `type == T_STR`
        Binding     bind;
        uint32_t    timeout_ms;///< ROLE_INPUT: no good read within this -> invalid; 0 = never
        /** @brief T_ENUM renderer; null for a plain number. */
        const char* (*str)(uint64_t);
        VarType     type;
        VarRole     role;
        VarScope    scope;     ///< which kind of row this is; what tables filter on
        uint8_t     digits;    ///< T_F32 display decimals
        uint8_t     flags;     ///< VarFlags
        uint8_t     _rsv[3];
    };

    /** @name Payload access
     *
     *  The only sanctioned way to reach the union. Both check the discriminant,
     *  so a caller that forgets which kind of row it has gets an inert answer
     *  rather than the other arm reinterpreted: an unset band, or a slot of
     *  capacity 0, which every existing writer already treats as "no slot".
     *  @{ */

    /** @brief Which arm @p d's payload holds. An alarm first (it is a T_BOOL,
     *  so the type alone would say "band"), then text, then everything else. */
    constexpr PayloadArm payload_arm(const Variable& d) {
        return (d.scope == SCOPE_A)  ? PAY_ALARM
             : (d.scope == SCOPE_AI ||
                d.scope == SCOPE_AO) ? PAY_ANALOG
             : (d.type == T_STR)     ? PAY_TEXT : PAY_NUM;
    }

    /** @brief @p d's numeric band; unset unless the row actually has one. */
    constexpr NumBand num_band(const Variable& d) {
        return (payload_arm(d) == PAY_NUM) ? d.pay.num : NumBand{Scalar(), Scalar()};
    }
    /** @brief @p d's declared pool slot; capacity 0 for anything but a text
     *  row, which every writer already treats as "no slot". */
    constexpr TextSlot text_slot(const Variable& d) {
        return (payload_arm(d) == PAY_TEXT) ? d.pay.text : TextSlot{0, STR_NONE};
    }
    /** @brief @p d's calibration block; null for anything but SCOPE_AI/AO.
     *  Reading the live numbers out of it needs the bank - see
     *  Vars::analog_values() in params.h. */
    constexpr const AnalogCal* analog_cal(const Variable& d) {
        return (payload_arm(d) == PAY_ANALOG) ? d.pay.cal : nullptr;
    }
    /** @brief @p d's alarm policy; a null code for anything but SCOPE_A. */
    constexpr AlarmSpec alarm_spec(const Variable& d) {
        return (payload_arm(d) == PAY_ALARM) ? d.pay.alarm
                                             : AlarmSpec{nullptr, 0, 0, 0, 0, 0};
    }

    /** @} */

    /** @brief One value slot, parallel to the descriptor table.
     *
     *  No `def` here: the default is a typed literal in flash, so an invalid
     *  read falls back to it without a per-slot copy.
     *
     *  @section value_override The override
     *
     *  A slot carries a second value that, while @c ovr_on, is what every
     *  reader sees instead of @c v - a maintenance tool: force a sensor to test
     *  the logic above it, freeze a computed value, or drive an actuator by
     *  hand. It shadows rather than replaces: the driver keeps publishing into
     *  @c v underneath, so clearing the override snaps straight back to the live
     *  reading with no gap and nothing to restore.
     *
     *  @c ovr_valid is separate because "force this to 20 C" and "make this read
     *  as a dead sensor" are both things you need, and quality is what tells
     *  them apart. Note what it does NOT do: the forced value is still what
     *  raw() returns, so a consumer that gates on valid() sees bad data while
     *  one that does not still reads the number. Forcing the quality is not the
     *  same as simulating the timeout fallback.
     *
     *  Both halves are PERSISTED. An override outlives the session that set it
     *  and now the reboot too, which is a deliberate trade: a maintainer who
     *  forced a signal to work on the plant should not lose it to a power cut,
     *  and the cost is that a forgotten one survives the reboot someone reaches
     *  for when the machine behaves strangely. That is why a restored override
     *  is announced as a CRITICAL at boot and why `override list` says so.
     *  ROLE_OUTPUT is excluded outright - see Bank::load_overrides().
     *
     *  Deliberately RAM-only and never persisted: an override left on across a
     *  reboot is a fault waiting to happen, and a reboot is exactly what an
     *  operator reaches for when a machine is behaving strangely. */
    struct Value {
        Scalar   v;
        Scalar   ovr;        ///< forced value, while ovr_on
        uint64_t stamp_ms;   ///< uptime ms of the last good update
        /** @brief T_STR only: where this row's text sits in the site's pool, or
         *  STR_NO_SLOT when it did not fit. Assigned by Bank::init(), never
         *  declared - see TextSlot. Ahead of the bools because it needs 2-byte
         *  alignment, and behind them it bought a pad byte that pushed the row
         *  past 32. */
        uint16_t str_off;
        bool     valid;
        bool     ovr_on;     ///< an override is in force
        bool     ovr_valid;  ///< what valid() reports while overridden
        uint8_t  _rsv[3];
    };
    // The override costs 8 bytes per variable, in RAM, on every site whether it
    // uses one or not - so it is pinned here rather than left to drift. Adding a
    // field means spending another 8 across the whole table; say so out loud.
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
     *  Not 0, which is a REAL slot - the first text row's. An overflow used to
     *  leave the row at offset 0 while its declared capacity stayed non-zero,
     *  so every writer wrote a full slot's worth on top of that row, and a pool
     *  smaller than a single slot ran off the end of the array entirely. "No
     *  slot" has to be sayable for a writer to be able to refuse. */
    static const uint16_t STR_NO_SLOT = 0xFFFFu;

    /** @} */

    /** @name Handle
     *  @{ */

    /**
     * @brief The one handle: a non-owning (descriptor, value slot) pair,
     * constructed on the fly and passed by value.
     *
     * It costs nothing to make and gives control logic type safety without any
     * offset arithmetic:
     * @code
     * float t = io.var(V_EXT_TEMP).value();
     * io.var(V_DRAIN_VALVE).set(true);
     * @endcode
     */
    class Var {
        const Variable* _d;
        Value*          _v;
    public:
        Var(const Variable* d, Value* v) : _d(d), _v(v) {}

        /** @name Read
         *  An invalid variable reads as its declared default, so control logic
         *  that does not care about quality still gets a sane number, and logic
         *  that does care asks valid().
         *
         *  An override is applied HERE, at the one place every reader goes
         *  through, so forcing a variable reaches control logic, the console,
         *  telemetry and the alarm model alike - there is no path that sees
         *  around it.
         *  @{ */
        Scalar   raw()   const { return _v->ovr_on ? _v->ovr
                                                   : (_v->valid ? _v->v : _d->def); }
        /** @brief As a float; a digital reads 0/1, so a consumer that only
         *  displays or publishes never branches on the type. */
        float    value() const { return as_f32(_d->type, raw()); }
        bool     b()     const { return as_bool(_d->type, raw()); }
        int64_t  i()     const { return as_i64(_d->type, raw()); }
        bool     valid() const { return _v->ovr_on ? _v->ovr_valid : _v->valid; }
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
         *  resolve. An integer literal is deliberately not accepted - it would be
         *  ambiguous; use set_i().
         *  @{ */
        void set(bool v)      { _v->v = from_bool(_d->type, v); _v->valid = true; }
        void set(float v)     { _v->v = from_f32 (_d->type, v); _v->valid = true; }
        void set_i(int64_t v) { _v->v = from_i64 (_d->type, v); _v->valid = true; }

        /** @brief What a DRIVER calls: value, timestamp and validity in one
         *  step, so the staleness clock cannot be forgotten. */
        void publish(Scalar v, uint64_t now_ms) {
            _v->v = v; _v->stamp_ms = now_ms; _v->valid = true;
        }
        /** @brief Drop to the declared safe state (what a fault does). */
        void safe() { _v->valid = false; }
        /** @} */

        const Variable* meta() const { return _d; }
        const char*     name() const { return _d->name; }
        const char*     unit() const { return _d->unit; }
        VarType         type() const { return _d->type; }
        VarRole         role() const { return _d->role; }

    private:
        /** @name Override mutation - Bank's, not yours
         *
         *  Private, and it is load-bearing. An override is PERSISTED, so every
         *  one of these has a storage side to it that only the Bank can reach:
         *  a set that skipped the store would vanish at the next boot, and a
         *  clear that skipped it would come BACK at the next boot. Both of those
         *  were real bugs here, caused by exactly one caller reaching past the
         *  bank because it could. The rule was written in a comment; now it is
         *  written in the access specifier.
         *  @{ */
        friend class Bank;

        /** @brief Force the variable to @p v, reading as @p valid. */
        void override_set(Scalar v, bool valid = true) {
            _v->ovr = v; _v->ovr_valid = valid; _v->ovr_on = true;
        }
        /** @brief Freeze at whatever it reads right now, keeping its quality. */
        void override_freeze() { override_set(raw(), valid()); }
        /** @brief Release the override; the live value takes over again. */
        void override_clear() { _v->ovr_on = false; }
        /** @} */
    };

    /** @} */

    /** @name Table
     *  @{ */

    /** @brief The site's variable table, its value store and its string pool.
     *
     *  Every pointer is borrowed; a site declares them as statics (descriptors
     *  const, values mutable).
     *
     *  No default member initialisers: in C++11 an NSDMI stops this being an
     *  aggregate, which forces a runtime constructor call at the site's
     *  descriptor and drags the whole (otherwise const) SiteDescriptor out of
     *  flash into .data. */
    struct VarTables {
        const Variable* var;
        Value*          val;
        uint16_t        count;
        char*           pool;         ///< T_STR live text
        uint16_t        pool_bytes;
    };

    /** @brief What a write attempt did.
     *
     *  A failed PERSIST is not a rejected EDIT: the value is live either way,
     *  and telling the operator their input was invalid when the storage
     *  refused it sends them to fix the wrong thing. */
    enum SetResult : uint8_t {
        SET_OK,             ///< applied and persisted
        SET_INVALID,        ///< rejected - malformed, out of range, read-only
        SET_NOT_PERSISTED   ///< applied in RAM, but storage refused it
    };

    /** @brief Where a variable's value survives a reboot: a parameter's setting,
     *  and an override. The implementation over HAL::Storage lives in params.h;
     *  the Bank only needs the interface, so a test can hand it a stub.
     *
     *  Overrides are kept under their own keys rather than mixed into the
     *  parameter's, because they are a different fact about the same row: one is
     *  the configured value, the other is a maintainer holding it somewhere
     *  else, and clearing the override must not disturb the setting under it. */
    struct VarStore {
        virtual ~VarStore() {}
        /** @brief Load @p d's stored value into @p v, or into @p text for a
         *  T_STR. @return false when the key is absent, which is not an error:
         *  the caller keeps the declared default.
         *
         *  @p text / @p cap is the row's slot, already resolved and bounded by
         *  the bank - null and 0 for a row that has none. The store is handed a
         *  buffer rather than the pool and an offset on purpose: it has no way
         *  to know how big the pool is, so it had no way to prove its own
         *  arithmetic landed inside it. */
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
        /** @brief Forget @p d's stored override. Called on every release, so a
         *  cleared override cannot come back at the next boot. */
        virtual bool clear_override(const Variable& d) = 0;
    };

    /**
     * @brief What everything is handed to reach the variables.
     *
     * An out-of-range index - VAR_NONE included - resolves to an inert sink
     * rather than faulting or clamping. Clamping was worse than it looked: it
     * aliased a bad index onto the LAST entry, so a typo would drive a real
     * relay. The sink absorbs reads and writes harmlessly, so a mistake shows up
     * as a signal that never moves instead of the wrong actuator firing.
     */
    class Bank {
        VarTables   _t;
        /** @name Parameter block
         *
         *  The ROLE_PARAM rows are one contiguous run, which is what lets the
         *  parameter pages address them with a dense 0-based index and one add.
         *  Found by scanning the table rather than written into the site's
         *  descriptor: a boundary spelled out by hand is a second copy of the
         *  row order, and that is the copy that goes stale.
         *  @{ */
        uint16_t      _param_first = 0;
        uint16_t      _param_count = 0;
        /** @} */
        /** @brief Where ROLE_PARAM writes are persisted (null -> RAM only, and
         *  set() reports SET_NOT_PERSISTED). */
        VarStore*     _store  = nullptr;
        /** @brief The holds currently in force. Starts held by the system:
         *  outputs are not ours to move until bring-up says so. */
        uint8_t       _inhibits = (uint8_t)INHIBIT_HEALTH;
        /** @brief For the one thing the bank must not swallow: a write the
         *  store refused (see set()). May be null. */
        Logs::Logger* _logger = nullptr;
        /** @brief How many name lookups have found nothing since construction.
         *  Variables are addressed by name now, so a typo no longer fails to
         *  compile - it resolves to the sink and reads zero forever. Counting
         *  the misses is what makes that visible: non-zero after bring-up means
         *  a table or a control program names a row that is not in vars.h.
         *  Mutable because find() is const and this is diagnostics, not
         *  state. */
        mutable uint16_t _unresolved = 0;
        /** @name String pool
         *  Filled by init(), which lays the T_STR slots out in table order.
         *  @{ */
        uint16_t      _pool_used     = 0;
        /** @brief Text rows that did not fit. Non-zero means the site's pool is
         *  too small - the rows that missed out have no slot at all, so they
         *  read empty and refuse every write. */
        uint16_t      _pool_overflow = 0;
        /** @} */
    public:
        /** VarTables is a bare aggregate (no member initializers, so a site's
         *  descriptor stays in flash), so value-initialize it here: without the
         *  `()` an unbound bank starts on indeterminate pointers and init()
         *  walks them. */
        Bank() : _t() {}
        explicit Bank(const VarTables& t) : _t(t) { _find_param_block(); }

        /** @brief Bind the parameter store and the log a refused write goes to.
         *  Wired once at bring-up, before load_params(). */
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

        /** @brief Restore the overrides that were in force at the last shutdown.
         *
         *  ROLE_OUTPUT rows are deliberately NOT restored, and any stored one is
         *  dropped: control starts disabled, so a restored actuator override
         *  would sit armed until bring-up enabled control and then fire, from a
         *  console session long since walked away from. That is the same hazard
         *  disable_control() releases overrides to avoid, and a reboot must not
         *  reopen it.
         *
         *  @return how many were restored. Non-zero deserves to be said out
         *  loud: an override explains behaviour nothing else accounts for, and
         *  after a reboot nobody remembers setting it. */
        uint16_t load_overrides();

        const VarTables& tables() const { return _t; }
        char*            pool() const   { return _t.pool; }
        uint16_t         count() const  { return _t.count; }

        /** @brief Resolve an index. Never fails; a bad one lands on the sink. */
        Var var(uint16_t i) const {
            if (i < _t.count) return Var(&_t.var[i], &_t.val[i]);
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

        /** @brief Where @p h's text lives in the pool, or null when it has none
         *  - not a T_STR, no pool bound, or the row found no room at init().
         *
         *  The ONLY way to reach a row's text. Four writers used to compute
         *  `pool + off` themselves and gate on the declared capacity, which is
         *  in flash and says nothing about whether the row actually got a slot.
         *  @p cap_out receives the usable capacity (0 when there is no slot). */
        char* text_at(Var h, uint16_t* cap_out = nullptr) const;

        /** @brief Find a variable by name. Returns VAR_NONE when absent, which
         *  resolves to the sink, so a caller may skip the check - at the cost
         *  of a silent miss, which is what unresolved_count() surfaces. */
        VarRef find(const char* name) const;
        /** @brief Length-bounded form, for a shell argument that is not
         *  NUL-terminated. Operator input, so a miss here is a typo at the
         *  keyboard rather than a build defect: it does NOT count. */
        VarRef find(const char* name, uint16_t len) const;

        /** @brief How many name lookups have found nothing. Zero on a
         *  correctly wired site; the test suite asserts that after resolving
         *  every table and running every control program. */
        uint16_t unresolved_count() const { return _unresolved; }

        /** @brief Pool bytes the text rows actually claimed, and how many rows
         *  found none left. The second must be zero; the site test asserts it,
         *  because a row with no slot reads empty and silently refuses to be
         *  set. */
        uint16_t pool_used() const     { return _pool_used; }
        uint16_t pool_overflow() const { return _pool_overflow; }

        /** @name Control authority
         *
         *  Whether the firmware is allowed to drive the field at all.
         *
         *  Two INDEPENDENT sources can hold it down, and control returns only
         *  when both have let go. That is the whole reason this is a set rather
         *  than a flag: an operator who disabled control at the console must
         *  still be disabled after a health fault comes and goes, and a firmware
         *  that has just recovered must not quietly talk over them.
         *
         *  It starts held by INHIBIT_HEALTH, so a half-constructed firmware
         *  cannot energise an actuator between power-on and the point bring-up
         *  says it is ready.
         *  @{ */

        /** @brief Who is holding control down. Control is enabled only when the
         *  set is empty. */
        enum ControlInhibit : uint8_t {
            INHIBIT_OPERATOR = 1u << 0,  ///< `control disable` at the console
            INHIBIT_HEALTH   = 1u << 1,  ///< not yet brought up, health fault,
                                         ///< or a reset we have decided to take
        };

        /** @brief Release @p source's hold. The field becomes drivable again
         *  only if nothing else is still holding it. */
        void enable_control(ControlInhibit source = INHIBIT_OPERATOR);

        /** @brief Have @p source withhold control: drop every ROLE_OUTPUT to its
         *  declared safe state, release any override holding one, and refuse to
         *  take new ones until control returns.
         *
         *  This outranks a maintenance override, never the other way round: an
         *  override is for a machine someone is standing at and watching, while
         *  this runs when the firmware can no longer reason about what it is
         *  driving (or is about to stop running at all). An actuator held
         *  energised by hand through that is the one outcome the safe state
         *  exists to prevent.
         *
         *  Outputs only. An internal is derived state - an alarm condition, a
         *  latched interlock - and clearing it would erase the very diagnosis
         *  that got us here. A parameter is configuration, and sweeping it would
         *  revert the network settings on a scan overrun. */
        void disable_control(ControlInhibit source = INHIBIT_OPERATOR);

        /** @brief Whether the field may be driven (the scan is gated on it). */
        bool control_enabled() const { return _inhibits == 0; }

        /** @brief True while @p source is one of the holds in force - what lets
         *  the console say WHY control is off, so an operator whose `control
         *  enable` did not bring it back is told what else is holding it. */
        bool control_inhibited_by(ControlInhibit source) const {
            return (_inhibits & (uint8_t)source) != 0;
        }

        /** @} */

        /** @brief Invalidate ROLE_INPUTs whose last good read is older than
         *  their declared timeout. A timeout of 0 never expires, for a value a
         *  driver publishes only on change. */
        void expire_stale(uint64_t now_ms);

        /** @brief Invalidate any ROLE_INPUT whose reading has left its declared
         *  plausibility band, and any T_F32 that has gone non-finite.
         *
         *  This is what F_BOUNDED was declared FOR on an input, and it went
         *  years unenforced: a shorted 4-20 mA loop reads full scale, which is a
         *  perfectly well-formed number, and every consumer treated it as a
         *  measurement. Marking it invalid instead is what makes the declared
         *  band worth writing - and what makes `def` do its documented job, so a
         *  control program reads the fallback without testing valid() at all.
         *
         *  Non-finite is the same argument in a nastier form: a NaN compares
         *  false against everything, so it does not trip a band, and it makes
         *  every downstream hysteresis and deadband behave oddly rather than
         *  fault. A driver that produces one has failed, so the reading has.
         *
         *  Analog rows are skipped here: their band is operator-editable and
         *  needs the parameter table to resolve, which the bank has no route to.
         *  @return how many readings this pass rejected. */
        uint16_t reject_implausible();

        /** @name Writing
         *
         *  The two ways a human changes a variable, and they are deliberately
         *  different things. set() edits a PARAMETER: validated against its
         *  declared band and persisted, because it is configuration. override()
         *  forces ANY variable: volatile, and range-checked against the type
         *  only - forcing a reading outside its plausibility band is precisely
         *  how you exercise the alarm that watches for it.
         *  @{ */

        /** @brief Apply @p text to parameter @p h and persist it. Rejects a
         *  non-parameter (SET_INVALID): writing a sensor or an output here would
         *  be overwritten by the next scan, which is what override() is for. */
        SetResult set(Var h, const char* text);

        /** @brief Force @p h to @p text until override_clear(). T_STR is
         *  refused (a hostname is configuration, not a field signal), and so is
         *  a ROLE_OUTPUT while control is disabled - accepting one there would
         *  quietly stage an actuator command to fire the moment control
         *  returned. */
        SetResult override_set(Var h, const char* text, bool valid = true);

        /** @brief The same, with the value already typed - for a caller that has
         *  a Scalar rather than text to parse. Forcing only the QUALITY is
         *  `override_set(h, h.raw(), false)`: it goes through here so the
         *  changed quality is persisted like any other override. */
        void override_set(Var h, Scalar v, bool valid = true);

        /** @brief Force @p h to whatever it reads right now, keeping its current
         *  validity - for pinning a signal while you work on what depends on
         *  it. Persists, like any other override. */
        void override_freeze(Var h);
        /** @brief Release @p h's override, and forget the stored copy. Always go
         *  through the bank rather than Var::override_clear(), which cannot
         *  reach the store and would leave one to come back at the next boot. */
        void override_clear(Var h);

        /** @brief Release every override in one go (what a maintainer wants
         *  after a session, and what init() does). @return how many were on. */
        uint16_t clear_overrides();

        /** @brief Variables currently overridden - surfaced so a forgotten
         *  override cannot hide. */
        uint16_t override_count() const;

        /** @} */

    private:
        /** @brief @p h's row index, or VAR_NONE when it points at the sink. */
        uint16_t _row_of(Var h) const;
        /** @brief Write @p h's override through to the store (never for a
         *  ROLE_OUTPUT - see load_overrides()). */
        void _persist_override(Var h);

        /** @brief Locate the ROLE_PARAM run: first such row, then how many
         *  follow it without a break. A parameter outside that run is a table
         *  defect (the pages address the block by offset); it is simply not
         *  reachable as a parameter, and the site test asserts there is none. */
        void _find_param_block();

        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access, so a write to one bad index is never visible to the next
         *  reader - the sink is scratch, not shared state. */
        static const Variable _sink_d;
        static Value          _sink_v;
    };

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
