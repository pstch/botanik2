#ifndef VARS_DECLARE_H
#define VARS_DECLARE_H

/**
 * @file declare.h
 * @brief One constexpr factory per row spelling: how the generated table reads.
 *
 * Sixteen factories over six Kinds. Each takes ONLY the arguments its spelling
 * can use, so a meaningless row is unspellable: a relay with a plausibility
 * band, an alarm with a timeout, a parameter with a device address. The fields a
 * row does not carry are absent from the call, and the factory supplies them.
 * @note The rationale behind this file is @ref declare_flash, @ref declare_local.
 */

#include <cstdint>

#include "libbotanik/vars/descriptor.h"

namespace Botanik { namespace Vars {

    /** @name Scalar shorthands
     *
     *  A bare literal cannot build a Scalar (the union takes uint64 / int64 /
     *  float / const char* equally well), so the type must be said. Scalar::
     *  U/I/F/B under names short enough to keep a table row on one line.
     *  @{ */

    constexpr Scalar SU(uint64_t v) { return Scalar::U(v); }
    constexpr Scalar SI(int64_t v)  { return Scalar::I(v); }
    constexpr Scalar SF(float v)    { return Scalar::F(v); }
    constexpr Scalar SB(bool v)     { return Scalar::B(v); }
    /// Text. Named ST, not SS: Mbed defines SS (SPI slave select), and the
    /// site table has to compile inside an Arduino sketch.
    constexpr Scalar ST(const char* v) { return Scalar::S(v); }

    /** @brief Build a Scalar of @p t from a plain number: the row's own type
     *  picks the arm, which is why P() below takes bare literals. */
    constexpr Scalar num(VarType t, double v) {
        return type_real(t)   ? Scalar::F((float)v)
             : type_signed(t) ? Scalar::I((int64_t)v)
                              : Scalar::U((uint64_t)v);
    }

    /** @} */

    /** @name Bit helpers
     *  Taken as the POSITIVE sense so the call sites do not read as double
     *  negatives.
     *  @{ */

    constexpr uint8_t retain_bits(bool retained) { return retained ? (uint8_t)F_RETAIN : 0; }
    constexpr uint8_t qlt_bits(bool has)         { return has ? (uint8_t)F_QLT : 0; }
    constexpr uint8_t swap_bits(bool swapped)    { return swapped ? (uint8_t)B_WORD_SWAP : 0; }

    /** @} */

    /** @name Row factories
     *
     *  Argument order is the same everywhere it applies: identity (name), then
     *  what it reads or drives, then the band, then presentation (digits, unit),
     *  then prose (desc, notes). Every row ends with desc and notes, and notes
     *  is never optional ("" is a valid note).
     *  @{ */

    /** @brief An **alarm**: the boolean carrying the condition, AND the policy
     *  the AlarmController runs it on. There is no second alarm table: the model
     *  scans the variable table for K_ALARM rows, in table order.
     *
     *  @p code is the short display code (max 9: AlarmsPage::CODE_W) the first
     *  column and the alarm bar show; identity still belongs to @p name.
     *
     *  Two of the timings decide what is SAID and one decides what is TRUE:
     *
     *      on_s / off_s   dwell before a START / END is announced, -1 to never
     *                     announce that edge. They gate the log line and the
     *                     notifier fan-out only; neither touches `active`.
     *      hold_s         keep the condition ACTIVE this long past the last raw
     *                     true, re-armed on every beat the bit is up. The
     *                     anti-flicker: it bridges a chattering signal into one
     *                     episode, and keeps a pulse too short to see on screen
     *                     long enough to be seen. 0 = follow the raw bit.
     *
     *  @p recent_s is neither: it is how long a CLEARED alarm stays in the
     *  ALARMS page's RECENT view (0 = Config::ALARM_RECENT_S, <0 = forever).
     *
     *  @p reg is its register on the Modbus TCP slave, published as a read-only
     *  coil; acknowledging one is a write to a separate block. */
    constexpr Descriptor A(const char* name, const char* code,
                           int16_t on_s, int16_t off_s, int32_t recent_s,
                           int16_t hold_s, uint16_t reg,
                           const char* desc, const char* notes) {
        return Descriptor{ Scalar::B(false),
                           Payload(code, on_s, off_s, recent_s, hold_s),
                           name, "", desc, notes,
                           { reg, 0, 0, 1, T_BOOL, 0, 0 }, 0, 0,
                           K_ALARM, T_BOOL, nullptr, 0, 0 };
    }

