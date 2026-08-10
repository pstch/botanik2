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
 * There is ONE descriptor, one value struct and one handle: "analog versus
 * digital" is a field to read, not a type to dispatch on.
 *
 * @section model_split Descriptor and value
 *
 * A variable is split in two: its **descriptor** is `const` and lives in flash,
 * its **value** in a parallel RAM array indexed the same way.
 *
 * Plain aggregates only, no constructors and no virtuals: a type with a vptr is
 * not a literal type in C++11 and would land the whole table in .bss behind a
 * startup initialiser. The Scalar members are the one exception - see scalar.h.
 *
 * @section model_default The default does three jobs
 *
 * @c def is an input's substitute when a read is invalid, an output's safe state
 * on a fault, and a parameter's factory value. One field, three uses. It is a
 * typed literal, not text, so a bad default is a compile error.
 */

#include <cstdint>                  // uint8_t/uint16_t/int64_t

#include "libbotanik/vars/scalar.h" // Vars::Scalar / VarType

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
     * `Transport` and `VarRole` are the axes BEHAVIOUR turns on. This is the
     * third thing a reader wants and neither answers: which concrete kind of row
     * this is. Declared, never reconstructed from (transport, role, type), so a
     * table filters on one comparison.
     *
     * Each value has exactly one factory in declare.h, named after it, taking
     * only the arguments its scope can use.
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
     * address is 0.
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

    /** @brief WHY a value reads as it does.
     *
     *  Three quite different things invalidate a reading: nothing ever wrote the
     *  row, it was live and stopped, or it is answering with nonsense. Written
     *  by whichever path invalidated the row, so it is a diagnosis rather than a
     *  guess made at display time.
     *
     *  @warning Q_NO_DATA is 0 deliberately: a zero-initialised Value - which is
     *  how every site's value store starts, and what the sink is - must read as
     *  "nothing has written this", never as good data. */
    enum Quality : uint8_t {
        Q_NO_DATA = 0,   ///< nothing has ever written it; reads its declared default
        Q_GOOD,          ///< a real value from whoever owns the row
        Q_STALE,         ///< it was live and stopped: no good read within its timeout
        Q_IMPLAUSIBLE,   ///< answering with nonsense: outside its band, or non-finite
        Q_SAFE,          ///< an output dropped to its declared safe state
        Q_REFUSED        ///< the device answered, and declined: it has no such register
    };

    /** @brief @p q as a phrase for an operator: "valid", or invalid and why. */
    inline const char* quality_str(Quality q) {
        switch (q) {
            case Q_GOOD:        return "valid";
            case Q_NO_DATA:     return "invalid: nothing has written it";
            case Q_STALE:       return "invalid: past its timeout";
            case Q_IMPLAUSIBLE: return "invalid: outside its band";
            case Q_SAFE:        return "invalid: dropped to its safe state";
            case Q_REFUSED:     return "invalid: the device refused the read";
            default:            return "invalid";
        }
    }

    /** @brief Per-variable behaviour bits. */
    enum VarFlags : uint8_t {
        F_INVERT  = 1u << 0,   ///< active-low wiring, folded in so logic reads true = asserted
        F_BOUNDED = 1u << 1,   ///< min/max are a real band, not "unset"
        F_RDONLY  = 1u << 2,   ///< a parameter shown but not editable
        /** @brief A 32-bit value's registers arrive low word first.
         *
         *  Registers are big-endian on the wire with no vendor freedom; only
         *  which half of a PAIR carries the high word varies, by device model.
         *  Meaningless unless `bind.regs` is 2. */
        F_WORD_SWAP = 1u << 3,
        /** @brief Keep this row's value across a reboot (IEC 61131-3 RETAIN).
         *
         *  For the rows whose value IS the history: litres delivered, hours
         *  run, cycles counted. Only `ROLE_COMPUTED` rows are retained
         *  (@see Bank::retainable()).
         *
         *  Saved on a cadence and on the way down, never on the write: a
         *  totalizer moves every beat, and a flash write per beat would wear the
         *  part out in days. @see Bank::save_retained() */
        F_RETAIN = 1u << 4
    };

    /** @brief Per-slot state bits, as opposed to the row's declared behaviour.
     *
     *  One byte carved out of Value's spare three, so the slot stays 32 bytes. */
    enum ValueFlags : uint8_t {
        /** @brief This output's demand has changed and has not reached the field.
         *
         *  Set on every write that moves a ROLE_OUTPUT, cleared by the driver
         *  once a transaction has carried it. */
        VF_FIELD_DIRTY = 1u << 0,
        /** @brief This row has had more than one control program write it.
         *  Latched, so the count is of ROWS with two authors rather than of
         *  per-beat collisions. @see Bank::claim_writer() */
        VF_TWO_WRITERS = 1u << 1,
        /** @brief This output's override is in force but must not reach the
         *  field: CONTROL_DISABLED. The override is NOT erased, so leaving that
         *  mode restores it. ROLE_OUTPUT only, and maintained by the Bank alone.
         *  @see Bank::set_control_mode() */
        VF_OVR_MUTED   = 1u << 2
    };

    /** @brief What the firmware may drive, strictest first. The order is
     *  load-bearing: the effective mode is the MINIMUM of the operator's
     *  choice and what the system's hold allows. */
    enum ControlMode : uint8_t {
        /** @brief Outputs safe, and an output override does NOT reach the
         *  field. The forced value is muted, not erased, so restoring a less
         *  strict mode brings it back - unless the SYSTEM withdrew control,
         *  which erases it (see set_health_hold()). */
        CONTROL_DISABLED  = 0,
        /** @brief Outputs safe and the programs stopped, but an override
         *  still drives. This is how you take the plant by hand: with the
         *  programs stopped, an override is the only thing left that can
         *  move an actuator. */
        CONTROL_INHIBITED = 1,
        /** @brief Normal operation. */
        CONTROL_ENABLED   = 2
    };

    /** @brief How a driver reaches a variable.
     *
     *  @c wire is the type ON THE WIRE, deliberately distinct from the
     *  variable's own VarType: a Modbus REAL feeding an f32, or a raw u32 count
     *  feeding an f32 engineering value via `value = wire * scale + offset`. */
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
     *  Not where it sits: Bank::init() lays the slots out in table order and
     *  puts the offset in the Value, so it is derived and never declared. */
    struct TextSlot {
        uint16_t cap;    ///< capacity including the NUL; 0 = no slot
        StrKind  kind;   ///< edit validation
    };

    /** @brief An alarm row's announce policy: the short display code plus the
     *  dwell thresholds the AlarmController runs it on.
     *
     *  Seconds as int16 because that is what fits: it keeps the arm inside the
     *  16 bytes NumBand already costs. Nine hours of dwell is past any plausible
     *  setting, and the sentinels (-1 = never announce, recent_s 0 = the default
     *  window) are unaffected. */
    struct AlarmSpec {
        const char* code;         ///< short display code (max 9: AlarmsPage::CODE_W)
        int16_t     on_s;         ///< time ON before announcing (-1: off)
        int16_t     off_s;        ///< time OFF before announcing (-1: off)
        int16_t     recent_s;     ///< RECENT window (0: default, <0: forever)
        int16_t     hold_on_s;  ///< hold ON this long after the raw clears
        int16_t     min_on_s;     ///< minimum ON time from onset
    };

    /** @brief A number a row declares: a fixed value, or the parameter that
     *  holds it.
     *
     *  Pointing a field at a parameter is what lets a calibration be re-trimmed
     *  from the PARAMS page without a rebuild.
     *
     *  @c k is also the FALLBACK: a named parameter that does not exist reads as
     *  @c k, so a mistyped name degrades to the declared constant rather than to
     *  zero (and the bank counts the miss, which the site test asserts is
     *  never). */
    struct Tunable {
        float       k;      ///< the value, when @c param is null or absent
        const char* param;  ///< the parameter to read instead (null = fixed)
    };

    /** @brief One analog channel's six tunables: the two-point calibration plus
     *  the plausibility band.
     *
     *  The endpoints say how the wire maps to engineering units - raw_min/max in
     *  the WIRE's unit (mV, uA), eng_min/max in the variable's. @c scale and
     *  @c offset are never stored: they are derived from the endpoints, so a
     *  recalibration takes effect the moment the parameter commits.
     *
     *  min/max are a separate pair: the endpoints are where the sensor was
     *  calibrated, the band is what counts as a believable reading, and it is
     *  usually wider.
     *
     *  Blocks are SHARED by pointer: two identical dividers name one block, and
     *  then share its parameters too. Give a channel its own block the moment it
     *  needs its own trim. */
    struct AnalogCal {
        Tunable raw_min, raw_max;   ///< wire units at the two calibration points
        Tunable eng_min, eng_max;   ///< engineering units at those same points
        Tunable min, max;           ///< plausibility band, engineering units
    };

    /** @brief Which arm of Payload a row is using. The one place the question is
     *  answered; nothing open-codes it. */
    enum PayloadArm : uint8_t { PAY_NUM, PAY_TEXT, PAY_ALARM, PAY_ANALOG };

    /** @brief The half of a row that depends on whether it holds text.
     *
     *  A band is meaningless for a string, a pool slot for a number, and an
     *  alarm wants neither. No two ever coexist, so one slot serves all four.
     *  The discriminant is on the row: see payload_arm().
     *
     *  Constexpr constructors rather than an aggregate, because C++11 aggregate
     *  initialisation can only reach a union's FIRST member (same reason Scalar
     *  has them, see scalar.h).
     *
     *  @warning Reach the arms through num_band() / text_slot() / analog_cal() /
     *  alarm_spec(), never directly: those check the discriminant. A T_STR read
     *  as a band returns two pointers-as-doubles, and a number read as a slot
     *  hands `copy_bounded` an arbitrary pool offset to write through. */
    union Payload {
        NumBand   num;
        TextSlot  text;
        AlarmSpec alarm;
        /** @brief Borrowed, and shareable: several rows may name one block. A
         *  pointer because six Tunables are 48 bytes and this union is 16. */
        const AnalogCal* cal;
        /// An unset band: what a row with no meaningful bounds carries.
        constexpr Payload() : num{Scalar(), Scalar()} {}
        constexpr Payload(Scalar lo, Scalar hi) : num{lo, hi} {}
        constexpr Payload(uint16_t cap, StrKind kind) : text{cap, kind} {}
        constexpr Payload(const char* code, int16_t on_s, int16_t off_s, int16_t recent_s,
                          int16_t hold_on_s, int16_t min_on_s)
            : alarm{code, on_s, off_s, recent_s, hold_on_s, min_on_s} {}
        constexpr Payload(const AnalogCal* c) : cal(c) {}
    };

    /** @brief Every variable, whatever its kind.
     *
     *  @warning Field order is load-bearing: @c pay is 8-aligned, so @c str must
     *  stay AFTER it, where it lands in the tail hole (72 bytes on the M7).
     *  Ahead of it, the padding costs 8 more.
     *
     *  Built through the declare.h factories, not brace-initialised. */
    struct Variable {
        Scalar      def;        ///< invalid-read fallback / safe state / factory value
        const char* name;       ///< symbolic name; the telemetry leaf; the storage key
        const char* unit;       ///< "C", "%", "" for none
        const char* desc;       ///< description
        const char* notes;      ///< additional notes
        Payload     pay;        ///< band or pool slot, per `type == T_STR`
        Binding     bind;
        uint32_t    timeout_ms;///< ROLE_INPUT: no good read within this -> invalid; 0 = never
        /** @brief How often to ASK for this row, in ms; 0 derives it from
         *  @c timeout_ms.
         *
         *  Two different questions, kept apart. The timeout is a CONTRACT with
         *  whatever reads the row ("believe this for 15 s and no longer"); the
         *  poll interval is a COST on the bus. A meter can say "poll me every
         *  10 s, call me stale at 15".
         *
         *  16 bits because the poll ladder tops out at 60 s, and it lands in
         *  padding after @c timeout_ms, so the descriptor costs no more. */
        uint16_t    poll_ms;
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

    /**
     * @brief Is @p v a believable reading for @p d? Well-formedness, then the
     *        declared band.
     *
     * Non-finite first (a NaN is not in any range, and saying so as "out of
     * band" would send an operator to look at a limit that is fine), then
     * `F_BOUNDED` against the descriptor's own band.
     *
     * Rows carrying their band in the PARAMETER table (`SCOPE_AI`/`AO`) are NOT
     * judged here - `num_band()` reads unset for them, which would reject
     * everything. Those are checked by their driver, which can resolve the
     * calibration; see `AnalogValues::plausible()`.
     *
     * One predicate because two callers must agree about it:
     * `Bank::reject_implausible()` sweeping the table, and the field-bus driver
     * judging a row the moment it commits it.
     */
    bool value_implausible(const Variable& d, const Scalar& v);

    /** @} */

    /** @brief One value slot, parallel to the descriptor table.
     *
     *  No `def` here: the default is a typed literal in flash, so an invalid
     *  read falls back to it without a per-slot copy.
     *
     *  @section value_override The override
     *
     *  While @c ovr_on, @c ovr is what every reader sees instead of @c v. It
     *  SHADOWS: the driver keeps publishing into @c v underneath, so clearing
     *  snaps back to live with nothing to restore. @c ovr_valid is separate
     *  because "force this to 20 C" and "make this read as a dead sensor" are
     *  different jobs.
     *
     *  @warning Both halves are PERSISTED, so a forgotten override survives the
     *  reboot someone reaches for when the machine behaves strangely - which is
     *  why each restore is announced as a CRITICAL.
     *  @see Bank::load_overrides() */
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
        /** @brief The live value's quality - valid, or invalid and why.
         *  @see Quality */
        Quality  qual;
        bool     ovr_on;     ///< an override is in force
        bool     ovr_valid;  ///< what valid() reports while overridden
        uint8_t  vflags;     ///< ValueFlags
        /** @brief Which control program last claimed this row, as its index in
         *  the site's program table; WRITER_NONE until one does.
         *
         *  Answers two questions: which program computes a row (the MONITOR
         *  page shows it), and whether TWO of them drive one output - a defect
         *  that otherwise shows up only as an actuator chattering at the beat
         *  rate. */
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

    /**
     * @brief The one handle: a non-owning (descriptor, value slot) pair,
     * constructed on the fly and passed by value.
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
         *  An override is applied HERE, the one place every reader goes
         *  through, so it reaches control logic, console, telemetry and the
         *  alarm model alike: no path sees around it.
         *  @{ */
        /** @brief What this row reads, override applied. @see _under() for
         *  the fallback rule. */
        Scalar   raw()   const { return _forced() ? _v->ovr : _under(); }
        /** @brief As a float; a digital reads 0/1, so a consumer that only
         *  displays or publishes never branches on the type. */
        float    value() const { return as_f32(_d->type, raw()); }
        bool     b()     const { return as_bool(_d->type, raw()); }
        int64_t  i()     const { return as_i64(_d->type, raw()); }
        bool     valid() const { return _forced() ? _v->ovr_valid : live_valid(); }
        /** @name Under the override
         *
         *  What the row would read with nothing forced: the driver's last value
         *  (or the declared default, when nothing has written it) and its real
         *  quality. Diagnostics only - control logic reads raw()/valid() and is
         *  meant to see the forced value. `get` shows both, so an operator can
         *  see whether a sensor reads sane again before releasing an override.
         *  @{ */
        Scalar   live()       const { return _under(); }
        bool     live_valid() const { return _v->qual == Q_GOOD; }
        /** @brief ...and WHY, when it is not good. An override does not change
         *  it: the row underneath keeps whatever quality it had. */
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
         *  resolve. An integer literal is deliberately not accepted - it would be
         *  ambiguous; use set_i().
         *  @{ */
        void set(bool v)      { _drive(from_bool(_d->type, v)); }
        void set(float v)     { _drive(from_f32 (_d->type, v)); }
        void set_i(int64_t v) { _drive(from_i64 (_d->type, v)); }

        /** @brief What a DRIVER calls: value, timestamp and validity in one
         *  step, so the staleness clock cannot be forgotten.
         *
         *  Deliberately does NOT mark the field dirty. This is a reading coming
         *  IN from the wire; marking it would ask the driver to write back what
         *  it just read. */
        void publish(Scalar v, uint64_t now_ms) {
            _v->v = v; _v->stamp_ms = now_ms; _v->qual = Q_GOOD;
        }
        /** @brief The quantity, without claiming anything has MEASURED it.
         *
         *  For a simulated field, which IS the device rather than a reader of
         *  it: stamping and marking Q_GOOD here would keep the row good for
         *  ever, so no timeout could ever catch a device going quiet.
         *
         *  Value only. Quality and staleness stay the business of whatever
         *  actually reads the wire. */
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

        const Variable* meta() const { return _d; }
        const char*     name() const { return _d->name; }
        const char*     unit() const { return _d->unit; }
        VarType         type() const { return _d->type; }
        VarRole         role() const { return _d->role; }

    private:
        /** @name Override mutation - Bank's, not yours
         *
         *  @warning Private, and load-bearing. An override is PERSISTED, so
         *  each of these has a storage side only the Bank can reach: a set that
         *  skipped the store would vanish at the next boot, and a clear that
         *  skipped it would come BACK at the next boot.
         *  @{ */
        friend class Bank;

        /** @brief Force the variable to @p v, reading as @p valid.
         *
         *  Marks the field dirty, like any other write: an operator forcing an
         *  actuator by hand expects it to move, and the collector reads raw()
         *  so that it does. */
        void override_set(Scalar v, bool valid = true) {
            _v->ovr = v; _v->ovr_valid = valid; _v->ovr_on = true; _touch_field();
        }
        /** @brief Freeze at whatever it reads right now, keeping its quality. */
        void override_freeze() { override_set(raw(), valid()); }
        /** @brief Release the override; the live value takes over again.
         *
         *  Marks the field dirty too: a release that was never written back
         *  would leave the actuator wherever the maintainer left it. */
        void override_clear() { _v->ovr_on = false; _touch_field(); }
        /** @} */

        /** @brief Take a new demand, and owe it to the field only if it MOVED.
         *
         *  A control law re-states its whole demand every activation, so
         *  dirtying on each one leaves every output permanently owed - and a
         *  pending write outranks a due read, so the readings starve. Q_SAFE
         *  counts as moved: the fault path put the row somewhere known, so it is
         *  not showing its demand. */
        void _drive(Scalar v) {
            const bool moved = (_v->v.u != v.u) || (_v->qual != Q_GOOD);
            _v->v    = v;
            _v->qual = Q_GOOD;
            if (moved) _touch_field();
        }

        /** @brief What the row reads with no override in force: the ONE
         *  statement of the fallback rule.
         *
         *  A STALE row keeps the value it actually had: a sensor that answered a
         *  minute ago still knows what the plant was doing, and 20 C from a
         *  minute ago is a better account of a greenhouse than its declared
         *  default. Q_NO_DATA, Q_SAFE and Q_IMPLAUSIBLE fall back: nothing to
         *  keep, a deliberate safe state, and a number already rejected for
         *  being wrong.
         *
         *  Shared by raw() and live() so the two cannot drift. */
        Scalar _under() const {
            return (_v->qual == Q_GOOD || _v->qual == Q_STALE) ? _v->v : _d->def;
        }

        /** @brief Whether the override actually reaches this reader. In force
         *  and not muted: CONTROL_DISABLED mutes an OUTPUT's override without
         *  erasing it, so the field sees the safe state while the forced value
         *  waits for control to come back. Every other role is unaffected -
         *  muting a forced sensor would erase the diagnosis. */
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
        /** @brief Scratch for the name index: @c count row numbers, which
         *  Bank's constructor sorts by name (see Bank::find()). Borrowed and
         *  exactly sized, like the value store.
         *
         *  Optional: a null one leaves find() walking the table, which is what a
         *  small test fixture wants and what a real site must not ship. */
        uint16_t*       index;
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
     *  Overrides are kept under their own keys, not mixed into the parameter's:
     *  they are a different fact about the same row, and clearing the override
     *  must not disturb the setting under it. */
    struct VarStore {
        virtual ~VarStore() {}
        /** @brief Load @p d's stored value into @p v, or into @p text for a
         *  T_STR. @return false when the key is absent, which is not an error:
         *  the caller keeps the declared default.
         *
         *  @p text / @p cap is the row's slot, already resolved and bounded by
         *  the bank - null and 0 for a row that has none. A buffer rather than
         *  the pool and an offset: the store cannot know the pool's size, so it
         *  could not prove its own arithmetic landed inside it. */
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

        /** @brief Load @p d's RETAINED value (F_RETAIN). @return false when
         *  none is stored - a first boot, or a row that has never been saved.
         *
         *  Its own key, for the same reason an override has one: a row can have
         *  both, and they are different facts. */
        virtual bool load_retained(const Variable& d, Value& v) = 0;
        /** @brief Persist @p d's current value as its retained one. */
        virtual bool save_retained(const Variable& d, const Value& v) = 0;
    };

    /**
     * @brief What everything is handed to reach the variables.
     *
     * An out-of-range index - VAR_NONE included - resolves to an inert sink,
     * never faulting and never CLAMPING: clamping would alias a bad index onto
     * a real row, so a typo could drive an actual relay. The sink absorbs reads
     * and writes harmlessly, so a mistake shows up as a signal that never moves.
     */
    class Bank {
        VarTables   _tables;
        /** @name Parameter block
         *
         *  The ROLE_PARAM rows MUST be one contiguous run: that is what lets the
         *  parameter pages address them with a dense 0-based index and one add.
         *  Found by scanning the table, never declared. @see _find_param_block()
         *  @{ */
        uint16_t      _param_first = 0;
        uint16_t      _param_count = 0;
        /** @} */
        /** @brief Where ROLE_PARAM writes are persisted (null -> RAM only, and
         *  set() reports SET_NOT_PERSISTED). */
        VarStore*     _store  = nullptr;
        /** @brief The operator's standing choice. Survives a health fault
         *  coming and going, which is the point of keeping it separate. */
        ControlMode   _op_mode = CONTROL_ENABLED;
        /** @brief The system's own hold: not yet brought up, a health fault, or
         *  a reset we have decided to take. Starts SET - outputs are not ours to
         *  move until bring-up says so. */
        bool          _health_hold = true;
        /** @brief For the one thing the bank must not swallow: a write the
         *  store refused (see set()). May be null. */
        Logs::Logger* _logger = nullptr;
        /** @brief How many name lookups have found nothing since construction.
         *  A typo resolves to the sink and reads zero forever, so counting the
         *  misses is what makes it visible: non-zero after bring-up means a
         *  table or a control program names a row that is not in vars.h.
         *  Mutable because find() is const and this is diagnostics. */
        mutable uint16_t _unresolved = 0;
        /** @brief Rows written by more than one program, and the first such row.
         *  @see claim_writer() */
        uint16_t _conflicts   = 0;
        VarRef   _conflict_row = { VAR_NONE };
        /** @brief Whether VarTables::index holds a usable sorted mapping. False
         *  when the site supplied no scratch, and find() then walks the table.
         *  @see _build_index() */
        bool          _indexed = false;
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
        /** VarTables has no member initializers (so a site's descriptor stays in
         *  flash); the `()` is required, or an unbound bank starts on
         *  indeterminate pointers that init() then walks. */
        Bank() : _tables() {}
        explicit Bank(const VarTables& t) : _tables(t) {
            _find_param_block();
            // Built here, not in init(): the table is const, and a find() before
            // init() (a site test, a fixture) then gets the fast path too.
            _build_index();
        }

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
         *  Every role, ROLE_OUTPUT included: an override means one thing
         *  whatever row it is on, "a person has taken this row by hand".
         *
         *  @warning A restored output override is DRIVING the plant from this
         *  moment, so each restore is announced as a CRITICAL naming the row.
         *  That announcement is the safeguard; do not quiet it.
         *
         *  @return how many were restored. */
        uint16_t load_overrides();

        /** @name Retained values (F_RETAIN)
         *
         *  The rows whose value IS the history - litres delivered, hours run.
         *  @{ */

        /** @brief Whether @p d is a row this bank will retain.
         *
         *  `F_RETAIN` on a `ROLE_COMPUTED` row, and nothing else. A parameter
         *  already persists, an input is overwritten by the next scan, and an
         *  output MUST come back at its declared safe state: restoring a coil
         *  from flash would energise a contactor before anything had looked at
         *  the plant. One predicate, so load and save cannot disagree. */
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

        const VarTables& tables() const { return _tables; }
        char*            pool() const   { return _tables.pool; }
        uint16_t         count() const  { return _tables.count; }

        /** @brief Resolve an index. Never fails; a bad one lands on the sink. */
        Var var(uint16_t i) const {
            if (i < _tables.count) return Var(&_tables.var[i], &_tables.val[i]);
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
         *  The ONLY way to reach a row's text: the declared capacity is in
         *  flash and says nothing about whether the row actually got a slot.
         *  @p cap_out receives the usable capacity (0 when there is no slot). */
        char* text_at(Var h, uint16_t* cap_out = nullptr) const;

        /** @brief Find a variable by name. Returns VAR_NONE when absent, which
         *  resolves to the sink, so a caller may skip the check - at the cost
         *  of a silent miss, which is what unresolved_count() surfaces.
         *
         *  A binary search over the mapping VarTables::index carries, so a
         *  lookup is ~8 comparisons rather than the whole table. See
         *  _build_index(). */
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

        /** @brief Record @p who as the writer of @p r, and report whether that
         *  is a change of author.
         *
         *  First claim wins and is silent. A SECOND program writing the same row
         *  is a defect in the site's design, not a race: an output with two
         *  authors gets whichever ran last in the beat, so it chatters at the
         *  scan rate while both keep insisting. Latched rather than per-beat,
         *  because two programs on different rungs collide rarely and would
         *  otherwise go unnoticed for however long the rates take to line up.
         *
         *  @return false when @p r already belonged to a different program. The
         *          write still goes through: refusing it would leave the plant
         *          in whatever state the losing program reached.
         */
        bool claim_writer(VarRef r, uint8_t who);

        /** @brief The program that claimed @p r, or WRITER_NONE. */
        uint8_t writer_of(VarRef r) const;

        /** @brief How many ROWS have been written by more than one program, and
         *  the first one that was. Counted rather than logged: the write path
         *  has no logger and runs in the control phase.
         *
         *  Rows, not collisions: two programs go on colliding every beat, while
         *  the number of rows with two authors is fixed by the site's design. */
        uint16_t writer_conflicts() const { return _conflicts; }
        VarRef   first_conflict() const   { return _conflict_row; }

        /** @} */

        /** @brief Whether the name index is in use. False means the site gave
         *  VarTables no index scratch and every find() is a full scan. */
        bool name_index_built() const { return _indexed; }

        /** @brief Pool bytes the text rows actually claimed, and how many rows
         *  found none left. The second must be zero; the site test asserts it,
         *  because a row with no slot reads empty and silently refuses to be
         *  set. */
        uint16_t pool_used() const     { return _pool_used; }
        uint16_t pool_overflow() const { return _pool_overflow; }

        /** @name Control authority

         *  Whether the firmware is allowed to drive the field, in three modes.
         *
         *  Two INDEPENDENT sources decide it and the STRICTER one wins: the
         *  operator's standing choice, and the system's own hold. An operator's
         *  `control disable` must survive a health fault coming and going, and a
         *  recovered firmware must not talk over the operator.
         *
         *  Starts held by the system, so a half-constructed firmware cannot
         *  energise an actuator before bring-up says it is ready.
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
         *  or a reset we have decided to take.
         *
         *  Raising it forces CONTROL_DISABLED whatever the operator asked for,
         *  and unlike the operator's own disable it **erases** every output
         *  override, stored copy included. The two differ because the operator
         *  is present and the fault is not: an actuator held energised by hand
         *  through a fault, or resurrected by the next boot, is exactly what the
         *  safe state exists to prevent. */
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

        /** @brief Invalidate any ROLE_INPUT whose reading has left its declared
         *  plausibility band, and any T_F32 that has gone non-finite.
         *
         *  What F_BOUNDED is declared FOR on an input: a shorted 4-20 mA loop
         *  reads full scale, a well-formed number no timeout catches. Marking it
         *  invalid puts `def` in front of control logic instead.
         *
         *  Non-finite separately, because a NaN compares false against
         *  everything and so trips no band.
         *
         *  Analog rows are skipped here: their band is operator-editable and
         *  needs the parameter table to resolve, which the bank has no route to.
         *  @return how many readings this pass rejected. */
        uint16_t reject_implausible();

        /** @name Writing
         *
         *  Two different things. set() edits a PARAMETER: validated against its
         *  declared band and persisted. override() forces ANY variable:
         *  volatile, and range-checked against the type only, so a reading can
         *  be pushed past its band to exercise the alarm watching for it.
         *  @{ */

        /** @brief Apply @p text to parameter @p h and persist it. Rejects a
         *  non-parameter (SET_INVALID): writing a sensor or an output here would
         *  be overwritten by the next scan, which is what override() is for. */
        SetResult set(Var h, const char* text);

        /** @brief Whether @p v WOULD be accepted, without writing anything.
         *
         *  The admission half of set(), for a caller committing several rows as
         *  one unit: it must know they all pass before changing any of them (the
         *  Modbus slave applies a multi-register write that way). The rules live
         *  here only, so a rule added later cannot be missed on the check. */
        SetResult check(Var h, Scalar v) const;

        /** @brief The same, with the value already typed.
         *
         *  For a caller whose value is already the row's type (a Modbus write).
         *  Going via text would put it through the row's DISPLAY rounding
         *  (`digits`), so 0.083 into a 2-decimal parameter would store 0.08.
         *  T_STR is refused: its text lives in the pool, not the slot. */
        SetResult set(Var h, Scalar v);

        /** @brief Apply @p v now and leave the flash write to the caller.
         *
         *  Storage is synchronous, so a caller committing many rows at once must
         *  pace the writes or stall the beat (one Modbus request can cover 61
         *  parameters). The value is live immediately; only the durable copy is
         *  deferred.
         *
         *  @warning Pair every SET_OK with a persist(), or the setting is
         *  silently lost at the next reboot. */
        SetResult set_deferred(Var h, Scalar v);

        /** @brief Write @p h's current value to the store. The other half of
         *  set_deferred(); harmless on a row that is already durable. */
        SetResult persist(Var h);

        /** @brief Force @p h to @p text until override_clear(). T_STR is refused
         *  (a hostname is configuration, not a field signal); everything else,
         *  ROLE_OUTPUT included, is fair game whatever control authority says -
         *  driving an actuator by hand with the programs stopped is the point of
         *  `control disable`. */
        SetResult override_set(Var h, const char* text, bool valid = true);

        /** @brief The same, with the value already typed. Forcing only the
         *  QUALITY is `override_set(h, h.raw(), false)`, which persists like any
         *  other override. */
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
        /** @brief Sort VarTables::index into name order, once, at construction.
         *  The descriptor table is const and never grows, so there is nothing to
         *  keep in step. A no-op (leaving _indexed false) when the site supplied
         *  no scratch array. */
        void _build_index();
        /** @brief Row order for the sort: by name, ties broken on row number, so
         *  a duplicated name resolves to the first of the two. */
        bool _order_before(uint16_t a, uint16_t b) const;
        /** @brief The one lookup both find() overloads run. @p len 0 means
         *  "@p name is NUL-terminated"; anything else bounds the comparison.
         *  @return the row index, or VAR_NONE. Does not count a miss - that is
         *  the caller's call, since a shell typo is not a build defect. */
        uint16_t _lookup(const char* name, uint16_t len) const;

        /** @brief @p h's row index, or VAR_NONE when it points at the sink. */
        uint16_t _row_of(Var h) const;
        /** @brief Write @p h's override through to the store. */
        void _persist_override(Var h);
        /** @brief Write an edited parameter through to the store, and say so
         *  loudly when the store refuses. Shared by both set() overloads. */
        SetResult _persist_param(Var h, const Variable& d, Value& slot);

        /** @brief The one output sweep both control edges run. @p before is the
         *  effective mode before the change, so a transition can be told from a
         *  re-assertion; @p erase_overrides is the system's destructive edge.
         *  Outputs only: clearing an internal would erase the diagnosis, and
         *  sweeping a parameter would revert the network settings on a scan
         *  overrun. */
        void _apply_control(ControlMode before, bool erase_overrides);
        /** @brief Bring one output's mute bit in step with the current mode,
         *  for an override forced while control is already withheld. */
        void _mute_if_disabled(Var h);

        /** @brief Locate the ROLE_PARAM run: first such row, then how many
         *  follow it without a break. A parameter outside that run is a table
         *  defect: the pages address the block by offset, so it is unreachable
         *  as a parameter. The site test asserts there is none. */
        void _find_param_block();

        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access: it is scratch, never shared state. */
        static const Variable _sink_d;
        static Value          _sink_v;
    };

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
