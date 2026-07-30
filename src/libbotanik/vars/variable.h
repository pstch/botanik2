#ifndef VARS_VARIABLE_H
#define VARS_VARIABLE_H

/**
 * @file variable.h
 * @brief The variable model: descriptor tables (flash) and value stores (RAM).
 *
 * Everything the firmware exposes is a *variable*: field IO, computed globals,
 * and parameters. A variable is split in two, deliberately:
 *
 *  - its **descriptor** is `const` and lives in flash, generated from the site's
 *    project. Plain aggregates only: no constructors and no virtuals, because a
 *    type with a vptr is not a literal type in C++11 and would land the whole
 *    table in .bss with a startup initialiser.
 *  - its **value** lives in a parallel RAM array, indexed the same way.
 *
 * That split is what lets the tables stay in flash while the values move, and it
 * makes a per-beat snapshot a contiguous memcpy rather than a walk over objects.
 *
 * @section var_default The default does three jobs
 *
 * Every variable carries `def` as text, parsed once at init. It is the input's
 * substitute when a read is invalid, the output's safe state on a fault, and a
 * parameter's factory value. One field, three uses, so there is no separate
 * fallback or safe-state concept to keep in sync.
 *
 * @section var_valid Validity
 *
 * `valid` is a plain bool. **An invalid variable reads as its default**, so
 * control logic that does not care about quality still gets a sane number, and
 * logic that does care asks `valid()`. An input goes invalid on a read timeout
 * or an out-of-band reading; an output on a driver fault.
 */

#include <cstdint>

#include "../core/scalar.h"   // ScalarType - reused as the raw wire type

namespace Botanik { namespace Vars {

    /** @name Common
     *  @{ */

    /** @brief Where a variable's value comes from. */
    enum Transport : uint8_t {
        IO_LOCAL,        ///< onboard channel; `reg` is the channel index
        IO_MODBUS_RTU    ///< Modbus RTU; `device`/`reg`/`fc` address it
    };


    /** @brief Which table a VarRef points into. */
    enum VarKind : uint8_t {
        VAR_ANALOG_IN, VAR_ANALOG_OUT, VAR_DIGITAL_IN, VAR_DIGITAL_OUT,
        VAR_INTERNAL,  VAR_PARAM
    };

    /** @brief A reference to any variable, by table and index.
     *
     *  The intended addressing for status groups, home fields and alarm
     *  conditions. Those still address the site data struct by byte offset -
     *  moving them over is the remaining half of the migration, and until it
     *  lands this type has no users. */
    struct VarRef { VarKind kind; uint16_t index; };

    /** @brief What every variable carries, whatever its kind. */
    struct Variable {
        const char* name;      ///< symbolic name; also the telemetry leaf
        const char* unit;      ///< "C", "%", "" for none
        const char* desc;
        const char* notes;
        /** @brief Default, as text, parsed once at init. Input substitute,
         *  output safe state, parameter factory value. */
        const char* def;
    };

    /** @brief How a driver reaches a variable. `device`/`fc` are unused for
     *  IO_LOCAL, where `reg` is the onboard channel index. */
    struct IoBinding {
        Transport transport;
        uint8_t   device;      ///< Modbus unit id
        uint16_t  reg;         ///< register address, or local channel
        uint8_t   fc;          ///< Modbus function code
    };

    /** @} */

    /** @name Descriptor tables (const, flash)
     *  @{ */

    /** @brief Analog input. Engineering value = raw * scale + offset, so logic
     *  never sees counts. A reading outside [low, high] is implausible - a
     *  disconnected sensor usually reads a rail - and marks the variable
     *  invalid rather than feeding rubbish downstream. */
    struct AnalogInVar {
        Variable   var;
        IoBinding  bind;
        ScalarType raw;              ///< wire type read from the device
        float      scale, offset;
        float      low, high;        ///< plausibility band
        uint32_t   timeout_ms;       ///< no good read within this -> invalid
    };

    /** @brief Analog output. The same scaling, applied in reverse on the way
     *  out. */
    struct AnalogOutVar {
        Variable   var;
        IoBinding  bind;
        ScalarType raw;
        float      scale, offset;
    };

    /** @brief Digital input; `invert` folds in active-low wiring so logic can
     *  read every signal as true-means-asserted. */
    struct DigitalInVar {
        Variable  var;
        IoBinding bind;
        bool      invert;
        uint32_t  timeout_ms;
    };

    /** @brief Digital / relay output. */
    struct DigitalOutVar {
        Variable  var;
        IoBinding bind;
        bool      invert;
    };


    /** @} */

    /** @name Value stores (RAM, parallel to their table)
     *  @{ */

    /** @brief One analog value. @c def holds the parsed default so an invalid
     *  read can fall back to it without re-parsing text every scan. */
    struct AnalogValue {
        float    value;
        float    def;
        uint64_t timestamp;    ///< uptime ms of the last good update
        bool     valid;
    };

    /** @brief One digital value. */
    struct DigitalValue {
        bool     value;
        bool     def;
        uint64_t timestamp;
        bool     valid;
    };


    /** @} */

}}  // namespace Botanik::Vars

#endif // VARS_VARIABLE_H