    /** @brief A numeric **parameter**: persisted, editable, always usable.
     *
     *  @p def, @p lo and @p hi are plain numbers - the row's @p type decides
     *  whether they land as unsigned, signed or float. Either end may instead
     *  name a ROW (@p lo_row / @p hi_row), so a pair with a required
     *  relationship - a target's floor and ceiling, a ramp's start and end -
     *  cannot be edited into an impossible one. The number beside a named end is
     *  the fallback, and what the generator checks the default against.
     *
     *  @p reg is the row's register on the Modbus TCP slave, hand-assigned and
     *  NOT derived from its position: an external master's map is a contract, so
     *  inserting a parameter must not renumber the ones after it.
     *
     *  @p retained marks a row whose value RECORDS what happened - the day a
     *  tray was last watered - which `params reset` then leaves alone. */
    constexpr Descriptor P(const char* name, VarType type, double def,
                           double lo, double hi, uint16_t lo_row, uint16_t hi_row,
                           uint8_t digits, const char* unit, uint16_t reg,
                           const char* desc, const char* notes,
                           bool retained = false) {
        return Descriptor{ num(type, def),
                           Payload(num(type, lo), num(type, hi), lo_row, hi_row),
                           name, unit, desc, notes,
                           { reg, 0, 0, 1, type, 0, 0 }, 0, 0,
                           K_PARAM, type, nullptr, digits,
                           retain_bits(retained) };
    }

    /** @brief An enumerated **parameter**: a small code the operator reads and
     *  writes as a WORD.
     *
     *  @p labels is the converter, and it is the row's whole vocabulary: it
     *  renders a code as its label, it parses a label back, and a code it
     *  answers null for is REFUSED. So the labels are the bound, and there is no
     *  band to declare - a set of codes is not an interval, and a min/max would
     *  admit every gap between them.
     *
     *  @warning The codes are what sits in the store and in an external master's
     *  register, so a converter may gain labels but must never renumber them. */
    constexpr Descriptor PE(const char* name, uint64_t def,
                            const char* (*labels)(uint64_t), uint16_t reg,
                            const char* desc, const char* notes,
                            bool retained = false) {
        return Descriptor{ Scalar::U(def), Payload(), name, "", desc, notes,
                           { reg, 0, 0, 1, T_ENUM, 0, 0 }, 0, 0,
                           K_PARAM, T_ENUM, labels, 0,
                           retain_bits(retained) };
    }

    /** @brief A text **parameter**: its own factory, since bounds mean nothing
     *  for a string and a pool slot nothing for a number.
     *
     *  @p cap is the slot size including the NUL; WHERE the slot sits is not
     *  declared, the bank lays the slots out in table order. @p kind is what a
     *  new value is validated against. */
    constexpr Descriptor PS(const char* name, const char* def, uint16_t cap,
                            StrKind kind, uint16_t reg,
                            const char* desc, const char* notes,
                            bool retained = false) {
        return Descriptor{ Scalar::S(def), Payload(cap, kind),
                           name, "", desc, notes,
                           { reg, 0, 0, 1, T_STR, 0, 0 }, 0, 0,
                           K_PARAM, T_STR, nullptr, 0,
                           retain_bits(retained) };
    }

    /** @brief A **status value**: computed by a law and shown on the HMI, plus
     *  published - MQTT telemetry and the Modbus TCP slave - when @p reg is not
     *  zero. Its name and its scaling are then an external contract, so renaming
     *  one breaks whatever is subscribed.
     *
     *  @p retained is IEC 61131-3 RETAIN: the value survives a reboot, for the
     *  rows whose value IS the history. It costs one storage key and a write
     *  every Config::RETAIN_SAVE_MS, so it is not for every row, and @p def is
     *  then what a FIRST boot reads. @p qlt says the next row is its companion,
     *  which the program that writes it has opted into. */
    constexpr Descriptor SV(const char* name, VarType type, double def,
                            uint8_t digits, const char* unit, uint16_t reg,
                            const char* desc, const char* notes,
                            bool retained = false, bool qlt = false) {
        return Descriptor{ num(type, def), Payload(), name, unit, desc, notes,
                           { reg, 0, 0, 1, type, 0, 0 }, 0, 0,
                           K_STATUS, type, nullptr, digits,
                           (uint8_t)(retain_bits(retained) | qlt_bits(qlt)) };
    }

