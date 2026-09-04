#ifndef VARS_DESCRIPTOR_H
#define VARS_DESCRIPTOR_H

/**
 * @file descriptor.h
 * @brief What a row IS: the fields a variable is declared along, and the const
 *        descriptor holding them.
 *
 * The flash-resident half of the model. model.h holds the other three quarters
 * - the RAM value slot, the handle over the pair, and the Bank.
 */

#include <cstddef>
#include <cstdint>
#include "libbotanik/alarms/spec.h"

#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Vars {


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

    /** @brief Which concrete kind of row this is - declared, never
     *  reconstructed from (transport, role, type), so a table filters on one
     *  comparison. One factory each in declare.h.
     *
     *  Two distinctions the names hide: **MI/MO vs AI/AO** is whether a
     *  CONVERSION happens; **SV vs G** is who can see it (SV is published, so
     *  its name and scaling are an external contract). Local I/O is not a scope
     *  - a local row is one whose Modbus device address is 0. */
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
        /** @brief A `_qlt` companion: the quality of the row it is named after.
         *  Mapped for a master, not published - a subscriber already learns a
         *  row is unbelievable by its absence from the payload. */
        SCOPE_Q,
        SCOPE_COUNT
    };

    /** @brief WHY a value reads as it does, written by whichever path judged the
     *  reading - a diagnosis, not a guess made at display time.
     *
     *  This is the VALUE of a `_qlt` companion row, and the only place a quality
     *  is ever stored. A row without a companion has no quality to store: it is
     *  valid, always. @see quality_base()
     *
     *  @warning The codes are the ST enumeration's too, which matiec numbers
     *  0,1,2... in declaration order - so this order is `plc/types.st`'s and
     *  cannot move. Q_NO_DATA leads for a second reason: a companion nothing has
     *  written yet must read as "nothing wrote this". */
    enum Quality : uint8_t {
        Q_NO_DATA = 0,   ///< nothing has ever written it; the row reads its declared default
        Q_VALID,         ///< a real value from whoever owns the row
        Q_STALE,         ///< it was live and stopped: no good read within its timeout
        Q_IMPLAUSIBLE,   ///< answering with nonsense: outside its band, or non-finite
        Q_REFUSED,       ///< the device answered, and declined: it has no such register
        /** @brief A law's own fallback: nothing measured it, but the number is
         *  one the law stands behind and readers should compute on. The
         *  interior temperature with every probe gone, held levels and the
         *  outside taken as evidence - substituting the declared 14 degC there
         *  would stop the roof venting on the day it matters most. */
        Q_SUBSTITUTED
    };

    /** @brief Does @p q have a NUMBER behind it a law should compute on?
     *
     *  The one rule Ref::get() branches on. Q_VALID because something measured
     *  it, Q_SUBSTITUTED because something stood behind it; every other code
     *  reads the row's declared value instead. Distinct from valid(), which
     *  stays "measured" - a law gating on whether the plant can be SEEN wants
     *  that one. */
    inline bool quality_holds(Quality q) {
        return q == Q_VALID || q == Q_SUBSTITUTED;
    }

    /** @brief @p q as a phrase for an operator: "valid", or invalid and why. */
    inline const char* quality_str(Quality q) {
        switch (q) {
            case Q_VALID:       return "valid";
            case Q_NO_DATA:     return "invalid: nothing has written it";
            case Q_STALE:       return "invalid: past its timeout";
            case Q_IMPLAUSIBLE: return "invalid: outside its band";
            case Q_REFUSED:     return "invalid: the device refused the read";
            case Q_SUBSTITUTED: return "unmeasured: a fallback the law stands behind";
            default:            return "invalid";
        }
    }

    /** @brief A Quality code's label, and the range check with it: a code this
     *  does not know renders and PARSES as nothing. The T_ENUM converter every
     *  `_qlt` row is declared with. */
    inline const char* quality_label(uint64_t q) {
        switch ((Quality)q) {
            case Q_NO_DATA:     return "NO_DATA";
            case Q_VALID:       return "VALID";
            case Q_STALE:       return "STALE";
            case Q_IMPLAUSIBLE: return "IMPLAUSIBLE";
            case Q_REFUSED:     return "REFUSED";
            case Q_SUBSTITUTED: return "SUBSTITUTED";
        }
        return nullptr;
    }

    /** @brief @p name less a trailing `_qlt`, into @p base. False when it does
     *  not end in one, or will not fit.
     *
     *  The suffix is the whole link between a row and its quality: `freeze()`
     *  pairs them by name, so there is no table to add a companion to and none
     *  to forget. A `_qlt` row has no companion of its own, which is what stops
     *  the regress. */
    bool quality_base(const char* name, char* base, size_t cap);

    /** @brief Per-variable behaviour bits. */
    enum VarFlags : uint8_t {
        F_INVERT  = 1u << 0,   ///< active-low wiring, folded in so logic reads true = asserted
        F_BOUNDED = 1u << 1,   ///< min/max are a real band, not "unset"
        F_RDONLY  = 1u << 2,   ///< a parameter shown but not written: read-only
        /** @brief A 32-bit value's registers arrive low word first. Registers
         *  are big-endian on the wire; only which half of a PAIR carries the
         *  high word varies. Meaningless unless `bind.regs` is 2. */
        F_WORD_SWAP = 1u << 3,
        /** @brief This row's value IS the history: litres delivered, hours
         *  run, the day a tray was last watered. Read by role - a
         *  `ROLE_COMPUTED` row goes to the retained store (@see
         *  Bank::retainable), a `ROLE_PARAM` one already persists, so the flag
         *  means `params reset` leaves it alone (@see Bank::param_retained). */
        F_RETAIN = 1u << 4,
        /** @brief A LOCAL digital input counted by an interrupt rather than
         *  polled as a level (`HAL::Pulse`). The terminal is attached once, as
         *  a counter: the level of a meter's pulse line is not a reading of
         *  anything, and configuring one pin two ways is a hardware fault
         *  waiting for the first wet week. */
        F_PULSE = 1u << 5,

        /** @brief The band's ends are in a NumRefBand, not in the payload's
         *  own scalars: this row's limits are other rows. */
        F_BAND_REF = 1u << 6
    };

    /** @brief Per-slot state bits, as opposed to the row's declared behaviour.
     *
     *  One byte carved out of Slot's spare three, so the slot stays 32 bytes. */
    enum ValueFlags : uint8_t {
        /** @brief This output's demand has moved and has not reached the field.
         *  Cleared by the driver once a transaction has carried it. */
        VF_FIELD_DIRTY = 1u << 0,
        /** @brief A PROGRAM's durable write to a parameter, waiting on the
         *  retain cadence rather than on an operator. Distinct from
         *  VF_PARAM_UNSAVED so that the two survive each other: a law stamping
         *  a watering date must not read as an unsaved SETTING on the params
         *  page, and the cadence must not commit a setting the operator has
         *  not confirmed. @see Bank::param_owe() */
        VF_PARAM_OWED    = 1u << 2,
        /** @brief This parameter's edit is live but not durable. What lets a
         *  display mark the row, and a bare `params save` find every row still
         *  owed a write. Cleared by save(), and by a load that overwrites the
         *  edit. @see Bank::set() @see Bank::save() */
        VF_PARAM_UNSAVED = 1u << 3,
        /** @brief The same, for this row's override: forced now, but it will
         *  not survive a reboot until `overrides save`.
         *  @see Bank::override_set() @see Bank::save_override() */
        VF_OVR_UNSAVED   = 1u << 4,
    };

    /** @brief What the firmware may drive, strictest first. The order is
     *  load-bearing: the effective mode is the MINIMUM of the operator's
     *  choice and what the system's hold allows. */
    enum ControlMode : uint8_t {
        /** @brief Outputs safe and the programs stopped, but an override still
         *  drives - how you take the plant by hand, since an override is then
         *  the only thing left that can move an actuator.
         *
         *  This rung does NOT mute overrides: one that stops driving without
         *  saying so is worse than one that keeps driving visibly. `override
         *  clear` takes a force off; what ERASES them is the system's own
         *  hold, a different axis. */
        CONTROL_WITHHELD = 0,
        /** @brief Normal operation. */
        CONTROL_ENABLED  = 1
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

    /** @brief A numeric row's band: a parameter's EDIT BOUNDS, or an input's
     *  PLAUSIBILITY BAND (a disconnected sensor usually reads a rail, and a
     *  reading outside marks the row invalid). Needs F_BOUNDED. */
    struct NumBand { Scalar min, max; };

    /** @brief What a T_STR row DECLARES about its pool slot: its size and what
     *  a new value is checked against - not where it sits, which Bank::init()
     *  derives into the Slot. */
    struct TextSlot {
        uint16_t cap;    ///< capacity including the NUL; 0 = no slot
        StrKind  kind;   ///< edit validation
    };

    /** @brief An alarm's announce policy. Defined in `alarms/spec.h` because it
     *  is an ALARM concept that a row happens to carry: a build with no variable
     *  model still has alarms, and they still have a dwell.
     *  @see Alarms::AlarmSpec */
    using Alarms::AlarmSpec;


    /** @brief A number a row declares: a fixed value, or the parameter holding
     *  it - which is what lets a calibration be re-trimmed from the PARAMS page
     *  without a rebuild. @c k is also the FALLBACK, so a mistyped name degrades
     *  to the declared constant rather than to zero (and the bank counts the
     *  miss, which the site test asserts is never). */
    struct Tunable {
        float       k;      ///< the value, when @c param is null or absent
        const char* param;  ///< the parameter to read instead (null = fixed)
    };

    /** @brief One analog channel's six tunables: a two-point calibration plus a
     *  plausibility band. @c scale / @c offset are never stored - they derive
     *  from the endpoints, so a recalibration lands the moment the parameter
     *  commits. min/max are separate and usually wider. Blocks are SHARED by
     *  pointer, which shares their parameters too. */
    struct AnalogCal {
        Tunable raw_min, raw_max;   ///< wire units at the two calibration points
        Tunable eng_min, eng_max;   ///< engineering units at those same points
        Tunable min, max;           ///< plausibility band, engineering units
    };

    /** @brief An edit band whose ends may be OTHER ROWS, so a pair with a
     *  required relationship cannot be edited into an impossible one: a target
     *  floor above its own ceiling, a ramp starting past where it ends. Both
     *  ends carry a fallback, so a mistyped name degrades to a fixed band. */
    struct NumRefBand {
        Tunable min, max;
    };

    /** @brief Which arm of Payload a row is using. The one place the question is
     *  answered; nothing open-codes it. */
    enum PayloadArm : uint8_t { PAY_NUM, PAY_TEXT, PAY_ALARM, PAY_ANALOG,
                                PAY_NUM_REF };

    /** @brief The half of a row that depends on its kind. No two arms coexist,
     *  so one slot serves all four; payload_arm() is the discriminant. Constexpr
     *  constructors because C++11 can only brace-initialise a union's FIRST
     *  member (as with Scalar).
     *  @warning Reach the arms through num_band() / text_slot() / analog_cal() /
     *  alarm_spec(), which check it: a number read as a slot hands
     *  `copy_bounded` an arbitrary pool offset. */
    union Payload {
        NumBand   num;
        TextSlot  text;
        AlarmSpec alarm;
        /** @brief Borrowed, and shareable: several rows may name one block. A
         *  pointer because six Tunables are 48 bytes and this union is 16. */
        const AnalogCal* cal;
        /// Borrowed the same way, and for the same reason.
        const NumRefBand* ref;
        /// An unset band: what a row with no meaningful bounds carries.
        constexpr Payload() : num{Scalar(), Scalar()} {}
        constexpr Payload(Scalar lo, Scalar hi) : num{lo, hi} {}
        constexpr Payload(uint16_t cap, StrKind kind) : text{cap, kind} {}
        constexpr Payload(const char* code, int16_t on_s, int16_t off_s,
                          int32_t recent_s, int16_t hold_s)
            : alarm{code, on_s, off_s, recent_s, hold_s} {}
        constexpr Payload(const AnalogCal* c) : cal(c) {}
        constexpr Payload(const NumRefBand* b) : ref(b) {}
    };

    /** @brief Every variable, whatever its kind.
     *
     *  @warning Field order is load-bearing: @c pay is 8-aligned, so @c str must
     *  stay AFTER it, where it lands in the tail hole (72 bytes on the M7).
     *  Ahead of it, the padding costs 8 more.
     *
     *  Built through the declare.h factories, not brace-initialised. */
    struct Descriptor {
        Scalar      def;        ///< invalid-read fallback / safe state / factory value
        const char* name;       ///< symbolic name; the telemetry leaf; the storage key
        const char* unit;       ///< "C", "%", "" for none
        const char* desc;       ///< description
        const char* notes;      ///< additional notes
        Payload     pay;        ///< band or pool slot, per `type == T_STR`
        Binding     bind;
        uint32_t    timeout_ms;///< ROLE_INPUT: no good read within this -> invalid; 0 = never
        /** @brief How often to ASK for this row, in ms; 0 derives it from
         *  @c timeout_ms. Kept apart from it: the timeout is a CONTRACT with
         *  readers ("believe this for 15 s"), the poll is a COST on the bus, so
         *  a meter can say "poll every 10 s, stale at 15". 16 bits, landing in
         *  the padding after @c timeout_ms. */
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
     *  The only sanctioned way to reach the union. Each checks the
     *  discriminant, so a caller that forgets which kind of row it has gets an
     *  inert answer rather than the other arm reinterpreted. @{ */

    /** @brief Which arm @p d's payload holds. Alarm first: it is a T_BOOL, so
     *  the type alone would say "band". */
    constexpr PayloadArm payload_arm(const Descriptor& d) {
        return (d.scope == SCOPE_A)  ? PAY_ALARM
             : (d.scope == SCOPE_AI ||
                d.scope == SCOPE_AO) ? PAY_ANALOG
             : (d.type == T_STR)     ? PAY_TEXT
             : (d.flags & F_BAND_REF) != 0 ? PAY_NUM_REF : PAY_NUM;
    }

    /** @brief @p d's numeric band; unset unless the row actually has one. */
    constexpr NumBand num_band(const Descriptor& d) {
        return (payload_arm(d) == PAY_NUM) ? d.pay.num : NumBand{Scalar(), Scalar()};
    }
    /** @brief @p d's declared pool slot; capacity 0 for a non-text row, which
     *  every writer already treats as "no slot". */
    constexpr TextSlot text_slot(const Descriptor& d) {
        return (payload_arm(d) == PAY_TEXT) ? d.pay.text : TextSlot{0, STR_NONE};
    }
    /** @brief @p d's calibration block; null for anything but SCOPE_AI/AO.
     *  Resolving its live numbers needs the bank - Vars::analog_values(). */
    constexpr const AnalogCal* analog_cal(const Descriptor& d) {
        return (payload_arm(d) == PAY_ANALOG) ? d.pay.cal : nullptr;
    }
    /** @brief @p d's referenced band; null unless it declared one. Resolving it
     *  needs the bank - Vars::var_band(). */
    constexpr const NumRefBand* num_ref_band(const Descriptor& d) {
        return (payload_arm(d) == PAY_NUM_REF) ? d.pay.ref : nullptr;
    }
    /** @brief @p d's alarm policy; a null code for anything but SCOPE_A. */
    constexpr AlarmSpec alarm_spec(const Descriptor& d) {
        return (payload_arm(d) == PAY_ALARM) ? d.pay.alarm
                                             : AlarmSpec{nullptr, 0, 0, 0, 0};
    }

    /** @brief Is @p v a believable reading for @p d? Non-finite first (a NaN is
     *  in no range, and calling that "out of band" would send an operator to a
     *  limit that is fine), then `F_BOUNDED` against the descriptor's band.
     *
     *  SCOPE_AI/AO are NOT judged here - their band is in the parameter table,
     *  so num_band() reads unset and would reject everything; their driver does
     *  it. One predicate, because the table sweep and the field-bus driver must
     *  agree about it. */
    bool value_implausible(const Descriptor& d, const Scalar& v);

    /** @} */


    /** @brief One layer of a variable: the number, what it is, and whether it
     *  is any good. Every read accessor returns this, so a value and its
     *  quality are fetched in one expression and cannot drift apart.
     *
     *  Whether the LAYER exists at all is @c present, not a quality: the
     *  override of an unforced row and the wire of a row with no binding are
     *  not conditions a row can be in, and putting them in the same vocabulary
     *  meant Q_NONE had to explain why it could not be 0.
     *
     *  @c v is always the best number available for @c q: a bad reading already
     *  carries the descriptor's `def`, so it never has to be inspected first. */
    struct Value {
        Scalar            v;
        const Descriptor* d;
        /** @brief What @c v actually is. USUALLY the row's type, but a wire
         *  reading is in the WIRE type - a u16 count feeding an f32 row. Held
         *  rather than derived from @c d, or that count would be read back as
         *  the bit pattern of a float. */
        VarType           t;
        Quality           q;
        /** @brief Whether this LAYER exists. False for the override of a row
         *  nothing forces, and for the wire of a row with no bus binding.
         *  No default: C++11 would make Value a non-aggregate and every brace
         *  construction below would stop compiling. */
        bool              present_;

        bool present() const { return present_; }
        bool good()    const { return q == Q_VALID; }

        VarType type() const { return t; }

        /** @name Conversions
         *  All SATURATING: 300 read as a u8 is 255, not 44. A wrapped value is
         *  indistinguishable from a real reading; a pinned one is obviously at
         *  the rail. T_STR is refused - its text is in the bank's pool, not
         *  here.
         *  @{ */
        float    as_f32() const;
        bool     as_bool() const;
        int64_t  as_i64() const;
        uint64_t as_u64() const;

        /** @brief The value as any whole type, saturating at ITS rail.
         *  @note Signedness and bounds come from @p T's own width, so this needs
         *  neither @c \<limits\> nor @c \<type_traits\>. */
        template <typename T> T as() const {
            // BOTH arms are compiled for every T - C++11 has no if constexpr -
            // so each rail is derived from sizeof rather than from T's own bit
            // pattern: ~(T)0 on a signed T sign-extends to all ones.
            const uint64_t span = (sizeof(T) >= 8)
                                      ? ~(uint64_t)0
                                      : (((uint64_t)1 << (8 * sizeof(T))) - 1);
            if ((T)-1 < (T)0) {
                const int64_t hi = (int64_t)(span >> 1);
                const int64_t lo = -hi - 1;
                const int64_t x  = as_i64();
                return (T)((x < lo) ? lo : ((x > hi) ? hi : x));
            }
            const uint64_t x = as_u64();
            return (T)((x > span) ? span : x);
        }
        /** @} */

        /** @brief A T_ENUM's label, or null: not an enum, or a code the
         *  converter does not know. That null IS the range check `set` uses. */
        const char* label() const;
    };


}}  // namespace Botanik::Vars

#endif // VARS_DESCRIPTOR_H
