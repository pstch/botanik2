#ifndef __POUS_H
#define __POUS_H

#include "accessor.h"
#include "iec_std_lib.h"

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
  __DECLARE_VAR(USINT,BAND)

} STAIRCASE3_data__;

// PROGRAM DESTRAT
// Data part
typedef struct {
  // PROGRAM Interface - IN, OUT, IN_OUT variables
  __DECLARE_VAR(REAL,STRAT)
  __DECLARE_VAR(REAL,LOW)
  __DECLARE_VAR(REAL,MED)
  __DECLARE_VAR(REAL,HIGH)
  __DECLARE_VAR(REAL,HYST)
  __DECLARE_VAR(REAL,FAN_LOW)
  __DECLARE_VAR(REAL,FAN_MED)
  __DECLARE_VAR(REAL,FAN_HIGH)
  __DECLARE_VAR(REAL,DEMAND)

  // PROGRAM private variables - TEMP, private and located variables
  STAIRCASE3_data__ BANDS;

} DESTRAT_data__;

#endif //__POUS_H
