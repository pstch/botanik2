#ifndef VARS_DECLARE_H
#define VARS_DECLARE_H

/**
 * @file declare.h
 * @brief One constexpr factory per VarScope: how a site spells a row.
 *
 * A site's variable table is a list of these calls. Each factory takes ONLY the
 * arguments its scope can use, so the things that used to be expressible and
 * meaningless are now unspellable: a relay with a plausibility band, an alarm
 * with a Modbus register, a parameter with a timeout. The fields those rows do
 * not carry are not written as zeros in the table - they are absent from the
 * call, and the factory supplies them.
 *
 * That replaces the site-local row macros (MTR / COIL / IBOOL / IREAL / PARAM),
 * which had the same intent but lived in one site, could not be type-checked,
 * and had to be `#undef`ed after the table.
 *
 * @section declare_flash Why constexpr, and why one return
 *
 * These build the site's `const Variable g_vars[]`, which must be
 * constant-initialised straight into flash: a runtime call in that initialiser
 * would drag ~20 KB of table into `.data` behind a startup loop. A C++11
 * constexpr function body must be a SINGLE return statement, so each one below
 * is exactly that - an aggregate initialiser, no locals and no branches.
 *
 * @section declare_local Local I/O
 *
 * There is no separate scope for a local channel. A local row is one whose
 * Modbus device address is 0, and the factories derive `transport` from that,
 * so the two can never disagree. `AI(...)`, `DI(...)`, `AO(...)`, `DO(...)` and
 * `RO(...)` all take a device: pass 0 and the row is local, pass a unit id and
 * it is on the bus.
 *
 * @section declare_names Why the names are abbreviations
 *
 * The table is meant to be READ as a table - one row per line, columns aligned.
 * A four-word factory name would push every row past the width where that stops
 * working. The scope enum uses the same abbreviations, so a row and the value it
 * filters on are spelled the same way.
 */

#include <cstdint>                    // uint8_t/uint16_t/int64_t - the factories' literal arguments

#include "libbotanik/vars/model.h"    // Vars::Variable / Tunable / AnalogCal - what the factories build

namespace Botanik { namespace Vars {

    /** @name Scalar shorthands
     *
     *  A bare literal cannot build a Scalar: the union takes uint64 / int64 /
     *  float / const char* equally well, so the type has to be said. These are
     *  Scalar::U/I/F/B under names short enough to keep a table row on one line.
     *  @{ */

    constexpr Scalar SU(uint64_t v) { return Scalar::U(v); }
    constexpr Scalar SI(int64_t v)  { return Scalar::I(v); }
    constexpr Scalar SF(float v)    { return Scalar::F(v); }
    constexpr Scalar SB(bool v)     { return Scalar::B(v); }
    /// Text. Named ST, not SS: Mbed defines SS (SPI slave select), and the
    /// site table has to compile inside an Arduino sketch.
    constexpr Scalar ST(const char* v) { return Scalar::S(v); }

    /** @brief Build a Scalar of @p t from a plain number.
     *
     *  A parameter row already says its type, so making it repeat itself in
     *  every one of its three value cells - SU(20), SU(10), SU(1800) - was three
     *  wrappers per row saying what the column beside them already said. This
     *  reads the type once and picks the arm, which is why P() below takes
     *  literals. Single-return, so the table stays constant-initialised. */
    constexpr Scalar num(VarType t, double v) {
        return type_real(t)   ? Scalar::F((float)v)
             : type_signed(t) ? Scalar::I((int64_t)v)
                              : Scalar::U((uint64_t)v);
    }

    /** @} */

    /** @name Calibration tunables
     *
     *  Each field of an AnalogCal is one of these: K() pins it in firmware,
     *  R() points it at a parameter an operator can edit. R() also takes the
     *  value to fall back on if the parameter is missing, so a typo degrades to
     *  a sane number instead of to zero.
     *  @{ */

    /** @brief A fixed value: this one is not the operator's to change. */
    constexpr Tunable K(float v) { return Tunable{v, nullptr}; }
    /** @brief Read it from parameter @p name, falling back to @p dflt. */
    constexpr Tunable R(const char* name, float dflt = 0.0f) { return Tunable{dflt, name}; }

    /** @} */

    /** @name Row factories
     *
     *  Argument order is the same everywhere it applies: identity (name), then
     *  what it reads or drives, then the band, then presentation (digits, unit),
     *  then prose (desc, notes). Every row ends with desc and notes; notes is
     *  never optional, because "" says "nothing more to add" in the same shape
     *  as a real note.
     *  @{ */

