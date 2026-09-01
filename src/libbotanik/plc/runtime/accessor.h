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
    ::Botanik::Vars::Ref< type, ::Botanik::Vars::Dir::INOUT > name;

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
/* held(), not get(): the row's OWN value, with no declared substitute.
 *
 * A law asks the COMPANION whether to believe a reading; substitution answers
 * the same question a second time and answers it SILENTLY, by handing back a
 * plausible number. That is how a rain gauge that stopped answering reads as
 * fine weather - roof_lockout's own comment says so - and every _qlt gate in
 * the site is the workaround. One mechanism, and it is the explicit one.
 *
 * It also fixes the shape substitution can never serve: a running counter,
 * where a law subtracts two readings of the same absolute and a constant put
 * in place of one makes the difference arithmetic about nothing. */
#define __GET_EXTERNAL(name, ...)   (name).held()
/* drive(), not set(): a PARAMETER an ST program writes is state the plant has
 * to keep - a watering date, a spent one-shot force - and set() would leave the
 * store owing it. There is no second macro to choose with, so the ROW decides.
 * @see Vars::Ref::drive() */
#define __SET_EXTERNAL(prefix, name, suffix, new_value) \
    (prefix name).drive((new_value))

/** @} */

#endif
