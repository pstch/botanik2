#ifndef VARS_DESCRIPTOR_H
#define VARS_DESCRIPTOR_H

/**
 * @file descriptor.h
 * @brief What a row IS: the fields a variable is declared along, the const
 *        descriptor holding them.
 *
 * The flash-resident half of the model. model.h holds the other three quarters
 * - the RAM value slot, the handle over the pair, and the Bank.
 */

#include <cstddef>
#include <cstdint>

#include "libbotanik/vars/quality.h"
#include "libbotanik/alarms/spec.h"
#include "libbotanik/vars/ref.h"
#include "libbotanik/vars/scalar.h"

namespace Botanik { namespace Vars {


    /** @brief What a row IS, in one field. Everything a table filters on is
     *  either this or derived from a field the row carries anyway.
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

    /** @brief Is there a NUMBER behind @p q a reader should compute on? The one
     *  rule Ref::get() branches on. */
    constexpr bool usable(Quality q) { return q != Q_NONE; }

    /** @brief Did something MEASURE it? What a law gating on whether the plant
     *  can be SEEN asks, and what Var::valid() answers. */
    constexpr bool measured(Quality q) { return q == Q_GOOD; }

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
        S_WRITTEN       = 1u << 5,
        /** @brief This retained row has MOVED since the store last took it, so
         *  the retain cadence owes it a write. Without the mark the cadence
         *  writes every retained row every period, and a flash erase for a value
         *  that did not change is wear bought for nothing. */
        S_RETAIN_OWED   = 1u << 6
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

    /** @brief A numeric row's band: a parameter's EDIT BOUNDS, or an input's
     *  PLAUSIBILITY BAND (a disconnected sensor usually reads a rail, and a
     *  reading outside marks the row unusable).
     *
     *  The value and the row it may name instead, per end, flattened so the
     *  pair packs into 24 bytes. Ends equal and both fixed is NO BAND - which
     *  is what a row with no meaningful bounds carries, and why there is no
     *  flag saying so. */
    struct Band {
        Scalar   lo, hi;
        uint16_t lo_row, hi_row;

        /** @brief Whether this is a band at all. */
        bool set() const {
            return lo_row != VAR_NONE || hi_row != VAR_NONE || lo.u != hi.u;
        }
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
    /** @brief The six ends of a calibration, named once. The index into both
     *  arrays of an AnalogCal, and the order the generator emits. */
    enum CalEnd : uint8_t {
        CAL_RAW_MIN,   ///< wire units at the two calibration points
        CAL_RAW_MAX,
        CAL_ENG_MIN,   ///< engineering units at those same points
        CAL_ENG_MAX,
        CAL_MIN,       ///< plausibility band, engineering units
        CAL_MAX,
        CAL_END_COUNT
    };

    /** @note Parallel arrays rather than six named pairs: a Scalar and a
     *  uint16_t pack to 16 bytes each, so an array of pairs would cost half
     *  again in flash for alignment that buys nothing. Indexed by CalEnd, so
     *  the value and the row it may name cannot drift apart by position. */
    struct AnalogCal {
        Scalar   v[CAL_END_COUNT];     ///< the fixed value of each end
        uint16_t row[CAL_END_COUNT];   ///< the row it reads instead, or VAR_NONE
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
    /** @brief Reaches a wire at all: the two kinds whose Binding means
     *  something. Everything else is computed, and its @c bind is noise. */
    constexpr bool bound(const Descriptor& d) {
        return d.kind == K_INPUT || d.kind == K_OUTPUT;
    }
    /** @brief Carries a Modbus TCP slave register. A bound row does not: its
     *  @c reg addresses a device on the FIELD bus, which is a different space
     *  entirely. The generator refuses a mapped kind with no register. */
    constexpr bool mapped(const Descriptor& d) {
        return d.bind.reg != 0 && !bound(d);
    }
    /** @brief Wired to one of the controller's own terminals rather than to the
     *  field bus. */
    constexpr bool local(const Descriptor& d) {
        return bound(d) && d.bind.device == 0;
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
    /** @brief Written back to the retained store: the row's VALUE is the
     *  history, and the store is where it survives a reboot. */
    constexpr bool retained(const Descriptor& d) {
        return (d.flags & F_RETAIN) != 0 && d.kind == K_STATUS;
    }
    /** @brief F_RETAIN on a PARAMETER means the other thing: it persists
     *  already, and the flag says `params reset` leaves it alone. */
    constexpr bool retained_param(const Descriptor& d) {
        return (d.flags & F_RETAIN) != 0 && d.kind == K_PARAM;
    }

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




    /** @name The RAM half
     *  The mutable quarter of a row, parallel to the const table above.
     *  Here because SlotFlags is: a Slot and the bits that describe it are
     *  one thing, and splitting them put the enum in one file and the field
     *  it annotates in another.
     *  @{ */

    /** @brief One value slot, parallel to the descriptor table. No `def`: the
     *  default is a typed literal in flash.
     *  @note While S_OVR_ON, @c ovr SHADOWS @c v, so clearing snaps back to
     *  live. "Read as a dead sensor" is an override on the row's `_qlt`
     *  companion, which is an ordinary force of an ordinary row.
     *  @warning Both halves are PERSISTED - hence the CRITICAL on each restore. */
    struct Slot {
        Scalar   v;
        Scalar   ovr;        ///< forced value, while S_OVR_ON
        /** @brief Low 32 bits of uptime at the last good update. Ages by
         *  UNSIGNED subtraction, so the 49-day wrap costs nothing; what the zero
         *  cannot say is "never written", which is S_WRITTEN. */
        uint32_t stamp_ms;
        /** @brief T_STR only: this row's pool slot, or STR_NO_SLOT when it did
         *  not fit. Laid out at construction, never declared. */
        uint16_t str_off;
        uint8_t  flags;      ///< SlotFlags
        /** @brief Index into the bank's wire array for an analog row, WIRE_NONE
         *  otherwise. */
        uint8_t  wire_off;
    };

    /** @brief This row has no wire value of its own: its value IS what the wire
     *  carries, or it has no binding at all. */
    static const uint8_t WIRE_NONE = 0xFFu;

    // Every field here costs RAM on every row of every site, so the size is
    // pinned rather than left to drift.
    static_assert(sizeof(Slot) == 24, "Slot must stay 24 bytes (RAM per variable)");

    /** @brief A text row that found no room in the pool. Not 0, which is a REAL
     *  slot: an overflowed row parked there keeps its declared capacity and
     *  would be written over the first text row. */
    static const uint16_t STR_NO_SLOT = 0xFFFFu;

    /** @brief What a write attempt did. A failed PERSIST is not a rejected
     *  EDIT: the value is live either way. */
    enum SetResult : uint8_t {
        SET_OK,             ///< applied (set), or written out (save)
        SET_INVALID,        ///< rejected - malformed, out of range, read-only
        SET_NOT_PERSISTED   ///< live in RAM, but storage refused the write
    };

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_DESCRIPTOR_H