    /** @brief Modbus RTU device 0 means the row is wired locally rather than on
     *  the bus. One place decides it, so `transport` cannot contradict the
     *  address. */
    constexpr Transport xp_of(uint8_t device) { return device == 0 ? XP_LOCAL : XP_MODBUS; }

    /** @brief An **alarm**: the boolean carrying the condition, AND the policy
     *  the AlarmController runs it on. There is no second alarm table - the model
     *  builds its working conditions by scanning the variable table for
     *  SCOPE_A rows, in table order, so an alarm is declared once.
     *
     *  @p code is the short display code (max 8 chars) the ALARMS table's first
     *  column and the alarm bar show; identity still belongs to @p name.
     *  @p on_s / @p off_s are the dwell before a START / END is announced, -1
     *  to never announce that edge. @p recent_s is the RECENT filter window
     *  (0 = the default week, <0 = never expires). @p hold_on_s holds the
     *  alarm ON that long after the raw bit clears, and @p min_on_s is the
     *  minimum ON time from onset - both anti-flicker, in seconds.
     *
     *  @p reg is its register on the Modbus TCP slave, hand-assigned like a
     *  parameter's and in the same space, but in a block of its own (a site's
     *  alarms in 800-899, the library's at the top of its 900-999 reserve) so
     *  the number itself says what kind of row it is. An alarm is published as a
     *  read-only coil; acknowledging one is a write to a separate block, not to
     *  the alarm's own bit.
     *
     *  Carries no band and no unit: it is raised by a control program and read
     *  by the alarm model, and the binding says where it is published, not where
     *  it comes from. */
    constexpr Variable A(const char* name, const char* code,
                         int16_t on_s, int16_t off_s, int16_t recent_s,
                         int16_t hold_on_s, int16_t min_on_s, uint16_t reg,
                         const char* desc, const char* notes) {
        return Variable{ Scalar::B(false), name, "", desc, notes,
                         Payload(code, on_s, off_s, recent_s, hold_on_s, min_on_s),
                         { XP_NONE, T_BOOL, 0, 0, reg, 1, 1.0f, 0.0f }, 0, nullptr,
                         T_BOOL, ROLE_COMPUTED, SCOPE_A, 0, 0, {} };
    }

    /** @brief A numeric **parameter**: persisted, editable, always valid.
     *
     *  @p def, @p lo and @p hi are plain numbers - the row's @p type is what
     *  decides whether they land as unsigned, signed or float, so a cell says a
     *  value and nothing else. @p reg is
     *  the row's register on the Modbus TCP slave, hand-assigned and NOT derived
     *  from its position: an external master's register map is a contract, so
     *  inserting a parameter must not renumber the ones after it. That is the
     *  one number a site still writes down, and the site test asserts no two
     *  share it. */
    constexpr Variable P(const char* name, VarType type, double def, double lo, double hi,
                         uint8_t digits, const char* unit, uint16_t reg,
                         const char* desc, const char* notes) {
        return Variable{ num(type, def), name, unit, desc, notes,
                         Payload(num(type, lo), num(type, hi)),
                         { XP_NONE, type, 0, 0, reg, 1, 1.0f, 0.0f }, 0, nullptr,
                         type, ROLE_PARAM, SCOPE_P, digits, F_BOUNDED, {} };
    }

    /** @brief A text **parameter**. Its own factory rather than a column set on
     *  P(): bounds mean nothing for a string and a pool slot means nothing for a
     *  number, so one shape carrying both made every row spell four cells that
     *  never applied to it, and the block stopped reading as a table.
     *
     *  @p cap is the slot size including the NUL; WHERE the slot sits is not
     *  declared, Bank::init() lays them out in table order. @p kind is what a
     *  new value is validated against. */
    constexpr Variable PS(const char* name, const char* def, uint16_t cap, StrKind kind,
                          uint16_t reg, const char* desc, const char* notes) {
        return Variable{ Scalar::S(def), name, "", desc, notes, Payload(cap, kind),
                         { XP_NONE, T_STR, 0, 0, reg, 1, 1.0f, 0.0f }, 0, nullptr,
                         T_STR, ROLE_PARAM, SCOPE_P, 0, 0, {} };
    }

