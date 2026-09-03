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
#include "libbotanik/vars/model.h"
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

/** @name The ST type QUALITY, which IS Vars::Quality
 *
 *  A `x_qlt` row resolves to Ref<Quality>, so the enumeration ST compares
 *  against has to be the SAME C++ type rather than one with equal values -
 *  matiec initialises a QUALITY-returning function with `QUALITY__Q_NO_DATA`,
 *  and a separate enum would not convert.
 *
 *  So the members are named constants of the real type and matiec's own list
 *  is discarded. A code added to `plc/functions/quality.st` and not to
 *  descriptor.h fails HERE, on the name matiec emits for it, which is the
 *  failure wanted. A second enumerated ST type needs its own block.
 *  @{ */
typedef ::Botanik::Vars::Quality QUALITY;
#define __DECLARE_ENUMERATED_TYPE(name, ...) BOTANIK_ST_TYPE_##name
#define BOTANIK_ST_TYPE_QUALITY                                                \
    static const QUALITY QUALITY__Q_NO_DATA     = ::Botanik::Vars::Q_NO_DATA;  \
    static const QUALITY QUALITY__Q_VALID       = ::Botanik::Vars::Q_VALID;    \
    static const QUALITY QUALITY__Q_STALE       = ::Botanik::Vars::Q_STALE;    \
    static const QUALITY QUALITY__Q_IMPLAUSIBLE = ::Botanik::Vars::Q_IMPLAUSIBLE; \
    static const QUALITY QUALITY__Q_REFUSED     = ::Botanik::Vars::Q_REFUSED;
/** @} */

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
 *  `__INIT_EXTERNAL` is the whole of `bind()`: one resolve per row, at setup.
 *  IEC is case-insensitive and matiec upper-cases every identifier, so the
 *  resolve folds case. All 425 site rows stay unique under folding.
 *  @{ */

#define __DECLARE_EXTERNAL(type, name) \
    ::Botanik::Vars::Ref< type > name;

/* The row already exists in the site's table, so a VAR_GLOBAL declares
 * nothing. It only has to name itself for __INIT_EXTERNAL to find. */
#define __DECLARE_GLOBAL(type, domain, name) \
    static const char* const GLOBAL_NAME__##name = #name;
#define __INIT_GLOBAL(type, name, initial, retained)  do { } while (0);
#define __DECLARE_GLOBAL_LOCATION(type, location)
/* The config header restates every global as a prototype. The row already
 * exists in the site table, so there is nothing to prototype. */
#define __DECLARE_GLOBAL_PROTOTYPE(type, name)
#define __DECLARE_GLOBAL_PROTOTYPE_FB(type, name)
#define __INIT_RETAIN(name, retained)

#define __INIT_EXTERNAL(type, global, name, retained) \
    (name) = ::Botanik::Plc::Runtime::resolve_ci< type >(BOTANIK_ST_IO, #global);

/* A Ref carries its store, so only the RESOLVE needs to be told which bank.
 * Reads and writes go through the reference, exactly as a C++ law writes
 * them - which is the point: the generated body is our own spelling. */
#define __GET_EXTERNAL(name, ...)   (name).get()
/* drive(), not set(): a PARAMETER an ST program writes is state the plant has
 * to keep - a watering date, a spent one-shot force - and set() would leave the
 * store owing it. There is no second macro to choose with, so the ROW decides.
 * @see Vars::Ref::drive() */
#define __SET_EXTERNAL(prefix, name, suffix, new_value) \
    (prefix name).drive((new_value))

/** @} */

#endif