    /** @brief An enumerated **status value**: SV(), rendered through a
     *  converter, for a code an operator reads as a WORD.
     *
     *  A control program's own state machine is the case this exists for: a
     *  phase reading `DAWN_BREEZE` on the HMI and 3 on the wire. Same contract
     *  as PE(): the converter is the vocabulary, and its codes may gain labels
     *  and must never be renumbered. */
    constexpr Descriptor SVE(const char* name, uint64_t def,
                             const char* (*labels)(uint64_t), uint16_t reg,
                             const char* desc, const char* notes,
                             bool retained = false, bool qlt = false) {
        return Descriptor{ Scalar::U(def), Payload(), name, "", desc, notes,
                           { reg, 0, 0, 1, T_ENUM, 0, 0 }, 0, 0,
                           K_STATUS, T_ENUM, labels, 0,
                           (uint8_t)(retain_bits(retained) | qlt_bits(qlt)) };
    }

    /** @brief A **quality companion**: `<row>_qlt`, whose value is the quality
     *  of the row it FOLLOWS. Generated, one per input and one per row an
     *  opted-in program writes; never hand-declared.
     *
     *  Q_NONE is its default, so a row reads unusable until something has spoken
     *  for it. Adjacency is the link: the row before it carries F_QLT. */
    constexpr Descriptor Q(const char* name, uint16_t reg, const char* desc) {
        return Descriptor{ Scalar::U((uint64_t)Q_NONE), Payload(),
                           name, "", desc, "",
                           { reg, 0, 0, 1, T_ENUM, 0, 0 }, 0, 0,
                           K_QUALITY, T_ENUM, quality_label, 0, 0 };
    }

    /** @brief A **direct input**: the register maps 1:1 onto @p type, no
     *  conversion. The band is the plausibility band - generous on purpose, to
     *  catch a dead meter reporting a rail rather than to police the process. A
     *  read older than @p timeout_ms drops the row to Q_NONE, and @p def is what
     *  it reads then: safety-relevant, so it is spelled per row rather than
     *  assumed to be zero. */
    constexpr Descriptor MI(const char* name, VarType type, uint8_t device,
                            uint8_t fc, uint16_t reg, uint8_t regs,
                            Scalar lo, Scalar hi, uint16_t lo_row, uint16_t hi_row,
                            Scalar def, uint32_t timeout_ms, uint8_t digits,
                            const char* unit, const char* desc, const char* notes,
                            uint16_t poll_ms = 0, bool word_swap = false,
                            bool qlt = true) {
        return Descriptor{ def, Payload(lo, hi, lo_row, hi_row),
                           name, unit, desc, notes,
                           { reg, device, fc, regs, type, swap_bits(word_swap), 0 },
                           timeout_ms, poll_ms, K_INPUT, type, nullptr, digits,
                           qlt_bits(qlt) };
    }

    /** @brief An **analog input**: a raw count on the wire, converted to the
     *  engineering value by the two-point calibration in @p cal, which also
     *  carries the plausibility band. Device 0 = a local expansion channel.
     *
     *  No scale/offset argument: both derive from the endpoints, and each of the
     *  six may be a constant or a row an operator edits. A block may be SHARED
     *  with any other channel calibrated identically. */
    constexpr Descriptor AI(const char* name, const AnalogCal* cal, uint8_t device,
                            uint8_t fc, uint16_t reg, uint8_t regs, VarType wire,
                            Scalar def, uint32_t timeout_ms, uint8_t digits,
                            const char* unit, const char* desc, const char* notes,
                            uint16_t poll_ms = 0, bool word_swap = false,
                            bool qlt = true) {
        return Descriptor{ def, Payload(cal), name, unit, desc, notes,
                           { reg, device, fc, regs, wire,
                             (uint8_t)(B_ANALOG | swap_bits(word_swap)), 0 },
                           timeout_ms, poll_ms, K_INPUT, T_F32, nullptr, digits,
                           qlt_bits(qlt) };
    }

    /** @brief A **digital input**: one bit, POLLED as a level. @p invert folds
     *  active-low wiring in here, so the control programs read true = asserted.
     *  Device 0 = one of the controller's own input terminals.
     *  @note An edge-counted terminal is PI(), not this: its level is not a
     *  reading of anything. */
    constexpr Descriptor DI(const char* name, uint8_t device, uint8_t fc,
                            uint16_t reg, bool def, uint32_t timeout_ms,
                            bool invert, const char* desc, const char* notes,
                            uint16_t poll_ms = 0, bool qlt = true) {
        return Descriptor{ Scalar::B(def), Payload(), name, "", desc, notes,
                           { reg, device, fc, 1, T_BOOL, 0, 0 },
                           timeout_ms, poll_ms, K_INPUT, T_BOOL, nullptr, 0,
                           (uint8_t)(qlt_bits(qlt) | (invert ? F_INVERT : 0)) };
    }

