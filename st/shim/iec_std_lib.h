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

/* The standard library, as much of it as the ported POUs reach. Every entry
 * takes matiec's implicit (EN, ENO) pair ahead of its own arguments. */
#define IEC_CONV(F, T) \
    static inline T ___##F##_TO_##T(BOOL en, BOOL* eno, F x) { \
        if (eno) *eno = en; return (T)x; }
IEC_CONV(UDINT, REAL)
IEC_CONV(UINT,  UDINT)
IEC_CONV(USINT, UDINT)
IEC_CONV(REAL,  UDINT)
IEC_CONV(REAL,  INT)
IEC_CONV(INT,   REAL)
#undef IEC_CONV

/* MAX and MIN are VARIADIC in IEC: matiec passes the argument count ahead of
 * the values, and a REAL promotes to double through the ellipsis. */
static inline REAL ___MAX__REAL__REAL(BOOL en, BOOL* eno, UINT n, ...) {
    va_list ap; va_start(ap, n);
    REAL best = (REAL)va_arg(ap, double);
    for (UINT i = 1; i < n; i++) {
        const REAL v = (REAL)va_arg(ap, double);
        if (v > best) best = v;
    }
    va_end(ap);
    if (eno) *eno = en;
    return best;
}
static inline REAL ___MIN__REAL__REAL(BOOL en, BOOL* eno, UINT n, ...) {
    va_list ap; va_start(ap, n);
    REAL best = (REAL)va_arg(ap, double);
    for (UINT i = 1; i < n; i++) {
        const REAL v = (REAL)va_arg(ap, double);
        if (v < best) best = v;
    }
    va_end(ap);
    if (eno) *eno = en;
    return best;
}

static inline REAL ___LIMIT__REAL__REAL__REAL__REAL(BOOL en, BOOL* eno,
                                                    REAL mn, REAL in, REAL mx) {
    if (eno) *eno = en;
    return in < mn ? mn : (in > mx ? mx : in);
}

#define TRUE  true
#define FALSE false

#endif
