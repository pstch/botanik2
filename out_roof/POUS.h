#ifndef __POUS_H
#define __POUS_H

#include "accessor.h"
#include "iec_std_lib.h"

// FUNCTION_BLOCK TOF_MS
// Data part
typedef struct {
  // FB Interface - IN, OUT, IN_OUT variables
  __DECLARE_VAR(BOOL,EN)
  __DECLARE_VAR(BOOL,ENO)
  __DECLARE_VAR(BOOL,IN)
  __DECLARE_VAR(UDINT,PT)
  __DECLARE_VAR(UDINT,DT_MS)
  __DECLARE_VAR(BOOL,Q)
  __DECLARE_VAR(UDINT,ET)

  // FB private variables - TEMP, private and located variables

} TOF_MS_data__;

// FUNCTION_BLOCK EWMA
// Data part
typedef struct {
  // FB Interface - IN, OUT, IN_OUT variables
  __DECLARE_VAR(BOOL,EN)
  __DECLARE_VAR(BOOL,ENO)
  __DECLARE_VAR(REAL,X)
  __DECLARE_VAR(REAL,TAU_S)
  __DECLARE_VAR(UDINT,DT_MS)
  __DECLARE_VAR(REAL,Y)

  // FB private variables - TEMP, private and located variables
  __DECLARE_VAR(BOOL,SEEDED)
  __DECLARE_VAR(REAL,DT_S)
  __DECLARE_VAR(REAL,ALPHA)

} EWMA_data__;

// FUNCTION_BLOCK STAIRCASE3
// Data part
typedef struct {
  // FB Interface - IN, OUT, IN_OUT variables
  __DECLARE_VAR(BOOL,EN)
  __DECLARE_VAR(BOOL,ENO)
  __DECLARE_VAR(REAL,IN)
  __DECLARE_VAR(REAL,LO)
  __DECLARE_VAR(REAL,MED)
  __DECLARE_VAR(REAL,HI)
  __DECLARE_VAR(REAL,HYST)
  __DECLARE_VAR(USINT,Q)

  // FB private variables - TEMP, private and located variables

} STAIRCASE3_data__;

// PROGRAM ROOF_LOCKOUT
// Data part
typedef struct {
  // PROGRAM Interface - IN, OUT, IN_OUT variables
  __DECLARE_VAR(REAL,RAIN_MMPH)
  __DECLARE_VAR(BOOL,RAIN_MMPH_QLT)
  __DECLARE_VAR(REAL,WIND_MPS)
  __DECLARE_VAR(BOOL,WIND_MPS_QLT)
  __DECLARE_VAR(REAL,GUST_MPS)
  __DECLARE_VAR(REAL,SHUT_BEGIN)
  __DECLARE_VAR(REAL,SHUT_END)
  __DECLARE_VAR(REAL,SHUT_MAX)
  __DECLARE_VAR(UINT,RAIN_HOLD_S)
  __DECLARE_VAR(BOOL,SUPPLY_LOCK)
  __DECLARE_VAR(UDINT,DT_MS)
  __DECLARE_VAR(BOOL,RAIN_LOCK)
  __DECLARE_VAR(BOOL,WIND_LOCK)
  __DECLARE_VAR(BOOL,WINDOW_LOCK)
  __DECLARE_VAR(REAL,CAP)

  // PROGRAM private variables - TEMP, private and located variables
  TOF_MS_data__ RAIN_FOR;
  __DECLARE_VAR(BOOL,BLIND)
  __DECLARE_VAR(REAL,W)
  __DECLARE_VAR(REAL,CEILING)

} ROOF_LOCKOUT_data__;

#endif //__POUS_H
