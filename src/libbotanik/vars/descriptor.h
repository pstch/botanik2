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
#include "libbotanik/vars/ref.h"
#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Vars {


    /** @brief What a row IS, in one field. Everything a table used to filter on
     *  by scope, role or transport is either this or derived from a field the
     *  row carries anyway.
     *
     *  It is also the payload discriminant, with @c type and @c B_ANALOG. */
    enum Kind : uint8_t {
        K_PARAM,     ///< persisted, editable; band, labels or text slot
        K_ALARM,     ///< the boolean an AlarmCondition reads; carries its spec
        K_STATUS,    ///< computed by a law, published unless its register is 0
        K_INPUT,     ///< read from the field; expires, and always has a companion
        K_OUTPUT,    ///< driven to the field; drops to `def` when nothing drives it
        K_QUALITY    ///< an input's or a status's `_qlt`: the row before it speaks
    };

    /** @brief A Kind as a short column label: what a table page's first column
     *  and a point list write. */
    inline const char* kind_label(Kind k) {
        switch (k) {
            case K_PARAM:   return "P";
            case K_ALARM:   return "A";
            case K_STATUS:  return "S";
            case K_INPUT:   return "I";
            case K_OUTPUT:  return "O";
            case K_QUALITY: return "Q";
        }
        return "?";
    }

    /** @brief WHY a value reads as it does, written by whichever path judged the
     *  reading.
     *
     *  This is the VALUE of a `_qlt` companion row, and the only place a quality
     *  is ever stored. A row without a companion has no quality to store: it is
     *  good, always.
     *
     *  @warning The codes are the ST enumeration's too, which matiec numbers
     *  0,1,2... in declaration order - so this order is `plc/functions/quality.st`'s
     *  and cannot move. Q_NONE leads for a second reason: a companion nothing
     *  has written yet must read as "nothing wrote this". */
    enum Quality : uint8_t {
        Q_NONE = 0,   ///< nothing usable behind the row; it reads its declared value
        Q_GOOD,       ///< measured, restored, or forced
        /** @brief A law holds a number it stands behind, though nothing
         *  measured it. The interior temperature with every probe gone, held
         *  levels and the outside taken as evidence - substituting the declared
         *  14 degC there would stop the roof venting on the day it matters. */
        Q_HELD
    };

    /** @brief Is there a NUMBER behind @p q a reader should compute on? The one
     *  rule Ref::get() branches on. */
    constexpr bool usable(Quality q) { return q != Q_NONE; }

    /** @brief Did something MEASURE it? What a law gating on whether the plant
     *  can be SEEN asks, and what Var::valid() answers. */
    constexpr bool measured(Quality q) { return q == Q_GOOD; }

    /** @brief A Quality code's label, and the range check with it: a code this
     *  does not know renders and PARSES as nothing. The T_ENUM converter every
     *  `_qlt` row is declared with. */
    inline const char* quality_label(uint64_t q) {
        switch ((Quality)q) {
            case Q_NONE: return "NONE";
            case Q_GOOD: return "GOOD";
            case Q_HELD: return "HELD";
        }
        return nullptr;
    }

    /** @brief Per-variable behaviour bits. */
    enum VarFlags : uint8_t {
        F_INVERT = 1u << 0,   ///< active-low wiring, folded in so logic reads true = asserted
        F_RDONLY = 1u << 1,   ///< a parameter shown but not written: read-only
        /** @brief This row's value IS the history: litres delivered, hours run,
         *  the day a tray was last watered. On a K_STATUS row it goes to the
         *  retained store; on a K_PARAM one it already persists, so the flag
         *  means `params reset` leaves it alone. */
        F_RETAIN = 1u << 2,
        /** @brief The NEXT row is this one's `_qlt` companion. Adjacency is the
         *  whole link: the generator emits the pair together, so there is no
         *  index to store and no pairing pass to run. */
        F_QLT    = 1u << 3
    };

    /** @brief Per-binding bits: how a driver reaches the row, not what the row
     *  is for. */
    enum BindFlags : uint8_t {
        /** @brief A 32-bit value's registers arrive low word first. Registers
         *  are big-endian on the wire; only which half of a PAIR carries the
         *  high word varies. Meaningless unless `regs` is 2. */
        B_WORD_SWAP = 1u << 0,
        /** @brief A LOCAL digital input counted by an interrupt rather than
         *  polled as a level (`HAL::Pulse`). The terminal is attached once, as a
         *  counter: the level of a meter's pulse line is not a reading of
         *  anything. Channel is in @c reg and edge in @c fc. */
        B_PULSE     = 1u << 1,
        /** @brief The payload is an AnalogCal pointer: the wire carries a raw
         *  count and the value is the engineering number derived from it. The
         *  one thing that makes an analog row analog. */
        B_ANALOG    = 1u << 2
    };

    /** @brief Per-slot state bits, as opposed to the row's declared behaviour. */
    enum SlotFlags : uint8_t {
        S_OVR_ON        = 1u << 0,   ///< an override is in force
        S_OVR_UNSAVED   = 1u << 1,   ///< ...and it will not survive a reboot yet
        /** @brief This parameter's edit is live but not durable: what a display
         *  marks, and what a bare `params save` writes. */
        S_PARAM_UNSAVED = 1u << 2,
        /** @brief A PROGRAM's durable write, waiting on the retain cadence
         *  rather than on an operator. Distinct from S_PARAM_UNSAVED so the two
         *  survive each other: a law stamping a watering date must not read as
         *  an unsaved SETTING, and the cadence must not commit a setting the
         *  operator has not confirmed. */
        S_PARAM_OWED    = 1u << 3,
        /** @brief This output's demand has moved and has not reached the field.
         *  Cleared by the driver once a transaction has carried it. */
        S_FIELD_DIRTY   = 1u << 4,
        /** @brief Something has written this slot at least once. What the
         *  stamp's zero cannot say, since 0 ms is a real uptime. */
        S_WRITTEN       = 1u << 5
    };

    /** @brief How a driver reaches a variable. Eight bytes, and every field is
     *  meaningless on a row with no binding.
     *
     *  @c wire is the type ON THE WIRE, deliberately distinct from the
     *  variable's own VarType: a Modbus REAL feeding an f32, or a raw u32 count
     *  feeding an f32 engineering value through a calibration. */
    struct Binding {
        uint16_t reg;      ///< register address, coil, or local channel
        uint8_t  device;   ///< Modbus unit id; 0 = one of the controller's own terminals
        uint8_t  fc;       ///< Modbus function code, or a pulse row's edge
        uint8_t  regs;     ///< registers spanned (2 for a 32-bit value)
        VarType  wire;
        uint8_t  flags;    ///< BindFlags
        uint8_t  _rsv;
    };

    /** @brief One end of a band, or one point of a calibration: a number, or the
     *  ROW holding it - which is what lets a limit be re-trimmed from the PARAMS
     *  page without a rebuild, and a pair with a required relationship be judged
     *  as a pair.
     *
     *  @c v is also the FALLBACK the generator checks a default against, so the
     *  meaning is the same whether the end is fixed or not. */
    struct Tunable {
        Scalar   v;
        uint16_t row;   ///< VAR_NONE: fixed at @c v
    };

    /** @brief A numeric row's band: a parameter's EDIT BOUNDS, or an input's
     *  PLAUSIBILITY BAND (a disconnected sensor usually reads a rail, and a
     *  reading outside marks the row unusable).
     *
     *  Two Tunables, flattened so the pair packs into 24 bytes. Ends equal and
     *  both fixed is NO BAND - which is what a row with no meaningful bounds
     *  carries, and why there is no flag saying so. */
    struct Band {
        Scalar   lo, hi;
        uint16_t lo_row, hi_row;

        /** @brief Whether this is a band at all. */
        bool set() const {
            return lo_row != VAR_NONE || hi_row != VAR_NONE || lo.u != hi.u;
        }
        Tunable low()  const { return Tunable{ lo, lo_row }; }
        Tunable high() const { return Tunable{ hi, hi_row }; }
    };

    /** @brief What a T_STR row DECLARES about its pool slot: its size and what a
     *  new value is checked against - not where it sits, which the bank derives
     *  into the Slot. */
    struct TextSlot {
        uint16_t cap;    ///< capacity including the NUL; 0 = no slot
        StrKind  kind;   ///< edit validation
    };

    /** @brief One analog channel's six tunables: a two-point calibration plus a
     *  plausibility band. @c scale / @c offset are never stored - they derive
     *  from the endpoints, so a recalibration lands the moment the parameter
     *  commits. Blocks are SHARED by pointer, which shares their parameters. */
    struct AnalogCal {
        Scalar   raw_min, raw_max;   ///< wire units at the two calibration points
        Scalar   eng_min, eng_max;   ///< engineering units at those same points
        Scalar   min, max;           ///< plausibility band, engineering units
        uint16_t row[6];             ///< the row each reads instead, or VAR_NONE
    };

    /** @brief An analog channel's six numbers, resolved now.
     *
     *  Read together, in one call: they are used together, and per-field reads
     *  would pay an indirection each, every sample. */
    struct AnalogValues {
        float raw_min, raw_max;
        float eng_min, eng_max;
        float min, max;

        /** @brief Slope of the raw-to-engineering conversion. 0 for a degenerate
         *  calibration (both raw endpoints equal), which would otherwise divide
         *  by zero and hand the control programs an infinity. */
        float scale() const {
            const float span = raw_max - raw_min;
            return (span == 0.0f) ? 0.0f : (eng_max - eng_min) / span;
        }
        /** @brief Whether the raw endpoints coincide, so there is no conversion
         *  to do. NOT a value: to_eng() would collapse every reading to eng_min,
         *  which is a plausible number and would read as a measurement. */
        bool degenerate() const { return scale() == 0.0f; }
        /** @brief Intercept, so `eng = raw * scale() + offset()`. */
        float offset() const { return eng_min - raw_min * scale(); }
        float to_eng(float raw) const { return raw * scale() + offset(); }
        /** @brief The inverse, for an analog OUTPUT. Returns raw_min for a
         *  degenerate calibration. */
        float to_raw(float eng) const {
            const float sc = scale();
            return (sc == 0.0f) ? raw_min : (eng - offset()) / sc;
        }
        bool plausible(float eng) const { return eng >= min && eng <= max; }
    };

    /** @brief An alarm's announce policy. Defined in `alarms/spec.h` because it
     *  is an ALARM concept that a row happens to carry: a build with no variable
     *  model still has alarms, and they still have a dwell. */
    using Alarms::AlarmSpec;

    /** @brief The half of a row that depends on its kind. No two arms coexist,
     *  so one slot serves all four. Constexpr constructors because C++11 can
     *  only brace-initialise a union's FIRST member (as with Scalar).
     *  @warning Reach the arms through band() / text_slot() / analog_cal() /
     *  alarm_spec(), which check the discriminant: a number read as a slot hands
     *  `copy_bounded` an arbitrary pool offset. */
    union Payload {
        Band             num;
        TextSlot         text;
        AlarmSpec        alarm;
        /** @brief Borrowed, and shareable: several rows may name one block. A
         *  pointer because six tunables are 60 bytes and this union is 24. */
        const AnalogCal* cal;

        /// No band: what a row with no meaningful bounds carries.
        constexpr Payload() : num{Scalar(), Scalar(), VAR_NONE, VAR_NONE} {}
        constexpr Payload(Scalar lo, Scalar hi, uint16_t lo_row, uint16_t hi_row)
            : num{lo, hi, lo_row, hi_row} {}
        constexpr Payload(uint16_t cap, StrKind kind) : text{cap, kind} {}
        constexpr Payload(const char* code, int16_t on_s, int16_t off_s,
                          int32_t recent_s, int16_t hold_s)
            : alarm{code, on_s, off_s, recent_s, hold_s} {}
        constexpr Payload(const AnalogCal* c) : cal(c) {}
    };

    /** @brief Every variable, whatever its kind.
     *
     *  @warning Field order is load-bearing: @c pay is 8-aligned, so the four
     *  strings must stay AFTER it, where they land in the tail hole (72 bytes on
     *  the M7). Ahead of it, the padding costs 8 more. */
    struct Descriptor {
        Scalar      def;        ///< invalid-read substitute / safe state / factory value
        Payload     pay;
        const char* name;       ///< symbolic name; the telemetry leaf; the storage key
        const char* unit;       ///< "C", "%", "" for none
        const char* desc;
        const char* notes;
        Binding     bind;
        uint32_t    timeout_ms; ///< K_INPUT: no good read within this -> Q_NONE; 0 = never
        /** @brief How often to ASK for this row, in ms; 0 derives it from
         *  @c timeout_ms. Kept apart from it: the timeout is a CONTRACT with
         *  readers ("believe this for 15 s"), the poll is a COST on the bus. */
        uint16_t    poll_ms;
        Kind        kind;
        VarType     type;
        /** @brief T_ENUM renderer; null for a plain number. */
        const char* (*labels)(uint64_t);
        uint8_t     digits;     ///< T_F32 display decimals
        uint8_t     flags;      ///< VarFlags
    };

    /** @name Derived properties
     *  What a table filters on. Each is one comparison over fields the row
     *  carries anyway, so none of them is a declared field.
     *  @{ */

    /** @brief Belongs on the MONITOR page and in MQTT telemetry: everything but
     *  the three kinds that have a page of their own or speak for another row. */
    constexpr bool published(const Descriptor& d) {
        return d.kind != K_PARAM && d.kind != K_ALARM && d.kind != K_QUALITY;
    }
    /** @brief Carries a Modbus TCP slave register. An INPUT or an OUTPUT does
     *  not: its @c reg addresses a device on the FIELD bus, which is a different
     *  space entirely. The generator refuses a mapped kind with no register. */
    constexpr bool mapped(const Descriptor& d) {
        return d.bind.reg != 0 && d.kind != K_INPUT && d.kind != K_OUTPUT;
    }
    /** @brief Wired to one of the controller's own terminals rather than to the
     *  field bus. */
    constexpr bool local(const Descriptor& d) {
        return (d.kind == K_INPUT || d.kind == K_OUTPUT) && d.bind.device == 0;
    }
    /** @brief The wire carries a raw count and the value is derived from it. */
    constexpr bool analog(const Descriptor& d) {
        return (d.bind.flags & B_ANALOG) != 0;
    }
    /** @brief A pulse-counted local terminal: the row IS the running edge count. */
    constexpr bool pulse(const Descriptor& d) { return (d.bind.flags & B_PULSE) != 0; }
    /** @name A pulse row's terminal
     *  `PI()` puts the channel in the two Binding fields a local row has no
     *  other use for. These read it back, so the encoding is stated once and
     *  everything that wants it ASKS. The edge is a `HAL::Pulse::Edge`, which
     *  this layer must not name.
     *  @{ */
    constexpr uint8_t pulse_channel(const Descriptor& d) { return (uint8_t)d.bind.reg; }
    constexpr uint8_t pulse_edge(const Descriptor& d) { return d.bind.fc; }
    /** @} */
    /** @brief Anything may WRITE it: a parameter that is not read-only. Nothing
     *  writes a reading. */
    constexpr bool writable(const Descriptor& d) {
        return d.kind == K_PARAM && (d.flags & F_RDONLY) == 0;
    }
    /** @brief The next row is this one's `_qlt` companion. */
    constexpr bool has_quality(const Descriptor& d) { return (d.flags & F_QLT) != 0; }

    /** @} */

    /** @name Payload access
     *  The only sanctioned way to reach the union. Each checks the
     *  discriminant, so a caller that forgets which kind of row it has gets an
     *  inert answer rather than the other arm reinterpreted. Resolving a band's
     *  ROW ends needs the bank - Var::band().
     *  @{ */

    /** @brief @p d's declared band, ends unresolved; no band unless the row
     *  actually has one. An analog row's band is in its calibration. */
    constexpr Band declared_band(const Descriptor& d) {
        return (analog(d) || d.type == T_STR || d.kind == K_ALARM)
                   ? Band{Scalar(), Scalar(), VAR_NONE, VAR_NONE}
                   : d.pay.num;
    }
    /** @brief @p d's declared pool slot; capacity 0 for a non-text row, which
     *  every writer already treats as "no slot". */
    constexpr TextSlot text_slot(const Descriptor& d) {
        return (d.type == T_STR) ? d.pay.text : TextSlot{0, STR_NONE};
    }
    /** @brief @p d's calibration block; null unless the row is analog. */
    constexpr const AnalogCal* analog_cal(const Descriptor& d) {
        return analog(d) ? d.pay.cal : nullptr;
    }
    /** @brief @p d's alarm policy; a null code for anything but a K_ALARM. */
    constexpr AlarmSpec alarm_spec(const Descriptor& d) {
        return (d.kind == K_ALARM) ? d.pay.alarm
                                   : AlarmSpec{nullptr, 0, 0, 0, 0};
    }

    /** @} */


    /** @brief A number and enough to read it: what a Var's read accessors
     *  return. The conversions below are the reason it is a struct - @c t is
     *  what @c v is IN, which is not always the row's own type.
     *
     *  It carries no quality. Quality is a row, so a caller holding the Var
     *  asks it directly and there is nothing to keep in step. */
    struct Value {
        Scalar            v;
        const Descriptor* d;
        /** @brief What @c v actually is. USUALLY the row's type, but a wire
         *  reading is in the WIRE type - a u16 count feeding an f32 row. */
        VarType           t;

        VarType type() const { return t; }

        /** @name Conversions
         *  All SATURATING: 300 read as a u8 is 255, not 44. A wrapped value is
         *  indistinguishable from a real reading; a pinned one is obviously at
         *  the rail. T_STR is refused - its text is in the bank's pool.
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