    /** @brief A **pulse-counted local terminal**: the running EDGE COUNT, as a
     *  row. Not a level - the level of a pulse line is a moment of a train, not
     *  a reading - so the row is the counter, and a meter's law differences it
     *  and carries the 32-bit wrap.
     *
     *  A row rather than a table beside the table: a counter only C++ can read
     *  is one no ST program can meter with.
     *  @param channel the local terminal, 0 = I1. Carried in @c reg, which a
     *         local row has no other use for.
     *  @param edge which edge advances the count (HAL::Pulse::Edge), in @c fc. */
    constexpr Descriptor PI(const char* name, uint8_t channel, uint8_t edge,
                            const char* desc, const char* notes,
                            bool qlt = true) {
        return Descriptor{ Scalar::U(0), Payload(), name, "", desc, notes,
                           { channel, 0, edge, 1, T_U32, B_PULSE, 0 },
                           0, 0, K_INPUT, T_U32, nullptr, 0, qlt_bits(qlt) };
    }

    /** @brief A **direct output**: written 1:1 to the register. @p safe is where
     *  it is driven when nothing drives it, which is the whole reason an output
     *  declares a default. */
    constexpr Descriptor MO(const char* name, VarType type, uint8_t device,
                            uint8_t fc, uint16_t reg, uint8_t regs, Scalar safe,
                            uint8_t digits, const char* unit,
                            const char* desc, const char* notes,
                            bool word_swap = false) {
        return Descriptor{ safe, Payload(), name, unit, desc, notes,
                           { reg, device, fc, regs, type, swap_bits(word_swap), 0 },
                           0, 0, K_OUTPUT, type, nullptr, digits, 0 };
    }

    /** @brief An **analog output**: an engineering value converted to a raw
     *  count on the way out, through the same two-point calibration. Device 0 =
     *  a local expansion channel. */
    constexpr Descriptor AO(const char* name, const AnalogCal* cal, uint8_t device,
                            uint8_t fc, uint16_t reg, uint8_t regs, VarType wire,
                            float safe, uint8_t digits, const char* unit,
                            const char* desc, const char* notes,
                            bool word_swap = false) {
        return Descriptor{ Scalar::F(safe), Payload(cal), name, unit, desc, notes,
                           { reg, device, fc, regs, wire,
                             (uint8_t)(B_ANALOG | swap_bits(word_swap)), 0 },
                           0, 0, K_OUTPUT, T_F32, nullptr, digits, 0 };
    }

    /** @brief A **digital output**: a logic-level bit on an expansion channel
     *  (device 0) or over the bus. See RO() for a relay coil - different
     *  hardware, different channel space. */
    constexpr Descriptor DO(const char* name, uint8_t device, uint8_t fc,
                            uint16_t reg, bool safe, bool invert,
                            const char* desc, const char* notes) {
        return Descriptor{ Scalar::B(safe), Payload(), name, "", desc, notes,
                           { reg, device, fc, 1, T_BOOL, 0, 0 },
                           0, 0, K_OUTPUT, T_BOOL, nullptr, 0,
                           (uint8_t)(invert ? F_INVERT : 0) };
    }

    /** @brief A **relay output**: a coil, written with function code 5. Device 0
     *  is an OPTA on-board relay; anything else is a coil on a relay module.
     *
     *  @p safe is the state of the LOGICAL variable when nothing drives it, and
     *  @p invert says how the wiring gets there. The two go together on any
     *  actuator that must fail to its asserted state: a de-energised coil is the
     *  only thing a dead PLC can guarantee, so "open on loss of control" means
     *  the valve is open when the coil is off, which is active-low wiring. */
    constexpr Descriptor RO(const char* name, uint8_t device, uint16_t coil,
                            bool safe, bool invert,
                            const char* desc, const char* notes) {
        return Descriptor{ Scalar::B(safe), Payload(), name, "", desc, notes,
                           { coil, device, 5, 1, T_BOOL, 0, 0 },
                           0, 0, K_OUTPUT, T_BOOL, nullptr, 0,
                           (uint8_t)(invert ? F_INVERT : 0) };
    }

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_DECLARE_H
