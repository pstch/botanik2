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

namespace Botanik { namespace Vars {

    /** @name Descriptor
     *  @{ */

    /** @brief Where a variable's value comes from. This is the axis the model
     *  is organised on - not analog/digital, which is a property of the wire and
     *  of the value type respectively. */
    enum Transport : uint8_t {
        XP_NONE,            ///< nothing drives it: a computed global or a parameter
        XP_LOCAL_DIGITAL,   ///< an OPTA relay coil / digital input bit
        XP_LOCAL_ANALOG,    ///< a raw count from an AI-1 / AO-1 expansion channel
        XP_MODBUS           ///< Modbus RTU; any wire type
    };

    /** @brief What a variable is FOR. Drives behaviour that the type system used
     *  to encode, so the rules live in one switch instead of in six tables. */
    enum VarRole : uint8_t {
        ROLE_INPUT,      ///< read from the field; goes invalid past its timeout
        ROLE_OUTPUT,     ///< driven to the field; dropped to `def` on a fault
        ROLE_INTERNAL,   ///< computed; never expires, never swept
        ROLE_PARAM       ///< persisted, editable, and always valid
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

    /** @brief Every variable, whatever its kind.
     *
     *  @c min and @c max serve two jobs by role: a parameter's EDIT BOUNDS and
     *  an input's PLAUSIBILITY BAND (a reading outside it is implausible - a
     *  disconnected sensor usually reads a rail - and marks the variable invalid
     *  rather than feeding rubbish downstream). Meaningful only with F_BOUNDED.
     *
     *  Ordered widest-first so the row carries no implicit padding. */
    struct Variable {
        Scalar      min, max, def;
        const char* name;      ///< symbolic name; the telemetry leaf; the storage key
        const char* unit;      ///< "C", "%", "" for none
        const char* desc;
        /** @brief T_ENUM renderer; null for a plain number. */
        const char* (*str)(uint64_t);
        Binding     bind;
        uint32_t    timeout_ms;///< ROLE_INPUT: no good read within this -> invalid; 0 = never
        uint16_t    str_off;   ///< T_STR: byte offset into the site's char pool
        uint16_t    str_cap;   ///< T_STR: capacity including the NUL
        VarType     type;
        VarRole     role;
        uint8_t     digits;    ///< T_F32 display decimals
        StrKind     kind;      ///< T_STR edit validation
        uint8_t     flags;     ///< VarFlags
        uint8_t     _rsv[3];
    };

    /** @brief One value slot, parallel to the descriptor table.
     *
     *  No `def` here: the default is a typed literal in flash, so an invalid
     *  read falls back to it without a per-slot copy. */
    struct Value {
        Scalar   v;
        uint64_t stamp_ms;   ///< uptime ms of the last good update
        bool     valid;
        uint8_t  _rsv[7];
    };

    /** @brief A reference to any variable: a bare index into the one table.
     *
     *  A struct rather than a typedef so a stray integer cannot become a
     *  reference by accident. */
    struct VarRef { uint16_t index; };

    /** @brief The "no variable" index. Deliberately NOT 0: a zero-initialised
     *  VarRef - which is how the platform-health alarms spell "this one reads no
     *  variable" - must not alias the first real row. */
    static const uint16_t VAR_NONE = 0xFFFFu;

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
         *  @{ */
        Scalar   raw()   const { return _v->valid ? _v->v : _d->def; }
        /** @brief As a float; a digital reads 0/1, so a consumer that only
         *  displays or publishes never branches on the type. */
        float    value() const { return as_f32(_d->type, raw()); }
        bool     b()     const { return as_bool(_d->type, raw()); }
        int64_t  i()     const { return as_i64(_d->type, raw()); }
        bool     valid() const { return _v->valid; }
        uint64_t stamp() const { return _v->stamp_ms; }
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
        /** ROLE_PARAM rows are a contiguous block, so the parameter pages can
         *  address them with a dense 0-based index and one add. */
        uint16_t        param_first;
        uint16_t        param_count;
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
        VarTables _t;
    public:
        Bank() {}
        explicit Bank(const VarTables& t) : _t(t) {}

        /** @brief Seed every slot from its declared default, copy string
         *  defaults into the pool, and set the initial validity per role. */
        void init();

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

        /** @brief Parameter @p k, 0-based over the ROLE_PARAM block. */
        Var      param(uint16_t k) const { return var((uint16_t)(_t.param_first + k)); }
        uint16_t param_count() const     { return _t.param_count; }

        /** @brief Find a variable by name. Returns VAR_NONE when absent, which
         *  resolves to the sink, so a caller may skip the check. */
        VarRef find(const char* name) const;
        /** @brief Length-bounded form, for a shell argument that is not
         *  NUL-terminated. */
        VarRef find(const char* name, uint16_t len) const;

        /** @brief Drop every ROLE_OUTPUT to its declared safe state.
         *
         *  Outputs only. An internal is derived state - an alarm condition, a
         *  latched interlock - and clearing it would erase the very diagnosis
         *  that put the firmware into safe mode. A parameter is configuration
         *  and sweeping it would revert the network settings on a scan overrun. */
        void all_outputs_safe();

        /** @brief Invalidate ROLE_INPUTs whose last good read is older than
         *  their declared timeout. A timeout of 0 never expires, for a value a
         *  driver publishes only on change. */
        void expire_stale(uint64_t now_ms);

    private:
        /** @brief The inert sink a bad index resolves to. Reset on every such
         *  access, so a write to one bad index is never visible to the next
         *  reader - the sink is scratch, not shared state. */
        static const Variable _sink_d;
        static Value          _sink_v;
    };

    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_MODEL_H
