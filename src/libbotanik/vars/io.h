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
#include <cstring>

#include "variable.h"

namespace Botanik { namespace Vars {

    /** @name Handles
     *  @{ */

    /** @brief Read handle over an analog input. */
    class AnalogIn {
        const AnalogInVar* _d;
        const AnalogValue* _v;
    public:
        AnalogIn(const AnalogInVar* d, const AnalogValue* v) : _d(d), _v(v) {}
        /** @brief Engineering value; the default when the reading is invalid. */
        float       value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        uint64_t    timestamp() const { return _v->timestamp; }
        const char* name() const { return _d->var.name; }
        const char* unit() const { return _d->var.unit; }
    };

    /** @brief Read/write handle over an analog output. */
    class AnalogOut {
        const AnalogOutVar* _d;
        AnalogValue*        _v;
    public:
        AnalogOut(const AnalogOutVar* d, AnalogValue* v) : _d(d), _v(v) {}
        float       value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        /** @brief Demand a new value; the write phase pushes it to the field. */
        void        set(float v) { _v->value = v; _v->valid = true; }
        /** @brief Drop to the declared safe state (what a fault does). */
        void        safe() { _v->value = _v->def; _v->valid = false; }
        const char* name() const { return _d->var.name; }
    };

    /** @brief Read handle over a digital input. */
    class DigitalIn {
        const DigitalInVar* _d;
        const DigitalValue* _v;
    public:
        DigitalIn(const DigitalInVar* d, const DigitalValue* v) : _d(d), _v(v) {}
        bool        value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        uint64_t    timestamp() const { return _v->timestamp; }
        const char* name() const { return _d->var.name; }
    };

    /** @brief Read/write handle over a digital output. */
    class DigitalOut {
        const DigitalOutVar* _d;
        DigitalValue*        _v;
    public:
        DigitalOut(const DigitalOutVar* d, DigitalValue* v) : _d(d), _v(v) {}
        bool        value() const { return _v->valid ? _v->value : _v->def; }
        bool        valid() const { return _v->valid; }
        void        set(bool v) { _v->value = v; _v->valid = true; }
        void        safe() { _v->value = _v->def; _v->valid = false; }
        const char* name() const { return _d->var.name; }
    };

    /** @} */

    /** @brief The site's variable tables and their value stores, bound
     *  together. Every pointer is borrowed; a site declares all of them as
     *  statics (descriptors const, values mutable). */
    struct IoTables {
        const AnalogInVar*   ai   = nullptr;  AnalogValue*   ai_v  = nullptr;  uint16_t ai_n  = 0;
        const AnalogOutVar*  ao   = nullptr;  AnalogValue*   ao_v  = nullptr;  uint16_t ao_n  = 0;
        const DigitalInVar*  di   = nullptr;  DigitalValue*  di_v  = nullptr;  uint16_t di_n  = 0;
        const DigitalOutVar* dout = nullptr;  DigitalValue*  do_v  = nullptr;  uint16_t do_n  = 0;
        const InternalVar*   iv   = nullptr;  InternalValue* iv_v  = nullptr;  uint16_t iv_n  = 0;
    };

    /**
     * @brief What control programs are handed to reach their variables.
     *
     * Indices come from the site's generated enums; they are the array index,
     * so a lookup is a bounds check and two pointer adds. An out-of-range index
     * is clamped to the last entry rather than faulting - a program with a bad
     * index is a build-time mistake, and taking the firmware down mid-scan is
     * the worse failure.
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

        /** @name Typed access
         *  @{ */
        AnalogIn   ai(uint16_t i)   const { i = _clamp(i, _t.ai_n);  return AnalogIn(&_t.ai[i], &_t.ai_v[i]); }
        AnalogOut  ao(uint16_t i)   const { i = _clamp(i, _t.ao_n);  return AnalogOut(&_t.ao[i], &_t.ao_v[i]); }
        DigitalIn  di(uint16_t i)   const { i = _clamp(i, _t.di_n);  return DigitalIn(&_t.di[i], &_t.di_v[i]); }
        DigitalOut dout(uint16_t i) const { i = _clamp(i, _t.do_n);  return DigitalOut(&_t.dout[i], &_t.do_v[i]); }
        /** @} */

        const IoTables& tables() const { return _t; }

        /** @brief Drop every output to its declared safe state. The write phase
         *  calls this on a fault, watchdog escalation or scan overrun. */
        void all_outputs_safe();

        /** @brief Expire inputs whose last good read is older than their
         *  declared timeout. Called once per beat by the read phase, after the
         *  drivers have had their turn. */
        void expire_stale(uint64_t now_ms);

    private:
        static uint16_t _clamp(uint16_t i, uint16_t n) { return (n == 0) ? 0 : (i < n ? i : (uint16_t)(n - 1)); }
    };

}}  // namespace Botanik::Vars

#endif // VARS_IO_H