    /** @brief A **computed** value: derived by a control program, shown on the
     *  HMI and nowhere else. Rename it freely - nothing outside the firmware
     *  knows its name. Use SV() instead the moment it is published. */
    constexpr Variable G(const char* name, VarType type, Scalar def, uint8_t digits,
                         const char* unit, const char* desc, const char* notes) {
        return Variable{ def, name, unit, desc, notes,
                         Payload(Scalar::U(0), Scalar::U(0)),
                         { XP_NONE, type, 0, 0, 0, 0, 1.0f, 0.0f }, 0, nullptr,
                         type, ROLE_COMPUTED, SCOPE_G, digits, 0, {} };
    }

    /** @brief A **status value**: computed like G(), but PUBLISHED - MQTT
     *  telemetry and the Modbus TCP slave. Its name and its scaling are an
     *  external contract, so renaming one breaks whatever is subscribed.
     *
     *  @p reg is its register on that slave, hand-assigned for the same reason a
     *  parameter's is: the map an external master holds must not renumber when a
     *  row is inserted. Parameters and status values share one register space,
     *  and the site test asserts no two rows claim the same one. */
    constexpr Variable SV(const char* name, VarType type, double def, uint8_t digits,
                          const char* unit, uint16_t reg,
                          const char* desc, const char* notes) {
        return Variable{ num(type, def), name, unit, desc, notes,
                         Payload(Scalar::U(0), Scalar::U(0)),
                         { XP_NONE, type, 0, 0, reg, 1, 1.0f, 0.0f }, 0, nullptr,
                         type, ROLE_COMPUTED, SCOPE_SV, digits, 0, {} };
    }

    /** @brief A **direct input**: the register maps 1:1 onto @p type, no
     *  conversion. @p lo / @p hi are the plausibility band - generous on
     *  purpose, to catch a dead meter reporting a rail rather than to police
     *  the process. A read older than @p timeout_ms marks the row invalid, and
     *  @p def is what it reads then - a safety-relevant choice, so it is
     *  spelled per row rather than assumed to be zero. */
    constexpr Variable MI(const char* name, VarType type, uint8_t device, uint8_t fc,
                          uint16_t reg, uint16_t regs, Scalar lo, Scalar hi, Scalar def,
                          uint32_t timeout_ms, uint8_t digits, const char* unit,
                          const char* desc, const char* notes) {
        return Variable{ def, name, unit, desc, notes, Payload(lo, hi),
                         { xp_of(device), type, device, fc, reg, regs, 1.0f, 0.0f },
                         timeout_ms, nullptr,
                         type, ROLE_INPUT, SCOPE_MI, digits, F_BOUNDED, {} };
    }

    /** @brief An **analog input**: a raw count on the wire, converted to the
     *  engineering value by the two-point calibration in @p cal. Device 0 = a
     *  local expansion channel.
     *
     *  @p cal carries the endpoints AND the plausibility band, each of them a
     *  constant or a parameter, and it may be SHARED with any other channel
     *  calibrated identically. There is no scale/offset argument: those are
     *  derived from the endpoints, so there is nothing to recompute by hand
     *  when a sensor is re-trimmed. */
    constexpr Variable AI(const char* name, const AnalogCal* cal, uint8_t device,
                          uint8_t fc, uint16_t reg, uint16_t regs, VarType wire,
                          Scalar def, uint32_t timeout_ms, uint8_t digits,
                          const char* unit, const char* desc, const char* notes) {
        return Variable{ def, name, unit, desc, notes, Payload(cal),
                         { xp_of(device), wire, device, fc, reg, regs, 1.0f, 0.0f },
                         timeout_ms, nullptr,
                         T_F32, ROLE_INPUT, SCOPE_AI, digits, F_BOUNDED, {} };
    }

    /** @brief A **digital input**: one bit. @p invert folds active-low wiring in
     *  here, so the control programs above read true = asserted. Device 0 = a
     *  local input bit. */
    constexpr Variable DI(const char* name, uint8_t device, uint8_t fc, uint16_t reg,
                          bool def, uint32_t timeout_ms, bool invert,
                          const char* desc, const char* notes) {
        return Variable{ Scalar::B(def), name, "", desc, notes,
                         Payload(Scalar::B(false), Scalar::B(true)),
                         { xp_of(device), T_BOOL, device, fc, reg, 1, 1.0f, 0.0f },
                         timeout_ms, nullptr,
                         T_BOOL, ROLE_INPUT, SCOPE_DI, 0, (uint8_t)(invert ? F_INVERT : 0), {} };
    }

