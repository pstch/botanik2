#ifndef VARS_IO_H
#define VARS_IO_H

/**
 * @file io.h
 * @brief Typed handles over the variable tables, and the Io accessor control
 *        programs use.
 *
 * A handle is a non-owning pair of (descriptor, value slot), constructed on the
 * fly and passed by value - it costs nothing to make and gives control logic
 * type safety without any offset arithmetic:
 *
 * @code
 * float t = io.ai(AI_EXT_TEMP).value();
 * io.dout(DO_DRAIN_VALVE).set(true);
 * @endcode
 *
 * The index constants are generated per table by the site, so a program names
 * its signals rather than counting bytes.
 */

#include <cstdint>

#include "variable.h"

namespace Botanik { namespace Vars {

    /** @name Handles
     *  @{ */

    /* A handle carries `const Variable*`, not the full descriptor: every
     * accessor it offers lives on the common part, so one handle type serves a
     * field signal and a computed internal alike. The scaling and binding a
     * descriptor adds beyond that are the driver's business, and the driver
     * walks the tables directly. */

    /** @brief Read handle over an analog input. */
    class AnalogIn {
        const Variable*    _d;
        const AnalogValue* _v;
    public:
        AnalogIn(const Variable* d, const AnalogValue* v) : _d(d), _v(v) {}
        /** @brief Engineering value; the default when the reading is invalid. */
        float       value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        uint64_t    timestamp() const { return _v->timestamp; }
        const char* name() const { return _d->name; }
        const char* unit() const { return _d->unit; }
        /** @brief The descriptor, for a generic reader (see access.h). */
        const Variable* meta() const { return _d; }
    };

    /** @brief Read/write handle over an analog output, or over a computed
     *  analog internal (which a program both writes and reads back). */
    class AnalogOut {
        const Variable* _d;
        AnalogValue*    _v;
    public:
        AnalogOut(const Variable* d, AnalogValue* v) : _d(d), _v(v) {}
        float       value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        /** @brief Demand a new value; the write phase pushes it to the field. */
        void        set(float v) { _v->value = v; _v->valid = true; }
        /** @brief Drop to the declared safe state (what a fault does). */
        void        safe() { _v->value = _v->def; _v->valid = false; }
        const char* name() const { return _d->name; }
        const char* unit() const { return _d->unit; }
        const Variable* meta() const { return _d; }
    };

    /** @brief Read handle over a digital input. */
    class DigitalIn {
        const Variable*     _d;
        const DigitalValue* _v;
    public:
        DigitalIn(const Variable* d, const DigitalValue* v) : _d(d), _v(v) {}
        bool        value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        uint64_t    timestamp() const { return _v->timestamp; }
        const char* name() const { return _d->name; }
        const Variable* meta() const { return _d; }
    };

    /** @brief Read/write handle over a digital / relay output, or over a
     *  computed digital internal. */
    class DigitalOut {
        const Variable* _d;
        DigitalValue*   _v;
    public:
        DigitalOut(const Variable* d, DigitalValue* v) : _d(d), _v(v) {}
        bool        value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        void        set(bool v) { _v->value = v; _v->valid = true; }
        void        safe() { _v->value = _v->def; _v->valid = false; }
        const char* name() const { return _d->name; }
        const Variable* meta() const { return _d; }
    };

    /** @} */

    /** @brief The site's variable tables and their value stores, bound
     *  together. Every pointer is borrowed; a site declares all of them as
     *  statics (descriptors const, values mutable).
     *
     *  No default member initialisers: in C++11 an NSDMI stops this being an
     *  aggregate, which forces a runtime constructor call at the site's
     *  descriptor and drags the whole (otherwise const) SiteDescriptor out of
     *  flash into .data with a startup initialiser. Same reasoning as
     *  SiteDescriptor itself. */
    struct IoTables {
        const AnalogInVar*   ai;    AnalogValue*   ai_v;  uint16_t ai_n;
        const AnalogOutVar*  ao;    AnalogValue*   ao_v;  uint16_t ao_n;
        const DigitalInVar*  di;    DigitalValue*  di_v;  uint16_t di_n;
        const DigitalOutVar* dout;  DigitalValue*  do_v;  uint16_t do_n;
        /** Computed globals: no binding, no scaling, so the descriptor is the
         *  bare Variable. Everything the firmware derives rather than reads -
         *  setpoints, states, and the alarm conditions - lives here, which is
         *  what lets the site describe itself without a payload struct. */
        const Variable*      ia;    AnalogValue*   ia_v;  uint16_t ia_n;
        const Variable*      id;    DigitalValue*  id_v;  uint16_t id_n;
    };

