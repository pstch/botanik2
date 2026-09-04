#ifndef BOTANIK_IEC_ACCESSOR_H
#define BOTANIK_IEC_ACCESSOR_H
/** @file  matiec's accessor macros, redefined onto Vars::Bank.
 *
 *  matiec's generated C reaches every variable through these seven macros and
 *  never touches storage directly, so replacing the header is what makes its
 *  output address OUR rows instead of its own `{value, flags}` structs.
 *
 *  Two kinds of variable, and the split is the whole design: VAR_EXTERNAL
 *  becomes a `Ref` into the bank and `__INIT_EXTERNAL` is the generated
 *  `bind()`; everything internal to a POU stays a plain member.
 */
#include "libbotanik/plc/health.h"
#include "libbotanik/vars/model.h"
#include "libbotanik/plc/runtime/health.gen.h"
#include "libbotanik/plc/runtime/quality.gen.h"
#include "iec_std_lib.h"

// Every name below is matiec's, not ours: its generated C calls these exact
// macros, so the leading underscores are an ABI rather than a style. That is
// why .clang-tidy turns bugprone-reserved-identifier off for the whole tree.

/** @name POU-internal variables - plain members, never rows
 *
 *  VAR, VAR_TEMP and a FUNCTION_BLOCK's VAR_INPUT/VAR_OUTPUT are internal to
 *  the POU: nothing outside names them, nothing displays them, no transport
 *  carries them. So they are ordinary C++ members, exactly as `Plc::Tof` has
 *  them today - no row, no descriptor, no pool.
 *
 *  Only VAR_EXTERNAL becomes a `Ref`, because only it names a row the site
 *  declares. That split is what removes the anonymous-row problem entirely.
 *  @{ */

#define __DECLARE_VAR(type, name)            type name;
#define __INIT_VAR(name, initial, retained)  (name) = (initial);
#define __GET_VAR(name, ...)                 ((name) __VA_ARGS__)
#define __SET_VAR(prefix, name, suffix, new_value) \
    (prefix name suffix) = (new_value)
#define __GET_VAR_BY_REF(name, ...)          (&((name) __VA_ARGS__))
#define __GET_VAR_REF(name, ...)             (&((name) __VA_ARGS__))
#define __GET_VAR_DREF(name, ...)            (*((name) __VA_ARGS__))
#define __INITIAL_VALUE(...)                 __VA_ARGS__

/** @} */

/** @name The enumerated ST types the LIBRARY owns
 *
 *  A `x_qlt` row is a Ref<Quality> and a `prog_x_health` row a Ref<Health>, so
 *  the enumeration ST compares against has to be the SAME C++ type rather than
 *  one with equal values - matiec initialises a QUALITY-returning function with
 *  `QUALITY__Q_NONE`, and a separate enum would not convert.
 *
 *  So the members are named constants of the real type and matiec's own list is
 *  discarded. Both blocks are GENERATED from the headers that declare the enums,
 *  so the sides cannot drift. A site's own enumerated type gets its block from
 *  `enums.gen.h` the same way.
 *  @{ */
typedef ::Botanik::Vars::Quality QUALITY;
typedef ::Botanik::Plc::Health   HEALTH;
#define __DECLARE_ENUMERATED_TYPE(name, ...) BOTANIK_ST_TYPE_##name
/** @} */

namespace Botanik { namespace Vars {
    /** @brief A reference to a program's health row. Here rather than in
     *  model.h: the code is Plc's, and vars/ does not know about plc/. */
    template <> struct RefIo< ::Botanik::Plc::Health> {
        static ::Botanik::Plc::Health read(Var h)
            { return (::Botanik::Plc::Health)h.as<uint8_t>(); }
        static ::Botanik::Plc::Health of(Value v)
            { return (::Botanik::Plc::Health)v.as<uint8_t>(); }
        static void write(Var h, ::Botanik::Plc::Health x)
            { h.set_i((int64_t)x); }
    };

    /** @brief A verdict is this scan's answer, so it never takes drive()'s
     *  parameter path - owing a health code to flash is not a thing a law can
     *  mean. The same reason Ref<Quality> has one. */
    template <> inline void Ref< ::Botanik::Plc::Health>::drive(
            ::Botanik::Plc::Health x) const {
        if (runtime_bank != nullptr)
            RefIo< ::Botanik::Plc::Health>::write(runtime_bank->var(i), x);
    }
}}  // namespace Botanik::Vars

#define __BOOL_LITERAL(v)   ((BOOL)(v))
#define __REAL_LITERAL(v)   ((REAL)(v))
#define __LREAL_LITERAL(v)  ((LREAL)(v))
#define __SINT_LITERAL(v)   ((SINT)(v))
#define __INT_LITERAL(v)    ((INT)(v))
#define __DINT_LITERAL(v)   ((DINT)(v))
#define __USINT_LITERAL(v)  ((USINT)(v))
#define __UINT_LITERAL(v)   ((UINT)(v))
#define __UDINT_LITERAL(v)  ((UDINT)(v))
#define __TIME_LITERAL(v)   ((TIME)(v))


/** @name VAR_EXTERNAL - the path that generates bind()
 *
 *  A POU declaring `VAR_EXTERNAL` reads and writes the global DIRECTLY:
 *  matiec emits no copy in or out, and the resource body is one call. The
 *  formal-parameter connection list does not work here - it emits a raw `=`
 *  between a value and matiec's own `{value, flags}` struct, which does not
 *  compile against matiec's own runtime either.
 *
 *  `__INIT_EXTERNAL` is the whole of `bind()`: one generated INDEX per row.
 *  IEC is case-insensitive and matiec upper-cases every identifier, so the
 *  index is `ROW_<NAME>`, which plant.cpp defines from the table. A name no
 *  table declares fails to COMPILE.
 *  @{ */

#define __DECLARE_EXTERNAL(type, name) \
    ::Botanik::Vars::Ref< type > name;

/* The row already exists in the site's table, so a VAR_GLOBAL declares
 * nothing at all. */
#define __DECLARE_GLOBAL(type, domain, name)
#define __INIT_GLOBAL(type, name, initial, retained)  do { } while (0);
#define __DECLARE_GLOBAL_LOCATION(type, location)
/* The config header restates every global as a prototype. The row already
 * exists in the site table, so there is nothing to prototype. */
#define __DECLARE_GLOBAL_PROTOTYPE(type, name)
#define __DECLARE_GLOBAL_PROTOTYPE_FB(type, name)
#define __INIT_RETAIN(name, retained)

#define __INIT_EXTERNAL(type, global, name, retained) \
    (name) = ::Botanik::Vars::Ref< type >{ ROW_##global };

/* A Ref reads the runtime's one bank, so nothing here is told which. Reads and
 * writes go through the reference, exactly as a C++ law writes them - which is
 * the point: the generated body is our own spelling. */
#define __GET_EXTERNAL(name, ...)   (name).get()
/* drive(), not set(): a PARAMETER an ST program writes is state the plant has
 * to keep - a watering date, a spent one-shot force - and set() would leave the
 * store owing it. There is no second macro to choose with, so the ROW decides.
 * @see Vars::Ref::drive() */
#define __SET_EXTERNAL(prefix, name, suffix, new_value) \
    (prefix name).drive((new_value))

/** @} */

#endif