    /** @brief A **direct output**: written 1:1 to the register. @p safe is where
     *  it is driven when control is withdrawn, which is the whole reason an
     *  output declares a default. */
    constexpr Variable MO(const char* name, VarType type, uint8_t device, uint8_t fc,
                          uint16_t reg, uint16_t regs, Scalar safe, uint8_t digits,
                          const char* unit, const char* desc, const char* notes) {
        return Variable{ safe, name, unit, desc, notes,
                         Payload(Scalar::U(0), Scalar::U(0)),
                         { xp_of(device), type, device, fc, reg, regs, 1.0f, 0.0f }, 0, nullptr,
                         type, ROLE_OUTPUT, SCOPE_MO, digits, 0, {} };
    }

    /** @brief An **analog output**: an engineering value converted to a raw
     *  count on the way out, through the same two-point calibration. Device 0 =
     *  a local expansion channel. @p safe is where it is driven when control is
     *  withdrawn. */
    constexpr Variable AO(const char* name, const AnalogCal* cal, uint8_t device,
                          uint8_t fc, uint16_t reg, uint16_t regs, VarType wire,
                          float safe, uint8_t digits, const char* unit,
                          const char* desc, const char* notes) {
        return Variable{ Scalar::F(safe), name, unit, desc, notes, Payload(cal),
                         { xp_of(device), wire, device, fc, reg, regs, 1.0f, 0.0f },
                         0, nullptr,
                         T_F32, ROLE_OUTPUT, SCOPE_AO, digits, F_BOUNDED, {} };
    }

    /** @brief A **digital output**: a logic-level bit on an expansion channel
     *  (device 0) or over the bus. See RO() for a relay coil - different
     *  hardware, different channel space. */
    constexpr Variable DO(const char* name, uint8_t device, uint8_t fc, uint16_t reg,
                          bool safe, bool invert, const char* desc, const char* notes) {
        return Variable{ Scalar::B(safe), name, "", desc, notes,
                         Payload(Scalar::B(false), Scalar::B(true)),
                         { xp_of(device), T_BOOL, device, fc, reg, 1, 1.0f, 0.0f }, 0, nullptr,
                         T_BOOL, ROLE_OUTPUT, SCOPE_DO, 0,
                         (uint8_t)(invert ? F_INVERT : 0), {} };
    }

    /** @brief A **relay output**: a coil, written with function code 5. Device 0
     *  is an OPTA on-board relay; anything else is a coil on a relay module.
     *
     *  @p safe is the state of the LOGICAL variable when control is withdrawn,
     *  and @p invert says how the wiring gets there. The two go together on any
     *  actuator that must fail to its asserted state: a de-energised coil is the
     *  only thing a dead PLC can guarantee, so "open on loss of control" means
     *  the valve is open when the coil is off, which is active-low wiring.
     *  Declaring @p safe true without @p invert describes a valve that shuts the
     *  moment the firmware stops driving it. */
    constexpr Variable RO(const char* name, uint8_t device, uint16_t coil, bool safe,
                          bool invert, const char* desc, const char* notes) {
        return Variable{ Scalar::B(safe), name, "", desc, notes,
                         Payload(Scalar::B(false), Scalar::B(true)),
                         { xp_of(device), T_BOOL, device, 5, coil, 1, 1.0f, 0.0f }, 0, nullptr,
                         T_BOOL, ROLE_OUTPUT, SCOPE_RO, 0,
                         (uint8_t)(invert ? F_INVERT : 0), {} };
    }

    /** @} */

    /** @brief A scope's column label: the abbreviation itself.
     *
     *  The code rather than a phrase, because it is the same token the row was
     *  declared with and the same one the enum is named after - one vocabulary
     *  instead of three. It also stops the column needing eight characters to
     *  say what two do, and the detail panel below the table still spells the
     *  binding out in full ("Modbus dev 5 fc 3 reg 0"). */
    inline const char* scope_label(VarScope s) {
        switch (s) {
            case SCOPE_A:  return "A";
            case SCOPE_P:  return "P";
            case SCOPE_G:  return "G";
            case SCOPE_SV: return "SV";
            case SCOPE_MI: return "MI";
            case SCOPE_AI: return "AI";
            case SCOPE_DI: return "DI";
            case SCOPE_MO: return "MO";
            case SCOPE_AO: return "AO";
            case SCOPE_DO: return "DO";
            case SCOPE_RO: return "RO";
            default:       return "?";
        }
    }

}}  // namespace Botanik::Vars

#endif // VARS_DECLARE_H