    /**
     * @brief What control programs are handed to reach their variables.
     *
     * Indices come from the site's generated enums; they are the array index,
     * so a lookup is a bounds check and two pointer adds.
     *
     * An out-of-range index - or any index into an empty table - resolves to an
     * inert sink rather than faulting or clamping. Clamping was worse than it
     * looked: it aliased a bad index onto the LAST entry, so a typo would drive
     * a real relay. The sink absorbs reads and writes harmlessly, so a mistake
     * shows up as a signal that never moves instead of the wrong actuator
     * firing.
     */
    class Io {
        IoTables _t;
    public:
        Io() {}
        explicit Io(const IoTables& t) : _t(t) {}

        /** @brief Parse every declared default into its value slot and start
         *  invalid, so a variable reads as its default until a driver (or a
         *  program) gives it a real value. */
        void init();

        /** @brief The bound tables. Used by the console to enumerate variables;
         *  programs address them by index through the handles instead. */
        const IoTables& tables() const { return _t; }

        /** @name Typed access
         *  @{ */
        AnalogIn ai(uint16_t i) const {
            if (i < _t.ai_n) return AnalogIn(&_t.ai[i].var, &_t.ai_v[i]);
            _sink_a = AnalogValue();     // fresh: a sink never accumulates state
            return AnalogIn(&_sink_desc, &_sink_a);
        }
        AnalogOut ao(uint16_t i) const {
            if (i < _t.ao_n) return AnalogOut(&_t.ao[i].var, &_t.ao_v[i]);
            _sink_a = AnalogValue();
            return AnalogOut(&_sink_desc, &_sink_a);
        }
        DigitalIn di(uint16_t i) const {
            if (i < _t.di_n) return DigitalIn(&_t.di[i].var, &_t.di_v[i]);
            _sink_d = DigitalValue();
            return DigitalIn(&_sink_desc, &_sink_d);
        }
        DigitalOut dout(uint16_t i) const {
            if (i < _t.do_n) return DigitalOut(&_t.dout[i].var, &_t.do_v[i]);
            _sink_d = DigitalValue();
            return DigitalOut(&_sink_desc, &_sink_d);
        }
        /** @brief A computed analog global. Read/write: whoever derives it
         *  writes it, everyone else observes it. */
        AnalogOut ia(uint16_t i) const {
            if (i < _t.ia_n) return AnalogOut(&_t.ia[i], &_t.ia_v[i]);
            _sink_a = AnalogValue();
            return AnalogOut(&_sink_desc, &_sink_a);
        }
        /** @brief A computed digital global (an alarm condition, a state flag). */
        DigitalOut id(uint16_t i) const {
            if (i < _t.id_n) return DigitalOut(&_t.id[i], &_t.id_v[i]);
            _sink_d = DigitalValue();
            return DigitalOut(&_sink_desc, &_sink_d);
        }
        /** @} */

        /** @brief The inert descriptor an unresolvable reference renders as.
         *  Never null, so a generic reader needs no null check. */
        const Variable* none() const { return &_sink_desc; }

        /** @brief Drop every output to its declared safe state. The write phase
         *  calls this on a fault, watchdog escalation or scan overrun. */
        void all_outputs_safe();

        /** @brief Expire inputs whose last good read is older than their
         *  declared timeout. Called once per beat by the read phase, after the
         *  drivers have had their turn. */
        void expire_stale(uint64_t now_ms);

    private:
        /** @brief The inert sink an out-of-range index resolves to. Reset on
         *  every such access, so a write to one bad index is never visible to
         *  the next reader - the sink is scratch, not shared state. Only the
         *  error path pays for the reset. One descriptor covers all six tables
         *  now that a handle only ever sees the common part. */
        static const Variable _sink_desc;
        static AnalogValue    _sink_a;
        static DigitalValue   _sink_d;
    };

}}  // namespace Botanik::Vars

#endif // VARS_IO_H
