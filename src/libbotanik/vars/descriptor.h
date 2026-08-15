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

#include <cstdint>

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
        SCOPE_COUNT
    };

    /** @brief WHY a value reads as it does, written by whichever path
     *  invalidated the row - a diagnosis, not a guess made at display time.
     *
     *  @warning Q_NO_DATA is 0 deliberately: a zero-initialised Value (every
     *  site's store, and the sink) must read as "nothing wrote this". */
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
        /** @brief A 32-bit value's registers arrive low word first. Registers
         *  are big-endian on the wire; only which half of a PAIR carries the
         *  high word varies. Meaningless unless `bind.regs` is 2. */
        F_WORD_SWAP = 1u << 3,
        /** @brief Keep this row's value across a reboot (IEC 61131-3 RETAIN),
         *  for rows whose value IS the history: litres delivered, hours run.
         *  `ROLE_COMPUTED` only (@see Bank::retainable()). Saved on a cadence
         *  and on the way down, never per write - a totalizer moves every beat
         *  and would wear the flash out in days. */
        F_RETAIN = 1u << 4
    };

    /** @brief Per-slot state bits, as opposed to the row's declared behaviour.
     *
     *  One byte carved out of Value's spare three, so the slot stays 32 bytes. */
    enum ValueFlags : uint8_t {
        /** @brief This output's demand has moved and has not reached the field.
         *  Cleared by the driver once a transaction has carried it. */
        VF_FIELD_DIRTY = 1u << 0,
        /** @brief This row has had more than one control program write it.
         *  Latched, so the count is of ROWS with two authors rather than of
         *  per-beat collisions. @see Bank::claim_writer() */
        VF_TWO_WRITERS = 1u << 1,
        /** @brief This output's override is in force but must not reach the
         *  field (CONTROL_DISABLED). NOT erased, so leaving the mode restores
         *  it. ROLE_OUTPUT only, maintained by the Bank alone. */
        VF_OVR_MUTED   = 1u << 2,
        /** @brief This parameter's edit is live but not durable. What lets a
         *  display mark the row, and a bare `params save` find every row still
         *  owed a write. Cleared by save(), and by a load that overwrites the
         *  edit. @see Bank::set() @see Bank::save() */
        VF_PARAM_UNSAVED = 1u << 3,
        /** @brief The same, for this row's override: forced now, but it will
         *  not survive a reboot until `overrides save`.
         *  @see Bank::override_set() @see Bank::save_override() */
        VF_OVR_UNSAVED   = 1u << 4
    };

    /** @brief What the firmware may drive, strictest first. The order is
     *  load-bearing: the effective mode is the MINIMUM of the operator's
     *  choice and what the system's hold allows. */
    enum ControlMode : uint8_t {
        /** @brief Outputs safe, and an output override does NOT reach the
         *  field - muted, not erased, so a less strict mode brings it back
         *  (unless the SYSTEM withdrew control, which erases it). */
        CONTROL_DISABLED  = 0,
        /** @brief Outputs safe and the programs stopped, but an override still
         *  drives - how you take the plant by hand, since an override is then
         *  the only thing left that can move an actuator. */
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

    /** @brief A numeric row's band: a parameter's EDIT BOUNDS, or an input's
     *  PLAUSIBILITY BAND (a disconnected sensor usually reads a rail, and a
     *  reading outside marks the row invalid). Needs F_BOUNDED. */
    struct NumBand { Scalar min, max; };

    /** @brief What a T_STR row DECLARES about its pool slot: its size and what
     *  a new value is checked against - not where it sits, which Bank::init()
     *  derives into the Value. */
    struct TextSlot {
        uint16_t cap;    ///< capacity including the NUL; 0 = no slot
        StrKind  kind;   ///< edit validation
    };

    /** @brief An alarm row's announce policy: display code plus the thresholds
     *  AlarmController runs it on.
     *
     *  The dwells are int16 seconds to keep the arm inside the 16 bytes NumBand
     *  already costs, and nine hours of dwell is past any plausible setting.
     *  @c recent_s is NOT a dwell - it is a retention window measured in days,
     *  so it gets the int32 the freed slot pays for. */
    struct AlarmSpec {
        const char* code;         ///< short display code (max 9: AlarmsPage::CODE_W)
        int16_t     on_s;         ///< time ON before announcing (-1: off)
        int16_t     off_s;        ///< time OFF before announcing (-1: off)
        int32_t     recent_s;     ///< RECENT window (0: default, <0: forever)
        int16_t     hold_s;       ///< keep active this long past the last raw true
    };

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

    /** @brief Which arm of Payload a row is using. The one place the question is
     *  answered; nothing open-codes it. */
    enum PayloadArm : uint8_t { PAY_NUM, PAY_TEXT, PAY_ALARM, PAY_ANALOG };

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
        /// An unset band: what a row with no meaningful bounds carries.
        constexpr Payload() : num{Scalar(), Scalar()} {}
        constexpr Payload(Scalar lo, Scalar hi) : num{lo, hi} {}
        constexpr Payload(uint16_t cap, StrKind kind) : text{cap, kind} {}
        constexpr Payload(const char* code, int16_t on_s, int16_t off_s,
                          int32_t recent_s, int16_t hold_s)
            : alarm{code, on_s, off_s, recent_s, hold_s} {}
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
    /** @brief @p d's declared pool slot; capacity 0 for a non-text row, which
     *  every writer already treats as "no slot". */
    constexpr TextSlot text_slot(const Variable& d) {
        return (payload_arm(d) == PAY_TEXT) ? d.pay.text : TextSlot{0, STR_NONE};
    }
    /** @brief @p d's calibration block; null for anything but SCOPE_AI/AO.
     *  Resolving its live numbers needs the bank - Vars::analog_values(). */
    constexpr const AnalogCal* analog_cal(const Variable& d) {
        return (payload_arm(d) == PAY_ANALOG) ? d.pay.cal : nullptr;
    }
    /** @brief @p d's alarm policy; a null code for anything but SCOPE_A. */
    constexpr AlarmSpec alarm_spec(const Variable& d) {
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
    bool value_implausible(const Variable& d, const Scalar& v);

    /** @} */


}}  // namespace Botanik::Vars

#endif // VARS_DESCRIPTOR_H
