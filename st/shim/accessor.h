#ifndef BOTANIK_IEC_ACCESSOR_H
#define BOTANIK_IEC_ACCESSOR_H
/** @file  matiec's accessor macros, redefined onto Vars::Bank.
 *
 *  matiec's generated C reaches every variable through these seven macros and
 *  never touches storage directly, so replacing the header is what makes its
 *  output address OUR rows instead of its own `{value, flags}` structs.
 *
 *  A POU's `_data__` struct becomes a struct of typed references. `bind()` is
 *  what fills them, exactly as a hand-written program's does.
 */
#include "libbotanik/vars/model.h"
#include "st/shim/iec_std_lib.h"

/* Direction is not in the macro - matiec separates IN/OUT in the STRUCT
 * COMMENT, not the declaration - so every reference is declared VAR_IN_OUT.
 * A POU may read its own output, and the writer check still catches two
 * programs driving one row. */
#define __DECLARE_VAR(type, name) \
    ::Botanik::Vars::Ref< type, ::Botanik::Vars::Dir::INOUT > name;

/* Initialisation is bind()'s, not the body's: a row's value comes from the
 * table's declared default and its quality from whoever last wrote it. */
#define __INIT_VAR(name, initial, retained)  do { (void)(initial); } while (0);

#define __GET_VAR(name, ...)        ((name) __VA_ARGS__).get()
#define __SET_VAR(prefix, name, suffix, new_value) \
    (prefix name suffix).set((new_value), true)

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

#endif
