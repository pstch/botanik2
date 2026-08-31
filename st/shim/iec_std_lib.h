#ifndef BOTANIK_IEC_STD_LIB_H
#define BOTANIK_IEC_STD_LIB_H
/** @file  A stand-in for matiec's LGPL runtime header.
 *
 *  matiec's generated C names IEC types directly (`USINT __case_expression`),
 *  so those names have to exist. Nothing else of its runtime is wanted: the
 *  storage, the forcing and the quality are the bank's.
 */
#include <cstdint>
#include <cstdarg>
#include <cmath>

typedef bool     BOOL;
typedef int8_t   SINT;
typedef int16_t  INT;
typedef int32_t  DINT;
typedef int64_t  LINT;
typedef uint8_t  USINT;
typedef uint16_t UINT;
typedef uint32_t UDINT;
typedef uint64_t ULINT;
typedef float    REAL;
typedef double   LREAL;
typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t LWORD;
typedef int64_t  TIME;

/* The standard FUNCTIONS are not here any more: they are ST, in st/lib/
 * ieclib.txt, so matiec compiles them like anything else we write. What is
 * left in this file is the TYPES matiec's generated C names. */

/** @brief matiec's IL "default variable".
 *
 *  Declared in any POU carrying a verbatim block, which our four type
 *  conversions do - ST has no way to change a value's type without one. We
 *  write no IL, and the generated code casts it straight to void, so the type
 *  only has to exist. */
typedef int __IL_DEFVAR_T;

#define TRUE  true
#define FALSE false

#endif
