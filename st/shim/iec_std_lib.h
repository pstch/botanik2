#ifndef BOTANIK_IEC_STD_LIB_H
#define BOTANIK_IEC_STD_LIB_H
/** @file  A stand-in for matiec's LGPL runtime header.
 *
 *  matiec's generated C names IEC types directly (`USINT __case_expression`),
 *  so those names have to exist. Nothing else of its runtime is wanted: the
 *  storage, the forcing and the quality are the bank's.
 */
#include <cstdint>

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

#define TRUE  true
#define FALSE false

#endif
