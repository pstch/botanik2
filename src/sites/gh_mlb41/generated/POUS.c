static REAL ___ABS(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN);
static REAL ___LIMIT(
  BOOL EN, 
  BOOL *__ENO, 
  REAL MN, 
  REAL IN, 
  REAL MX);
static REAL ___MAX(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN1, 
  REAL IN2);
static REAL ___MIN(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN1, 
  REAL IN2);
static REAL ___LN(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN);
static REAL ___EXP(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN);
static UDINT ___REAL_TO_UDINT(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN);
static REAL ___UDINT_TO_REAL(
  BOOL EN, 
  BOOL *__ENO, 
  UDINT IN);
static UINT ___UDINT_TO_UINT(
  BOOL EN, 
  BOOL *__ENO, 
  UDINT IN);
static REAL ___UINT_TO_REAL(
  BOOL EN, 
  BOOL *__ENO, 
  UINT IN);
static UDINT ___UINT_TO_UDINT(
  BOOL EN, 
  BOOL *__ENO, 
  UINT IN);
static REAL ___LADDER(
  BOOL EN, 
  BOOL *__ENO, 
  REAL V, 
  REAL LO, 
  REAL HI);
static REAL ___SIGNED_LADDER(
  BOOL EN, 
  BOOL *__ENO, 
  REAL V, 
  REAL NEG_FULL, 
  REAL NEG_START, 
  REAL POS_START, 
  REAL POS_FULL);
static REAL ___DEW_POINT(
  BOOL EN, 
  BOOL *__ENO, 
  REAL T_DEGC, 
  REAL RH);
static REAL ___DEW_POINT_MARGIN(
  BOOL EN, 
  BOOL *__ENO, 
  REAL T_DEGC, 
  REAL RH);
static REAL ___SAT_VAPOUR_PRESSURE(
  BOOL EN, 
  BOOL *__ENO, 
  REAL T_DEGC);
static void BAND_LATCH_init__(BAND_LATCH_data__ *data__, BOOL retain);
static void BAND_LATCH_body__(BAND_LATCH_data__ *data__);
static void STAIRCASE3_init__(STAIRCASE3_data__ *data__, BOOL retain);
static void STAIRCASE3_body__(STAIRCASE3_data__ *data__);
static void DEADBAND_init__(DEADBAND_data__ *data__, BOOL retain);
static void DEADBAND_body__(DEADBAND_data__ *data__);
static void TOF_MS_init__(TOF_MS_data__ *data__, BOOL retain);
static void TOF_MS_body__(TOF_MS_data__ *data__);
static void TON_MS_init__(TON_MS_data__ *data__, BOOL retain);
static void TON_MS_body__(TON_MS_data__ *data__);
static void EWMA_init__(EWMA_data__ *data__, BOOL retain);
static void EWMA_body__(EWMA_data__ *data__);
static void PID_init__(PID_data__ *data__, BOOL retain);
static void PID_body__(PID_data__ *data__);
static void AC_MIRROR_init__(AC_MIRROR_data__ *data__, BOOL retain);
static void AC_MIRROR_body__(AC_MIRROR_data__ *data__);
static void AC_SUPPLY_init__(AC_SUPPLY_data__ *data__, BOOL retain);
static void AC_SUPPLY_body__(AC_SUPPLY_data__ *data__);
static void VALVE_PULSE_init__(VALVE_PULSE_data__ *data__, BOOL retain);
static void VALVE_PULSE_body__(VALVE_PULSE_data__ *data__);
static void WX_WORSE_init__(WX_WORSE_data__ *data__, BOOL retain);
static void WX_WORSE_body__(WX_WORSE_data__ *data__);
static void WX_MEAN_init__(WX_MEAN_data__ *data__, BOOL retain);
static void WX_MEAN_body__(WX_MEAN_data__ *data__);
static void WX_PUBLISH_init__(WX_PUBLISH_data__ *data__, BOOL retain);
static void WX_PUBLISH_body__(WX_PUBLISH_data__ *data__);
static void ZONE_VOTE_init__(ZONE_VOTE_data__ *data__, BOOL retain);
static void ZONE_VOTE_body__(ZONE_VOTE_data__ *data__);
static void ZONE_PUBLISH_init__(ZONE_PUBLISH_data__ *data__, BOOL retain);
static void ZONE_PUBLISH_body__(ZONE_PUBLISH_data__ *data__);
static void ZONE_PROBES_init__(ZONE_PROBES_data__ *data__, BOOL retain);
static void ZONE_PROBES_body__(ZONE_PROBES_data__ *data__);
static void FAN_CONTROLLER_init__(FAN_CONTROLLER_data__ *data__, BOOL retain);
static void FAN_CONTROLLER_body__(FAN_CONTROLLER_data__ *data__);
static void INTRACTOR_CONTROLLER_init__(INTRACTOR_CONTROLLER_data__ *data__, BOOL retain);
static void INTRACTOR_CONTROLLER_body__(INTRACTOR_CONTROLLER_data__ *data__);
static void ROOF_LOCKOUT_init__(ROOF_LOCKOUT_data__ *data__, BOOL retain);
static void ROOF_LOCKOUT_body__(ROOF_LOCKOUT_data__ *data__);
static void SKIRT_CONTROLLER_init__(SKIRT_CONTROLLER_data__ *data__, BOOL retain);
static void SKIRT_CONTROLLER_body__(SKIRT_CONTROLLER_data__ *data__);
static void COOLING_CONTROLLER_init__(COOLING_CONTROLLER_data__ *data__, BOOL retain);
static void COOLING_CONTROLLER_body__(COOLING_CONTROLLER_data__ *data__);
static void DAY_PHASE_CONTROLLER_init__(DAY_PHASE_CONTROLLER_data__ *data__, BOOL retain);
static void DAY_PHASE_CONTROLLER_body__(DAY_PHASE_CONTROLLER_data__ *data__);
static void DESTRAT_CONTROLLER_init__(DESTRAT_CONTROLLER_data__ *data__, BOOL retain);
static void DESTRAT_CONTROLLER_body__(DESTRAT_CONTROLLER_data__ *data__);
static void HUMIDITY_CONTROLLER_init__(HUMIDITY_CONTROLLER_data__ *data__, BOOL retain);
static void HUMIDITY_CONTROLLER_body__(HUMIDITY_CONTROLLER_data__ *data__);
static void INTAKE_DRY_GATE_init__(INTAKE_DRY_GATE_data__ *data__, BOOL retain);
static void INTAKE_DRY_GATE_body__(INTAKE_DRY_GATE_data__ *data__);
static void INTAKE_GATES_init__(INTAKE_GATES_data__ *data__, BOOL retain);
static void INTAKE_GATES_body__(INTAKE_GATES_data__ *data__);
static void TARGET_TEMP_CONTROLLER_init__(TARGET_TEMP_CONTROLLER_data__ *data__, BOOL retain);
static void TARGET_TEMP_CONTROLLER_body__(TARGET_TEMP_CONTROLLER_data__ *data__);
static void AC_GRID_WATCH_init__(AC_GRID_WATCH_data__ *data__, BOOL retain);
static void AC_GRID_WATCH_body__(AC_GRID_WATCH_data__ *data__);
static void AC_LOCAL_WATCH_init__(AC_LOCAL_WATCH_data__ *data__, BOOL retain);
static void AC_LOCAL_WATCH_body__(AC_LOCAL_WATCH_data__ *data__);
static void CLIMATE_WATCH_init__(CLIMATE_WATCH_data__ *data__, BOOL retain);
static void CLIMATE_WATCH_body__(CLIMATE_WATCH_data__ *data__);
static void STRAT_SENSOR_init__(STRAT_SENSOR_data__ *data__, BOOL retain);
static void STRAT_SENSOR_body__(STRAT_SENSOR_data__ *data__);
static void TEMP_RH_SENSORS_init__(TEMP_RH_SENSORS_data__ *data__, BOOL retain);
static void TEMP_RH_SENSORS_body__(TEMP_RH_SENSORS_data__ *data__);
static void TEMP_RH_ZONE_init__(TEMP_RH_ZONE_data__ *data__, BOOL retain);
static void TEMP_RH_ZONE_body__(TEMP_RH_ZONE_data__ *data__);
static void WEATHER_WATCH_init__(WEATHER_WATCH_data__ *data__, BOOL retain);
static void WEATHER_WATCH_body__(WEATHER_WATCH_data__ *data__);
static void WX_SENSORS_init__(WX_SENSORS_data__ *data__, BOOL retain);
static void WX_SENSORS_body__(WX_SENSORS_data__ *data__);

static REAL ___ABS(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL ABS = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return ABS;
  }
  if ((IN < __REAL_LITERAL(0.0))) {
    ABS = (__REAL_LITERAL(0.0) - IN);
  } else {
    ABS = IN;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return ABS;
}


static REAL ___LIMIT(
  BOOL EN, 
  BOOL *__ENO, 
  REAL MN, 
  REAL IN, 
  REAL MX)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL LIMIT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return LIMIT;
  }
  if ((IN < MN)) {
    LIMIT = MN;
  } else if ((IN > MX)) {
    LIMIT = MX;
  } else {
    LIMIT = IN;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return LIMIT;
}


static REAL ___MAX(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN1, 
  REAL IN2)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL MAX = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return MAX;
  }
  if ((IN1 > IN2)) {
    MAX = IN1;
  } else {
    MAX = IN2;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return MAX;
}


static REAL ___MIN(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN1, 
  REAL IN2)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL MIN = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return MIN;
  }
  if ((IN1 < IN2)) {
    MIN = IN1;
  } else {
    MIN = IN2;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return MIN;
}


static REAL ___LN(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL LN = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return LN;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 LN = logf(IN); 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return LN;
}


static REAL ___EXP(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL EXP = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return EXP;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 EXP = expf(IN); 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return EXP;
}


static UDINT ___REAL_TO_UDINT(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  UDINT REAL_TO_UDINT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return REAL_TO_UDINT;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 REAL_TO_UDINT = (UDINT)(IN < 0 ? 0 : IN); 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return REAL_TO_UDINT;
}


static REAL ___UDINT_TO_REAL(
  BOOL EN, 
  BOOL *__ENO, 
  UDINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL UDINT_TO_REAL = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return UDINT_TO_REAL;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 UDINT_TO_REAL = (REAL)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return UDINT_TO_REAL;
}


static UINT ___UDINT_TO_UINT(
  BOOL EN, 
  BOOL *__ENO, 
  UDINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  UINT UDINT_TO_UINT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return UDINT_TO_UINT;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 UDINT_TO_UINT = (UINT)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return UDINT_TO_UINT;
}


static REAL ___UINT_TO_REAL(
  BOOL EN, 
  BOOL *__ENO, 
  UINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL UINT_TO_REAL = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return UINT_TO_REAL;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 UINT_TO_REAL = (REAL)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return UINT_TO_REAL;
}


static UDINT ___UINT_TO_UDINT(
  BOOL EN, 
  BOOL *__ENO, 
  UINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  UDINT UINT_TO_UDINT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return UINT_TO_UDINT;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 UINT_TO_UDINT = (UDINT)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return UINT_TO_UDINT;
}


static REAL ___LADDER(
  BOOL EN, 
  BOOL *__ENO, 
  REAL V, 
  REAL LO, 
  REAL HI)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL LADDER = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return LADDER;
  }
  if ((HI <= LO)) {
    if ((V >= HI)) {
      LADDER = __REAL_LITERAL(1.0);
    } else {
      LADDER = __REAL_LITERAL(0.0);
    };
  } else {
    LADDER = ___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.0),
      (REAL)((V - LO) / (HI - LO)),
      (REAL)__REAL_LITERAL(1.0));
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return LADDER;
}


static REAL ___SIGNED_LADDER(
  BOOL EN, 
  BOOL *__ENO, 
  REAL V, 
  REAL NEG_FULL, 
  REAL NEG_START, 
  REAL POS_START, 
  REAL POS_FULL)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL SIGNED_LADDER = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return SIGNED_LADDER;
  }
  if ((V >= POS_START)) {
    SIGNED_LADDER = ___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)V,
      (REAL)POS_START,
      (REAL)POS_FULL);
  } else if ((V <= NEG_START)) {
    SIGNED_LADDER = (__REAL_LITERAL(0.0) - ___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)(__REAL_LITERAL(0.0) - V),
      (REAL)(__REAL_LITERAL(0.0) - NEG_START),
      (REAL)(__REAL_LITERAL(0.0) - NEG_FULL)));
  } else {
    SIGNED_LADDER = __REAL_LITERAL(0.0);
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return SIGNED_LADDER;
}


static REAL ___DEW_POINT(
  BOOL EN, 
  BOOL *__ENO, 
  REAL T_DEGC, 
  REAL RH)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL GAMMA = 0;
  REAL DEW_POINT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return DEW_POINT;
  }
  GAMMA = (___LN(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.05),
      (REAL)RH,
      (REAL)__REAL_LITERAL(1.0))) + ((__REAL_LITERAL(17.62) * T_DEGC) / (__REAL_LITERAL(243.12) + T_DEGC)));
  DEW_POINT = ((__REAL_LITERAL(243.12) * GAMMA) / (__REAL_LITERAL(17.62) - GAMMA));

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return DEW_POINT;
}


static REAL ___DEW_POINT_MARGIN(
  BOOL EN, 
  BOOL *__ENO, 
  REAL T_DEGC, 
  REAL RH)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL DEW_POINT_MARGIN = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return DEW_POINT_MARGIN;
  }
  DEW_POINT_MARGIN = (T_DEGC - ___DEW_POINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)T_DEGC,
    (REAL)RH));

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return DEW_POINT_MARGIN;
}


static REAL ___SAT_VAPOUR_PRESSURE(
  BOOL EN, 
  BOOL *__ENO, 
  REAL T_DEGC)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL SAT_VAPOUR_PRESSURE = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return SAT_VAPOUR_PRESSURE;
  }
  SAT_VAPOUR_PRESSURE = (__REAL_LITERAL(0.6108) * ___EXP(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)((__REAL_LITERAL(17.27) * T_DEGC) / (T_DEGC + __REAL_LITERAL(237.3)))));

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return SAT_VAPOUR_PRESSURE;
}


static void BAND_LATCH_init__(BAND_LATCH_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->IN,0,retain)
  __INIT_VAR(data__->RISE,0,retain)
  __INIT_VAR(data__->FALL,0,retain)
  __INIT_VAR(data__->Q,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void BAND_LATCH_body__(BAND_LATCH_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables

  if ((__GET_VAR(data__->IN,) >= __GET_VAR(data__->RISE,))) {
    __SET_VAR(data__->,Q,,__BOOL_LITERAL(TRUE));
  } else if ((__GET_VAR(data__->IN,) <= __GET_VAR(data__->FALL,))) {
    __SET_VAR(data__->,Q,,__BOOL_LITERAL(FALSE));
  };

  goto __end;

__end:
  return;
} // BAND_LATCH_body__() 





static void STAIRCASE3_init__(STAIRCASE3_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->IN,0,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->MED,0,retain)
  __INIT_VAR(data__->HI,0,retain)
  __INIT_VAR(data__->HYST,0,retain)
  __INIT_VAR(data__->Q,0,retain)
  BAND_LATCH_init__(&data__->RUNG1,retain);
  BAND_LATCH_init__(&data__->RUNG2,retain);
  BAND_LATCH_init__(&data__->RUNG3,retain);
}

// Code part
static void STAIRCASE3_body__(STAIRCASE3_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,H,,0);

  if ((__GET_VAR(data__->HYST,) < __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,H,,__REAL_LITERAL(0.0));
  } else {
    __SET_VAR(data__->,H,,__GET_VAR(data__->HYST,));
  };
  __SET_VAR(data__->RUNG1.,IN,,__GET_VAR(data__->IN,));
  __SET_VAR(data__->RUNG1.,RISE,,__GET_VAR(data__->LO,));
  __SET_VAR(data__->RUNG1.,FALL,,(__GET_VAR(data__->LO,) - __GET_VAR(data__->H,)));
  BAND_LATCH_body__(&data__->RUNG1);
  __SET_VAR(data__->RUNG2.,IN,,__GET_VAR(data__->IN,));
  __SET_VAR(data__->RUNG2.,RISE,,__GET_VAR(data__->MED,));
  __SET_VAR(data__->RUNG2.,FALL,,(__GET_VAR(data__->MED,) - __GET_VAR(data__->H,)));
  BAND_LATCH_body__(&data__->RUNG2);
  __SET_VAR(data__->RUNG3.,IN,,__GET_VAR(data__->IN,));
  __SET_VAR(data__->RUNG3.,RISE,,__GET_VAR(data__->HI,));
  __SET_VAR(data__->RUNG3.,FALL,,(__GET_VAR(data__->HI,) - __GET_VAR(data__->H,)));
  BAND_LATCH_body__(&data__->RUNG3);
  __SET_VAR(data__->,Q,,__USINT_LITERAL(0));
  if (__GET_VAR(data__->RUNG1.Q,)) {
    __SET_VAR(data__->,Q,,__USINT_LITERAL(1));
  };
  if (__GET_VAR(data__->RUNG2.Q,)) {
    __SET_VAR(data__->,Q,,__USINT_LITERAL(2));
  };
  if (__GET_VAR(data__->RUNG3.Q,)) {
    __SET_VAR(data__->,Q,,__USINT_LITERAL(3));
  };

  goto __end;

__end:
  return;
} // STAIRCASE3_body__() 





static void DEADBAND_init__(DEADBAND_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->V,0,retain)
  __INIT_VAR(data__->BAND,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->HELD,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void DEADBAND_body__(DEADBAND_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,DIFF,,0);

  __SET_VAR(data__->,DIFF,,(__GET_VAR(data__->V,) - __GET_VAR(data__->HELD,)));
  if (((!(__GET_VAR(data__->SEEDED,)) || (__GET_VAR(data__->DIFF,) > __GET_VAR(data__->BAND,))) || (__GET_VAR(data__->DIFF,) < (__REAL_LITERAL(0.0) - __GET_VAR(data__->BAND,))))) {
    __SET_VAR(data__->,HELD,,__GET_VAR(data__->V,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,Y,,__GET_VAR(data__->HELD,));

  goto __end;

__end:
  return;
} // DEADBAND_body__() 





static void TOF_MS_init__(TOF_MS_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->IN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->PT,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Q,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->ET,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void TOF_MS_body__(TOF_MS_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,ELAPSED,,0);

  if (__GET_VAR(data__->SEEDED,)) {
    __SET_VAR(data__->,ELAPSED,,(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,)));
  } else {
    __SET_VAR(data__->,ELAPSED,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if (__GET_VAR(data__->IN,)) {
    __SET_VAR(data__->,ET,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,Q,,__BOOL_LITERAL(TRUE));
  } else if (__GET_VAR(data__->Q,)) {
    if ((__GET_VAR(data__->ET,) > (__UDINT_LITERAL(4000000000) - __GET_VAR(data__->ELAPSED,)))) {
      __SET_VAR(data__->,ET,,__UDINT_LITERAL(4000000000));
    } else {
      __SET_VAR(data__->,ET,,(__GET_VAR(data__->ET,) + __GET_VAR(data__->ELAPSED,)));
    };
    if ((__GET_VAR(data__->ET,) >= __GET_VAR(data__->PT,))) {
      __SET_VAR(data__->,Q,,__BOOL_LITERAL(FALSE));
    };
  };

  goto __end;

__end:
  return;
} // TOF_MS_body__() 





static void TON_MS_init__(TON_MS_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->IN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->PT,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Q,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->ET,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void TON_MS_body__(TON_MS_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,ELAPSED,,0);

  if (__GET_VAR(data__->SEEDED,)) {
    __SET_VAR(data__->,ELAPSED,,(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,)));
  } else {
    __SET_VAR(data__->,ELAPSED,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if (!(__GET_VAR(data__->IN,))) {
    __SET_VAR(data__->,ET,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,Q,,__BOOL_LITERAL(FALSE));
  } else {
    if ((__GET_VAR(data__->ET,) > (__UDINT_LITERAL(4000000000) - __GET_VAR(data__->ELAPSED,)))) {
      __SET_VAR(data__->,ET,,__UDINT_LITERAL(4000000000));
    } else {
      __SET_VAR(data__->,ET,,(__GET_VAR(data__->ET,) + __GET_VAR(data__->ELAPSED,)));
    };
    __SET_VAR(data__->,Q,,(__GET_VAR(data__->ET,) >= __GET_VAR(data__->PT,)));
  };

  goto __end;

__end:
  return;
} // TON_MS_body__() 





static void EWMA_init__(EWMA_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->X,0,retain)
  __INIT_VAR(data__->TAU_S,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void EWMA_body__(EWMA_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,DT_S,,0);
  __SET_VAR(data__->,ALPHA,,0);

  if (!(__GET_VAR(data__->SEEDED,))) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->X,));
    __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  } else if ((__GET_VAR(data__->TAU_S,) <= __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->X,));
    __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  } else {
    __SET_VAR(data__->,DT_S,,(___UDINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UDINT)(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,))) / __REAL_LITERAL(1000.0)));
    __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
    __SET_VAR(data__->,ALPHA,,(__GET_VAR(data__->DT_S,) / (__GET_VAR(data__->TAU_S,) + __GET_VAR(data__->DT_S,))));
    __SET_VAR(data__->,Y,,(__GET_VAR(data__->Y,) + (__GET_VAR(data__->ALPHA,) * (__GET_VAR(data__->X,) - __GET_VAR(data__->Y,)))));
  };

  goto __end;

__end:
  return;
} // EWMA_body__() 





static void PID_init__(PID_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->SP,0,retain)
  __INIT_VAR(data__->PV,0,retain)
  __INIT_VAR(data__->KP,0,retain)
  __INIT_VAR(data__->KI,0,retain)
  __INIT_VAR(data__->KD,0,retain)
  __INIT_VAR(data__->OUT_MIN,0,retain)
  __INIT_VAR(data__->OUT_MAX,0,retain)
  __INIT_VAR(data__->D_TAU_S,0,retain)
  __INIT_VAR(data__->REVERSE,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->RESET,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->ERR,0,retain)
  __INIT_VAR(data__->INTEGRAL,0,retain)
  __INIT_VAR(data__->SATURATED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->PREV_PV,0,retain)
  __INIT_VAR(data__->DERIV,0,retain)
}

// Code part
static void PID_body__(PID_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,DT_S,,0);
  __SET_VAR(data__->,RAW,,0);
  __SET_VAR(data__->,ALPHA,,0);
  __SET_VAR(data__->,P_TERM,,0);
  __SET_VAR(data__->,D_TERM,,0);
  __SET_VAR(data__->,UNSAT,,0);
  __SET_VAR(data__->,WINDING,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,RAW_OUT,,0);
  __SET_VAR(data__->,LO,,0);
  __SET_VAR(data__->,HI,,0);

  if (__GET_VAR(data__->RESET,)) {
    __SET_VAR(data__->,INTEGRAL,,__REAL_LITERAL(0.0));
    __SET_VAR(data__->,PREV_PV,,__REAL_LITERAL(0.0));
    __SET_VAR(data__->,DERIV,,__REAL_LITERAL(0.0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(FALSE));
  };
  if (__GET_VAR(data__->SEEDED,)) {
    __SET_VAR(data__->,DT_S,,(___UDINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UDINT)(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,))) / __REAL_LITERAL(1000.0)));
  } else {
    __SET_VAR(data__->,DT_S,,__REAL_LITERAL(0.0));
    __SET_VAR(data__->,PREV_PV,,__GET_VAR(data__->PV,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if (__GET_VAR(data__->REVERSE,)) {
    __SET_VAR(data__->,ERR,,(__GET_VAR(data__->PV,) - __GET_VAR(data__->SP,)));
  } else {
    __SET_VAR(data__->,ERR,,(__GET_VAR(data__->SP,) - __GET_VAR(data__->PV,)));
  };
  if ((__GET_VAR(data__->DT_S,) > __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,RAW,,((__GET_VAR(data__->PV,) - __GET_VAR(data__->PREV_PV,)) / __GET_VAR(data__->DT_S,)));
    if ((__GET_VAR(data__->D_TAU_S,) > __REAL_LITERAL(0.0))) {
      __SET_VAR(data__->,ALPHA,,(__GET_VAR(data__->DT_S,) / (__GET_VAR(data__->D_TAU_S,) + __GET_VAR(data__->DT_S,))));
      __SET_VAR(data__->,DERIV,,(__GET_VAR(data__->DERIV,) + (__GET_VAR(data__->ALPHA,) * (__GET_VAR(data__->RAW,) - __GET_VAR(data__->DERIV,)))));
    } else {
      __SET_VAR(data__->,DERIV,,__GET_VAR(data__->RAW,));
    };
    __SET_VAR(data__->,PREV_PV,,__GET_VAR(data__->PV,));
  };
  __SET_VAR(data__->,P_TERM,,(__GET_VAR(data__->KP,) * __GET_VAR(data__->ERR,)));
  if (__GET_VAR(data__->REVERSE,)) {
    __SET_VAR(data__->,D_TERM,,(__GET_VAR(data__->KD,) * __GET_VAR(data__->DERIV,)));
  } else {
    __SET_VAR(data__->,D_TERM,,(__REAL_LITERAL(0.0) - (__GET_VAR(data__->KD,) * __GET_VAR(data__->DERIV,))));
  };
  if ((__GET_VAR(data__->KI,) == __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,INTEGRAL,,__REAL_LITERAL(0.0));
  } else if ((__GET_VAR(data__->DT_S,) > __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,UNSAT,,((__GET_VAR(data__->P_TERM,) + __GET_VAR(data__->INTEGRAL,)) + __GET_VAR(data__->D_TERM,)));
    __SET_VAR(data__->,WINDING,,(((__GET_VAR(data__->UNSAT,) >= __GET_VAR(data__->OUT_MAX,)) && (__GET_VAR(data__->ERR,) > __REAL_LITERAL(0.0))) || ((__GET_VAR(data__->UNSAT,) <= __GET_VAR(data__->OUT_MIN,)) && (__GET_VAR(data__->ERR,) < __REAL_LITERAL(0.0)))));
    if (!(__GET_VAR(data__->WINDING,))) {
      __SET_VAR(data__->,INTEGRAL,,(__GET_VAR(data__->INTEGRAL,) + ((__GET_VAR(data__->KI,) * __GET_VAR(data__->ERR,)) * __GET_VAR(data__->DT_S,))));
      if ((__GET_VAR(data__->OUT_MIN,) < __REAL_LITERAL(0.0))) {
        __SET_VAR(data__->,LO,,__GET_VAR(data__->OUT_MIN,));
      } else {
        __SET_VAR(data__->,LO,,__REAL_LITERAL(0.0));
      };
      if ((__GET_VAR(data__->OUT_MAX,) > __REAL_LITERAL(0.0))) {
        __SET_VAR(data__->,HI,,__GET_VAR(data__->OUT_MAX,));
      } else {
        __SET_VAR(data__->,HI,,__REAL_LITERAL(0.0));
      };
      __SET_VAR(data__->,INTEGRAL,,___LIMIT(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (REAL)__GET_VAR(data__->LO,),
        (REAL)__GET_VAR(data__->INTEGRAL,),
        (REAL)__GET_VAR(data__->HI,)));
    };
  };
  __SET_VAR(data__->,RAW_OUT,,((__GET_VAR(data__->P_TERM,) + __GET_VAR(data__->INTEGRAL,)) + __GET_VAR(data__->D_TERM,)));
  __SET_VAR(data__->,SATURATED,,((__GET_VAR(data__->RAW_OUT,) > __GET_VAR(data__->OUT_MAX,)) || (__GET_VAR(data__->RAW_OUT,) < __GET_VAR(data__->OUT_MIN,))));
  __SET_VAR(data__->,Y,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->OUT_MIN,),
    (REAL)__GET_VAR(data__->RAW_OUT,),
    (REAL)__GET_VAR(data__->OUT_MAX,)));

  goto __end;

__end:
  return;
} // PID_body__() 





static void AC_MIRROR_init__(AC_MIRROR_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->V,0,retain)
  __INIT_VAR(data__->V_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->BAND,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->OK,__BOOL_LITERAL(FALSE),retain)
  DEADBAND_init__(&data__->DB,retain);
}

// Code part
static void AC_MIRROR_body__(AC_MIRROR_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables

  __SET_VAR(data__->,OK,,__GET_VAR(data__->V_OK,));
  if (__GET_VAR(data__->V_OK,)) {
    __SET_VAR(data__->DB.,V,,__GET_VAR(data__->V,));
    __SET_VAR(data__->DB.,BAND,,__GET_VAR(data__->BAND,));
    DEADBAND_body__(&data__->DB);
  };
  __SET_VAR(data__->,Y,,__GET_VAR(data__->DB.Y,));

  goto __end;

__end:
  return;
} // AC_MIRROR_body__() 





static void AC_SUPPLY_init__(AC_SUPPLY_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->V,0,retain)
  __INIT_VAR(data__->V_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->A,0,retain)
  __INIT_VAR(data__->A_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->W,0,retain)
  __INIT_VAR(data__->W_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->HZ,0,retain)
  __INIT_VAR(data__->HZ_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->LO_V,0,retain)
  __INIT_VAR(data__->HI_V,0,retain)
  __INIT_VAR(data__->LO_HZ,0,retain)
  __INIT_VAR(data__->HI_HZ,0,retain)
  __INIT_VAR(data__->OUT_V,0,retain)
  __INIT_VAR(data__->OUT_V_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->OUT_A,0,retain)
  __INIT_VAR(data__->OUT_A_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->OUT_W,0,retain)
  __INIT_VAR(data__->OUT_W_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->OUT_HZ,0,retain)
  __INIT_VAR(data__->OUT_HZ_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->FAULT,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->ANSWERED,__BOOL_LITERAL(FALSE),retain)
  AC_MIRROR_init__(&data__->MV,retain);
  AC_MIRROR_init__(&data__->MA,retain);
  AC_MIRROR_init__(&data__->MW,retain);
  AC_MIRROR_init__(&data__->MHZ,retain);
  __INIT_VAR(data__->SEEN,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void AC_SUPPLY_body__(AC_SUPPLY_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,MEASURED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,IN_SPEC,,__BOOL_LITERAL(FALSE));

  __SET_VAR(data__->,MEASURED,,(__GET_VAR(data__->V_OK,) && __GET_VAR(data__->HZ_OK,)));
  if (__GET_VAR(data__->MEASURED,)) {
    __SET_VAR(data__->,SEEN,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->MV.,V,,__GET_VAR(data__->V,));
  __SET_VAR(data__->MV.,V_OK,,__GET_VAR(data__->V_OK,));
  __SET_VAR(data__->MV.,BAND,,__REAL_LITERAL(0.5));
  AC_MIRROR_body__(&data__->MV);
  __SET_VAR(data__->MA.,V,,__GET_VAR(data__->A,));
  __SET_VAR(data__->MA.,V_OK,,__GET_VAR(data__->A_OK,));
  __SET_VAR(data__->MA.,BAND,,__REAL_LITERAL(0.1));
  AC_MIRROR_body__(&data__->MA);
  __SET_VAR(data__->MW.,V,,__GET_VAR(data__->W,));
  __SET_VAR(data__->MW.,V_OK,,__GET_VAR(data__->W_OK,));
  __SET_VAR(data__->MW.,BAND,,__REAL_LITERAL(5.0));
  AC_MIRROR_body__(&data__->MW);
  __SET_VAR(data__->MHZ.,V,,__GET_VAR(data__->HZ,));
  __SET_VAR(data__->MHZ.,V_OK,,__GET_VAR(data__->HZ_OK,));
  __SET_VAR(data__->MHZ.,BAND,,__REAL_LITERAL(0.05));
  AC_MIRROR_body__(&data__->MHZ);
  __SET_VAR(data__->,OUT_V,,__GET_VAR(data__->MV.Y,));
  __SET_VAR(data__->,OUT_V_OK,,__GET_VAR(data__->MV.OK,));
  __SET_VAR(data__->,OUT_A,,__GET_VAR(data__->MA.Y,));
  __SET_VAR(data__->,OUT_A_OK,,__GET_VAR(data__->MA.OK,));
  __SET_VAR(data__->,OUT_W,,__GET_VAR(data__->MW.Y,));
  __SET_VAR(data__->,OUT_W_OK,,__GET_VAR(data__->MW.OK,));
  __SET_VAR(data__->,OUT_HZ,,__GET_VAR(data__->MHZ.Y,));
  __SET_VAR(data__->,OUT_HZ_OK,,__GET_VAR(data__->MHZ.OK,));
  __SET_VAR(data__->,IN_SPEC,,((((__GET_VAR(data__->V,) >= __GET_VAR(data__->LO_V,)) && (__GET_VAR(data__->V,) <= __GET_VAR(data__->HI_V,))) && (__GET_VAR(data__->HZ,) >= __GET_VAR(data__->LO_HZ,))) && (__GET_VAR(data__->HZ,) <= __GET_VAR(data__->HI_HZ,))));
  __SET_VAR(data__->,OK,,(__GET_VAR(data__->MEASURED,) && __GET_VAR(data__->IN_SPEC,)));
  __SET_VAR(data__->,FAULT,,(__GET_VAR(data__->SEEN,) && (!(__GET_VAR(data__->MEASURED,)) || !(__GET_VAR(data__->IN_SPEC,)))));
  __SET_VAR(data__->,ANSWERED,,((__GET_VAR(data__->MEASURED,) && __GET_VAR(data__->A_OK,)) && __GET_VAR(data__->W_OK,)));

  goto __end;

__end:
  return;
} // AC_SUPPLY_body__() 





static void VALVE_PULSE_init__(VALVE_PULSE_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->DUTY_IN,0,retain)
  __INIT_VAR(data__->PERIOD_MS,0,retain)
  __INIT_VAR(data__->MIN_ON_MS,0,retain)
  __INIT_VAR(data__->RESET,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Q,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->SERVED,0,retain)
  __INIT_VAR(data__->PHASE_MS,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void VALVE_PULSE_body__(VALVE_PULSE_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,WANT,,0);
  __SET_VAR(data__->,PERIOD,,0);
  __SET_VAR(data__->,ON_MS,,0);
  __SET_VAR(data__->,SERVE,,0);
  __SET_VAR(data__->,ELAPSED,,0);

  if (__GET_VAR(data__->SEEDED,)) {
    __SET_VAR(data__->,ELAPSED,,(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,)));
  } else {
    __SET_VAR(data__->,ELAPSED,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if (__GET_VAR(data__->RESET,)) {
    __SET_VAR(data__->,PHASE_MS,,__UDINT_LITERAL(0));
  };
  __SET_VAR(data__->,WANT,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__REAL_LITERAL(0.0),
    (REAL)__GET_VAR(data__->DUTY_IN,),
    (REAL)__REAL_LITERAL(1.0)));
  __SET_VAR(data__->,PERIOD,,__GET_VAR(data__->PERIOD_MS,));
  if ((__GET_VAR(data__->PERIOD,) == __UDINT_LITERAL(0))) {
    __SET_VAR(data__->,PERIOD,,__UDINT_LITERAL(1));
  };
  __SET_VAR(data__->,ON_MS,,___REAL_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)(__GET_VAR(data__->WANT,) * ___UDINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UDINT)__GET_VAR(data__->PERIOD,)))));
  if ((__GET_VAR(data__->ON_MS,) < __GET_VAR(data__->MIN_ON_MS,))) {
    __SET_VAR(data__->,SERVE,,__UDINT_LITERAL(0));
  } else {
    __SET_VAR(data__->,SERVE,,__GET_VAR(data__->ON_MS,));
  };
  __SET_VAR(data__->,PHASE_MS,,((__GET_VAR(data__->PERIOD,) == 0)?0:((__GET_VAR(data__->PHASE_MS,) + __GET_VAR(data__->ELAPSED,)) % __GET_VAR(data__->PERIOD,))));
  __SET_VAR(data__->,Q,,(__GET_VAR(data__->PHASE_MS,) < __GET_VAR(data__->SERVE,)));
  __SET_VAR(data__->,SERVED,,(___UDINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UDINT)__GET_VAR(data__->SERVE,)) / ___UDINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UDINT)__GET_VAR(data__->PERIOD,))));

  goto __end;

__end:
  return;
} // VALVE_PULSE_body__() 





static void WX_WORSE_init__(WX_WORSE_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->A,0,retain)
  __INIT_VAR(data__->A_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->B,0,retain)
  __INIT_VAR(data__->B_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->USED,0,retain)
}

// Code part
static void WX_WORSE_body__(WX_WORSE_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables

  __SET_VAR(data__->,USED,,__USINT_LITERAL(0));
  __SET_VAR(data__->,Y,,__REAL_LITERAL(0.0));
  if (__GET_VAR(data__->A_OK,)) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->A,));
    __SET_VAR(data__->,USED,,__USINT_LITERAL(1));
  };
  if (__GET_VAR(data__->B_OK,)) {
    if (((__GET_VAR(data__->USED,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->B,) > __GET_VAR(data__->Y,)))) {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->B,));
    };
    __SET_VAR(data__->,USED,,(__GET_VAR(data__->USED,) + __USINT_LITERAL(1)));
  };

  goto __end;

__end:
  return;
} // WX_WORSE_body__() 





static void WX_MEAN_init__(WX_MEAN_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->A,0,retain)
  __INIT_VAR(data__->A_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->B,0,retain)
  __INIT_VAR(data__->B_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->USED,0,retain)
}

// Code part
static void WX_MEAN_body__(WX_MEAN_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables

  __SET_VAR(data__->,USED,,__USINT_LITERAL(0));
  __SET_VAR(data__->,Y,,__REAL_LITERAL(0.0));
  if (__GET_VAR(data__->A_OK,)) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->A,));
    __SET_VAR(data__->,USED,,__USINT_LITERAL(1));
  };
  if (__GET_VAR(data__->B_OK,)) {
    __SET_VAR(data__->,Y,,(__GET_VAR(data__->Y,) + __GET_VAR(data__->B,)));
    __SET_VAR(data__->,USED,,(__GET_VAR(data__->USED,) + __USINT_LITERAL(1)));
  };
  if ((__GET_VAR(data__->USED,) == __USINT_LITERAL(2))) {
    __SET_VAR(data__->,Y,,(__GET_VAR(data__->Y,) * __REAL_LITERAL(0.5)));
  };

  goto __end;

__end:
  return;
} // WX_MEAN_body__() 





static void WX_PUBLISH_init__(WX_PUBLISH_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->V,0,retain)
  __INIT_VAR(data__->USED,0,retain)
  __INIT_VAR(data__->TAU_S,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->HI,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->OK,__BOOL_LITERAL(FALSE),retain)
  EWMA_init__(&data__->FILT,retain);
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void WX_PUBLISH_body__(WX_PUBLISH_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables

  if ((__GET_VAR(data__->USED,) == __USINT_LITERAL(0))) {
    __SET_VAR(data__->,OK,,__BOOL_LITERAL(FALSE));
    if (__GET_VAR(data__->SEEDED,)) {
      __SET_VAR(data__->,Y,,___LIMIT(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (REAL)__GET_VAR(data__->LO,),
        (REAL)__GET_VAR(data__->FILT.Y,),
        (REAL)__GET_VAR(data__->HI,)));
    };
  } else {
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
    __SET_VAR(data__->FILT.,X,,__GET_VAR(data__->V,));
    __SET_VAR(data__->FILT.,TAU_S,,__GET_VAR(data__->TAU_S,));
    __SET_VAR(data__->FILT.,NOW,,__GET_VAR(data__->NOW,));
    EWMA_body__(&data__->FILT);
    __SET_VAR(data__->,Y,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->LO,),
      (REAL)__GET_VAR(data__->FILT.Y,),
      (REAL)__GET_VAR(data__->HI,)));
    __SET_VAR(data__->,OK,,__BOOL_LITERAL(TRUE));
  };

  goto __end;

__end:
  return;
} // WX_PUBLISH_body__() 





static void ZONE_VOTE_init__(ZONE_VOTE_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->A,0,retain)
  __INIT_VAR(data__->A_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->B,0,retain)
  __INIT_VAR(data__->B_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->C,0,retain)
  __INIT_VAR(data__->C_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->USED,0,retain)
}

// Code part
static void ZONE_VOTE_body__(ZONE_VOTE_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,V1,,0);
  __SET_VAR(data__->,V2,,0);
  __SET_VAR(data__->,V3,,0);
  __SET_VAR(data__->,N,,0);

  __SET_VAR(data__->,N,,__USINT_LITERAL(0));
  if (__GET_VAR(data__->A_OK,)) {
    __SET_VAR(data__->,N,,(__GET_VAR(data__->N,) + __USINT_LITERAL(1)));
    __SET_VAR(data__->,V1,,__GET_VAR(data__->A,));
  };
  if (__GET_VAR(data__->B_OK,)) {
    if ((__GET_VAR(data__->N,) == __USINT_LITERAL(0))) {
      __SET_VAR(data__->,V1,,__GET_VAR(data__->B,));
    } else {
      __SET_VAR(data__->,V2,,__GET_VAR(data__->B,));
    };
    __SET_VAR(data__->,N,,(__GET_VAR(data__->N,) + __USINT_LITERAL(1)));
  };
  if (__GET_VAR(data__->C_OK,)) {
    if ((__GET_VAR(data__->N,) == __USINT_LITERAL(0))) {
      __SET_VAR(data__->,V1,,__GET_VAR(data__->C,));
    } else if ((__GET_VAR(data__->N,) == __USINT_LITERAL(1))) {
      __SET_VAR(data__->,V2,,__GET_VAR(data__->C,));
    } else {
      __SET_VAR(data__->,V3,,__GET_VAR(data__->C,));
    };
    __SET_VAR(data__->,N,,(__GET_VAR(data__->N,) + __USINT_LITERAL(1)));
  };
  __SET_VAR(data__->,USED,,__GET_VAR(data__->N,));
  if ((__GET_VAR(data__->N,) == __USINT_LITERAL(3))) {
    if ((((__GET_VAR(data__->V1,) <= __GET_VAR(data__->V2,)) && (__GET_VAR(data__->V2,) <= __GET_VAR(data__->V3,))) || ((__GET_VAR(data__->V3,) <= __GET_VAR(data__->V2,)) && (__GET_VAR(data__->V2,) <= __GET_VAR(data__->V1,))))) {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->V2,));
    } else if ((((__GET_VAR(data__->V2,) <= __GET_VAR(data__->V1,)) && (__GET_VAR(data__->V1,) <= __GET_VAR(data__->V3,))) || ((__GET_VAR(data__->V3,) <= __GET_VAR(data__->V1,)) && (__GET_VAR(data__->V1,) <= __GET_VAR(data__->V2,))))) {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->V1,));
    } else {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->V3,));
    };
  } else if ((__GET_VAR(data__->N,) == __USINT_LITERAL(2))) {
    __SET_VAR(data__->,Y,,(__REAL_LITERAL(0.5) * (__GET_VAR(data__->V1,) + __GET_VAR(data__->V2,))));
  } else if ((__GET_VAR(data__->N,) == __USINT_LITERAL(1))) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->V1,));
  } else {
    __SET_VAR(data__->,Y,,__REAL_LITERAL(0.0));
  };

  goto __end;

__end:
  return;
} // ZONE_VOTE_body__() 





static void ZONE_PUBLISH_init__(ZONE_PUBLISH_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->RAW,0,retain)
  __INIT_VAR(data__->USED,0,retain)
  __INIT_VAR(data__->TAU_S,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->HI,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->OK,__BOOL_LITERAL(FALSE),retain)
  EWMA_init__(&data__->FILT,retain);
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void ZONE_PUBLISH_body__(ZONE_PUBLISH_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables

  if ((__GET_VAR(data__->USED,) == __USINT_LITERAL(0))) {
    __SET_VAR(data__->,OK,,__BOOL_LITERAL(FALSE));
    if (!(__GET_VAR(data__->SEEDED,))) {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->LO,));
    };
  } else {
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
    __SET_VAR(data__->FILT.,X,,__GET_VAR(data__->RAW,));
    __SET_VAR(data__->FILT.,TAU_S,,__GET_VAR(data__->TAU_S,));
    __SET_VAR(data__->FILT.,NOW,,__GET_VAR(data__->NOW,));
    EWMA_body__(&data__->FILT);
    __SET_VAR(data__->,Y,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->LO,),
      (REAL)__GET_VAR(data__->FILT.Y,),
      (REAL)__GET_VAR(data__->HI,)));
    __SET_VAR(data__->,OK,,__BOOL_LITERAL(TRUE));
  };

  goto __end;

__end:
  return;
} // ZONE_PUBLISH_body__() 





static void ZONE_PROBES_init__(ZONE_PROBES_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->T1,0,retain)
  __INIT_VAR(data__->T1_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->T2,0,retain)
  __INIT_VAR(data__->T2_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->T3,0,retain)
  __INIT_VAR(data__->T3_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->H1,0,retain)
  __INIT_VAR(data__->H1_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->H2,0,retain)
  __INIT_VAR(data__->H2_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->H3,0,retain)
  __INIT_VAR(data__->H3_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->TAU_S,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->TEMP,0,retain)
  __INIT_VAR(data__->RH,0,retain)
  __INIT_VAR(data__->TEMP_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->RH_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->FAULT,__BOOL_LITERAL(FALSE),retain)
  ZONE_VOTE_init__(&data__->T_VOTE,retain);
  ZONE_VOTE_init__(&data__->H_VOTE,retain);
  ZONE_PUBLISH_init__(&data__->T_PUB,retain);
  ZONE_PUBLISH_init__(&data__->H_PUB,retain);
  TOF_MS_init__(&data__->QUORUM,retain);
}

// Code part
static void ZONE_PROBES_body__(ZONE_PROBES_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,WORST,,0);
  __SET_VAR(data__->,H_RAW,,0);

  __SET_VAR(data__->T_VOTE.,A,,__GET_VAR(data__->T1,));
  __SET_VAR(data__->T_VOTE.,A_OK,,__GET_VAR(data__->T1_OK,));
  __SET_VAR(data__->T_VOTE.,B,,__GET_VAR(data__->T2,));
  __SET_VAR(data__->T_VOTE.,B_OK,,__GET_VAR(data__->T2_OK,));
  __SET_VAR(data__->T_VOTE.,C,,__GET_VAR(data__->T3,));
  __SET_VAR(data__->T_VOTE.,C_OK,,__GET_VAR(data__->T3_OK,));
  ZONE_VOTE_body__(&data__->T_VOTE);
  __SET_VAR(data__->H_VOTE.,A,,__GET_VAR(data__->H1,));
  __SET_VAR(data__->H_VOTE.,A_OK,,__GET_VAR(data__->H1_OK,));
  __SET_VAR(data__->H_VOTE.,B,,__GET_VAR(data__->H2,));
  __SET_VAR(data__->H_VOTE.,B_OK,,__GET_VAR(data__->H2_OK,));
  __SET_VAR(data__->H_VOTE.,C,,__GET_VAR(data__->H3,));
  __SET_VAR(data__->H_VOTE.,C_OK,,__GET_VAR(data__->H3_OK,));
  ZONE_VOTE_body__(&data__->H_VOTE);
  __SET_VAR(data__->,H_RAW,,(__GET_VAR(data__->H_VOTE.Y,) * __REAL_LITERAL(0.01)));
  __SET_VAR(data__->T_PUB.,RAW,,__GET_VAR(data__->T_VOTE.Y,));
  __SET_VAR(data__->T_PUB.,USED,,__GET_VAR(data__->T_VOTE.USED,));
  __SET_VAR(data__->T_PUB.,TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->T_PUB.,NOW,,__GET_VAR(data__->NOW,));
  __SET_VAR(data__->T_PUB.,LO,,__REAL_LITERAL(-100.0));
  __SET_VAR(data__->T_PUB.,HI,,__REAL_LITERAL(200.0));
  ZONE_PUBLISH_body__(&data__->T_PUB);
  __SET_VAR(data__->H_PUB.,RAW,,__GET_VAR(data__->H_RAW,));
  __SET_VAR(data__->H_PUB.,USED,,__GET_VAR(data__->H_VOTE.USED,));
  __SET_VAR(data__->H_PUB.,TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->H_PUB.,NOW,,__GET_VAR(data__->NOW,));
  __SET_VAR(data__->H_PUB.,LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->H_PUB.,HI,,__REAL_LITERAL(1.0));
  ZONE_PUBLISH_body__(&data__->H_PUB);
  __SET_VAR(data__->,TEMP,,__GET_VAR(data__->T_PUB.Y,));
  __SET_VAR(data__->,RH,,__GET_VAR(data__->H_PUB.Y,));
  __SET_VAR(data__->,TEMP_OK,,__GET_VAR(data__->T_PUB.OK,));
  __SET_VAR(data__->,RH_OK,,__GET_VAR(data__->H_PUB.OK,));
  if ((__GET_VAR(data__->T_VOTE.USED,) < __GET_VAR(data__->H_VOTE.USED,))) {
    __SET_VAR(data__->,WORST,,__GET_VAR(data__->T_VOTE.USED,));
  } else {
    __SET_VAR(data__->,WORST,,__GET_VAR(data__->H_VOTE.USED,));
  };
  __SET_VAR(data__->QUORUM.,IN,,(__GET_VAR(data__->WORST,) < __USINT_LITERAL(3)));
  __SET_VAR(data__->QUORUM.,PT,,__UDINT_LITERAL(5000));
  __SET_VAR(data__->QUORUM.,NOW,,__GET_VAR(data__->NOW,));
  TOF_MS_body__(&data__->QUORUM);
  __SET_VAR(data__->,FAULT,,__GET_VAR(data__->QUORUM.Q,));

  goto __end;

__end:
  return;
} // ZONE_PROBES_body__() 





static void FAN_CONTROLLER_init__(FAN_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_STRATDEM,data__->CLI_STRATDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLDEM,data__->CLI_COOLDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANIRRDEM,data__->CLI_FANIRRDEM,retain)
  __INIT_EXTERNAL(USINT,IRR_STATE,data__->IRR_STATE,retain)
  __INIT_EXTERNAL(USINT,CLI_PHASE,data__->CLI_PHASE,retain)
  __INIT_EXTERNAL(REAL,CLI_BREEZEDEM,data__->CLI_BREEZEDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANLOWDEM,data__->CLI_FANLOWDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANMEDDEM,data__->CLI_FANMEDDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANHIGHDEM,data__->CLI_FANHIGHDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANDEMHYST,data__->CLI_FANDEMHYST,retain)
  __INIT_EXTERNAL(BOOL,CLI_DESTRAT1_SPEED1,data__->CLI_DESTRAT1_SPEED1,retain)
  __INIT_EXTERNAL(BOOL,CLI_DESTRAT1_SPEED2,data__->CLI_DESTRAT1_SPEED2,retain)
  __INIT_EXTERNAL(BOOL,CLI_DESTRAT1_SPEED3,data__->CLI_DESTRAT1_SPEED3,retain)
  __INIT_EXTERNAL(BOOL,CLI_DESTRAT2_SPEED1,data__->CLI_DESTRAT2_SPEED1,retain)
  __INIT_EXTERNAL(BOOL,CLI_DESTRAT2_SPEED2,data__->CLI_DESTRAT2_SPEED2,retain)
  __INIT_EXTERNAL(BOOL,CLI_DESTRAT2_SPEED3,data__->CLI_DESTRAT2_SPEED3,retain)
  __INIT_EXTERNAL(USINT,CLI_FANSPEED,data__->CLI_FANSPEED,retain)
  STAIRCASE3_init__(&data__->STEPS,retain);
}

// Code part
static void FAN_CONTROLLER_body__(FAN_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,WATERING,,0);
  __SET_VAR(data__->,BREEZE,,0);
  __SET_VAR(data__->,DEMAND,,0);

  if ((__GET_EXTERNAL(data__->IRR_STATE,) != __USINT_LITERAL(0))) {
    __SET_VAR(data__->,WATERING,,__GET_EXTERNAL(data__->CLI_FANIRRDEM,));
  } else {
    __SET_VAR(data__->,WATERING,,__REAL_LITERAL(0.0));
  };
  if (((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(2)) || (__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(5)))) {
    __SET_VAR(data__->,BREEZE,,__GET_EXTERNAL(data__->CLI_BREEZEDEM,));
  } else {
    __SET_VAR(data__->,BREEZE,,__REAL_LITERAL(0.0));
  };
  __SET_VAR(data__->,DEMAND,,___MAX(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_STRATDEM,),
      (REAL)__GET_EXTERNAL(data__->CLI_COOLDEM,)),
    (REAL)___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->WATERING,),
      (REAL)__GET_VAR(data__->BREEZE,))));
  __SET_VAR(data__->STEPS.,IN,,__GET_VAR(data__->DEMAND,));
  __SET_VAR(data__->STEPS.,LO,,__GET_EXTERNAL(data__->CLI_FANLOWDEM,));
  __SET_VAR(data__->STEPS.,MED,,__GET_EXTERNAL(data__->CLI_FANMEDDEM,));
  __SET_VAR(data__->STEPS.,HI,,__GET_EXTERNAL(data__->CLI_FANHIGHDEM,));
  __SET_VAR(data__->STEPS.,HYST,,__GET_EXTERNAL(data__->CLI_FANDEMHYST,));
  STAIRCASE3_body__(&data__->STEPS);
  __SET_EXTERNAL(data__->,CLI_DESTRAT1_SPEED1,,(__GET_VAR(data__->STEPS.Q,) == __USINT_LITERAL(1)));
  __SET_EXTERNAL(data__->,CLI_DESTRAT2_SPEED1,,(__GET_VAR(data__->STEPS.Q,) == __USINT_LITERAL(1)));
  __SET_EXTERNAL(data__->,CLI_DESTRAT1_SPEED2,,(__GET_VAR(data__->STEPS.Q,) == __USINT_LITERAL(2)));
  __SET_EXTERNAL(data__->,CLI_DESTRAT2_SPEED2,,(__GET_VAR(data__->STEPS.Q,) == __USINT_LITERAL(2)));
  __SET_EXTERNAL(data__->,CLI_DESTRAT1_SPEED3,,(__GET_VAR(data__->STEPS.Q,) == __USINT_LITERAL(3)));
  __SET_EXTERNAL(data__->,CLI_DESTRAT2_SPEED3,,(__GET_VAR(data__->STEPS.Q,) == __USINT_LITERAL(3)));
  __SET_EXTERNAL(data__->,CLI_FANSPEED,,__GET_VAR(data__->STEPS.Q,));

  goto __end;

__end:
  return;
} // FAN_CONTROLLER_body__() 





static void INTRACTOR_CONTROLLER_init__(INTRACTOR_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_COOLDEM,data__->CLI_COOLDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_ROTVENTDEM,data__->CLI_ROTVENTDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_HUMIDDEM,data__->CLI_HUMIDDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_GROUNDSPEED,data__->CLI_GROUNDSPEED,retain)
  __INIT_EXTERNAL(REAL,CLI_INTAKESTARTDEM,data__->CLI_INTAKESTARTDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_INTAKEFULLDEM,data__->CLI_INTAKEFULLDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_INTAKECOOLOK,data__->CLI_INTAKECOOLOK,retain)
  __INIT_EXTERNAL(BOOL,CLI_GROUNDHEATOK,data__->CLI_GROUNDHEATOK,retain)
  __INIT_EXTERNAL(BOOL,CLI_INTAKEDRYOK,data__->CLI_INTAKEDRYOK,retain)
  __INIT_EXTERNAL(REAL,CLI_INTRSAFESPEED,data__->CLI_INTRSAFESPEED,retain)
  __INIT_EXTERNAL(REAL,CLI_INTR1,data__->CLI_INTR1,retain)
  __INIT_EXTERNAL(REAL,CLI_INTR2,data__->CLI_INTR2,retain)
}

// Code part
static void INTRACTOR_CONTROLLER_body__(INTRACTOR_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,COOLING,,0);
  __SET_VAR(data__->,DRYING,,0);
  __SET_VAR(data__->,WETTING,,0);
  __SET_VAR(data__->,DEMAND,,0);
  __SET_VAR(data__->,SPEED,,0);

  if (__GET_EXTERNAL(data__->CLI_INTAKECOOLOK,)) {
    __SET_VAR(data__->,COOLING,,__GET_EXTERNAL(data__->CLI_COOLDEM,));
  } else {
    __SET_VAR(data__->,COOLING,,__REAL_LITERAL(0.0));
  };
  if (__GET_EXTERNAL(data__->CLI_INTAKEDRYOK,)) {
    __SET_VAR(data__->,DRYING,, -(__GET_EXTERNAL(data__->CLI_HUMIDDEM,)));
  } else {
    __SET_VAR(data__->,DRYING,,__REAL_LITERAL(0.0));
  };
  __SET_VAR(data__->,WETTING,,___MAX(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__REAL_LITERAL(0.0),
    (REAL)__GET_EXTERNAL(data__->CLI_HUMIDDEM,)));
  __SET_VAR(data__->,DEMAND,,___MAX(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->COOLING,),
      (REAL)__GET_EXTERNAL(data__->CLI_ROTVENTDEM,)),
    (REAL)___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->DRYING,),
      (REAL)__GET_VAR(data__->WETTING,))));
  __SET_VAR(data__->,SPEED,,___MAX(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_INTRSAFESPEED,),
    (REAL)___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.0),
      (REAL)((__GET_VAR(data__->DEMAND,) - __GET_EXTERNAL(data__->CLI_INTAKESTARTDEM,)) / (__GET_EXTERNAL(data__->CLI_INTAKEFULLDEM,) - __GET_EXTERNAL(data__->CLI_INTAKESTARTDEM,))),
      (REAL)__REAL_LITERAL(1.0))));
  if (__GET_EXTERNAL(data__->CLI_GROUNDHEATOK,)) {
    __SET_VAR(data__->,SPEED,,___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->SPEED,),
      (REAL)__GET_EXTERNAL(data__->CLI_GROUNDSPEED,)));
  };
  __SET_EXTERNAL(data__->,CLI_INTR1,,__GET_VAR(data__->SPEED,));
  __SET_EXTERNAL(data__->,CLI_INTR2,,__GET_VAR(data__->SPEED,));

  goto __end;

__end:
  return;
} // INTRACTOR_CONTROLLER_body__() 





static void ROOF_LOCKOUT_init__(ROOF_LOCKOUT_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_RAIN_MMPH,data__->CLI_RAIN_MMPH,retain)
  __INIT_EXTERNAL(BOOL,CLI_RAIN_MMPH_QLT,data__->CLI_RAIN_MMPH_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WIND_MPS,data__->CLI_WIND_MPS,retain)
  __INIT_EXTERNAL(BOOL,CLI_WIND_MPS_QLT,data__->CLI_WIND_MPS_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDGUST_MPS,data__->CLI_WINDGUST_MPS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDSHUTBEGIN_MPS,data__->CLI_WINDSHUTBEGIN_MPS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDSHUTEND_MPS,data__->CLI_WINDSHUTEND_MPS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDSHUTMAX,data__->CLI_WINDSHUTMAX,retain)
  __INIT_EXTERNAL(UINT,CLI_RAINLOCKOUT_SEC,data__->CLI_RAINLOCKOUT_SEC,retain)
  __INIT_EXTERNAL(BOOL,SYS_SUPPLY_LOCKOUT,data__->SYS_SUPPLY_LOCKOUT,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(BOOL,CLI_RAINLOCKOUT,data__->CLI_RAINLOCKOUT,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDLOCKOUT,data__->CLI_WINDLOCKOUT,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOWLOCKOUT,data__->CLI_WINDOWLOCKOUT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOWCAP,data__->CLI_WINDOWCAP,retain)
  TOF_MS_init__(&data__->RAIN_FOR,retain);
}

// Code part
static void ROOF_LOCKOUT_body__(ROOF_LOCKOUT_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,BLIND,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,W,,0);
  __SET_VAR(data__->,CEILING,,0);

  __SET_VAR(data__->,BLIND,,(!(__GET_EXTERNAL(data__->CLI_RAIN_MMPH_QLT,)) || !(__GET_EXTERNAL(data__->CLI_WIND_MPS_QLT,))));
  __SET_VAR(data__->RAIN_FOR.,IN,,(__GET_EXTERNAL(data__->CLI_RAIN_MMPH,) > __REAL_LITERAL(0.0)));
  __SET_VAR(data__->RAIN_FOR.,PT,,(___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_RAINLOCKOUT_SEC,)) * __UDINT_LITERAL(1000)));
  __SET_VAR(data__->RAIN_FOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TOF_MS_body__(&data__->RAIN_FOR);
  __SET_EXTERNAL(data__->,CLI_RAINLOCKOUT,,(__GET_VAR(data__->RAIN_FOR.Q,) || __GET_VAR(data__->BLIND,)));
  if ((__GET_EXTERNAL(data__->CLI_WINDGUST_MPS,) > __GET_EXTERNAL(data__->CLI_WIND_MPS,))) {
    __SET_VAR(data__->,W,,__GET_EXTERNAL(data__->CLI_WINDGUST_MPS,));
  } else {
    __SET_VAR(data__->,W,,__GET_EXTERNAL(data__->CLI_WIND_MPS,));
  };
  __SET_EXTERNAL(data__->,CLI_WINDLOCKOUT,,(__GET_VAR(data__->BLIND,) || (__GET_VAR(data__->W,) >= __GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,))));
  __SET_EXTERNAL(data__->,CLI_WINDOWLOCKOUT,,((__GET_EXTERNAL(data__->CLI_RAINLOCKOUT,) || __GET_EXTERNAL(data__->CLI_WINDLOCKOUT,)) || __GET_EXTERNAL(data__->SYS_SUPPLY_LOCKOUT,)));
  __SET_VAR(data__->,CEILING,,__REAL_LITERAL(1.0));
  if (((__GET_VAR(data__->W,) >= __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,)) && (__GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,) > __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,)))) {
    __SET_VAR(data__->,CEILING,,(__GET_EXTERNAL(data__->CLI_WINDSHUTMAX,) * (__REAL_LITERAL(1.0) - ((__GET_VAR(data__->W,) - __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,)) / (__GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,) - __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,))))));
  };
  if (__GET_EXTERNAL(data__->CLI_WINDOWLOCKOUT,)) {
    __SET_EXTERNAL(data__->,CLI_WINDOWCAP,,__REAL_LITERAL(0.0));
  } else {
    __SET_EXTERNAL(data__->,CLI_WINDOWCAP,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.0),
      (REAL)__GET_VAR(data__->CEILING,),
      (REAL)__REAL_LITERAL(1.0)));
  };

  goto __end;

__end:
  return;
} // ROOF_LOCKOUT_body__() 





static void SKIRT_CONTROLLER_init__(SKIRT_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_COOLDEM,data__->CLI_COOLDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_COOLDEM_QLT,data__->CLI_COOLDEM_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_HUMIDDEM,data__->CLI_HUMIDDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_SKIRTCOOLDEM,data__->CLI_SKIRTCOOLDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_SKIRTDRYMIN_DEGC,data__->CLI_SKIRTDRYMIN_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DPMARGIN_DEGC,data__->CLI_DPMARGIN_DEGC,retain)
  __INIT_EXTERNAL(BOOL,CLI_DPMARGIN_DEGC_QLT,data__->CLI_DPMARGIN_DEGC_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_ROTLOCKOUT,data__->CLI_ROTLOCKOUT,retain)
  __INIT_EXTERNAL(UINT,CLI_SKIRTPERIOD_SEC,data__->CLI_SKIRTPERIOD_SEC,retain)
  __INIT_EXTERNAL(UINT,CLI_SKIRTMINON_SEC,data__->CLI_SKIRTMINON_SEC,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(BOOL,CLI_SKIRTVALVE,data__->CLI_SKIRTVALVE,retain)
  __INIT_EXTERNAL(REAL,CLI_SKIRTDUTY,data__->CLI_SKIRTDUTY,retain)
  VALVE_PULSE_init__(&data__->PULSE,retain);
}

// Code part
static void SKIRT_CONTROLLER_body__(SKIRT_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,HUMID,,0);
  __SET_VAR(data__->,COOL,,0);
  __SET_VAR(data__->,WANT,,0);
  __SET_VAR(data__->,DRY,,__BOOL_LITERAL(FALSE));

  __SET_VAR(data__->,HUMID,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__REAL_LITERAL(0.0),
    (REAL)__GET_EXTERNAL(data__->CLI_HUMIDDEM,),
    (REAL)__REAL_LITERAL(1.0)));
  __SET_VAR(data__->,DRY,,(__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC_QLT,) && (__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,) >= __GET_EXTERNAL(data__->CLI_SKIRTDRYMIN_DEGC,))));
  if ((__GET_EXTERNAL(data__->CLI_COOLDEM_QLT,) && __GET_VAR(data__->DRY,))) {
    __SET_VAR(data__->,COOL,,___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_COOLDEM,),
      (REAL)__GET_EXTERNAL(data__->CLI_SKIRTCOOLDEM,),
      (REAL)__REAL_LITERAL(1.0)));
  } else {
    __SET_VAR(data__->,COOL,,__REAL_LITERAL(0.0));
  };
  if ((__GET_VAR(data__->HUMID,) > __GET_VAR(data__->COOL,))) {
    __SET_VAR(data__->,WANT,,__GET_VAR(data__->HUMID,));
  } else {
    __SET_VAR(data__->,WANT,,__GET_VAR(data__->COOL,));
  };
  if (__GET_EXTERNAL(data__->CLI_ROTLOCKOUT,)) {
    __SET_VAR(data__->,WANT,,__REAL_LITERAL(0.0));
  };
  __SET_VAR(data__->PULSE.,DUTY_IN,,__GET_VAR(data__->WANT,));
  __SET_VAR(data__->PULSE.,PERIOD_MS,,(___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SKIRTPERIOD_SEC,)) * __UDINT_LITERAL(1000)));
  __SET_VAR(data__->PULSE.,MIN_ON_MS,,(___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SKIRTMINON_SEC,)) * __UDINT_LITERAL(1000)));
  __SET_VAR(data__->PULSE.,RESET,,__GET_EXTERNAL(data__->CLI_ROTLOCKOUT,));
  __SET_VAR(data__->PULSE.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  VALVE_PULSE_body__(&data__->PULSE);
  if (__GET_EXTERNAL(data__->CLI_ROTLOCKOUT,)) {
    __SET_EXTERNAL(data__->,CLI_SKIRTVALVE,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,CLI_SKIRTDUTY,,__REAL_LITERAL(0.0));
  } else {
    __SET_EXTERNAL(data__->,CLI_SKIRTVALVE,,__GET_VAR(data__->PULSE.Q,));
    __SET_EXTERNAL(data__->,CLI_SKIRTDUTY,,__GET_VAR(data__->PULSE.SERVED,));
  };

  goto __end;

__end:
  return;
} // SKIRT_CONTROLLER_body__() 





static void COOLING_CONTROLLER_init__(COOLING_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_IN,data__->CLI_TEMP_IN,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_IN_QLT,data__->CLI_TEMP_IN_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TARGETTEMP_DEGC,data__->CLI_TARGETTEMP_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOWSTARTDEM,data__->CLI_WINDOWSTARTDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOWFULLDEM,data__->CLI_WINDOWFULLDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLKP,data__->CLI_COOLKP,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLTI_SEC,data__->CLI_COOLTI_SEC,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLTD_SEC,data__->CLI_COOLTD_SEC,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLDEM,data__->CLI_COOLDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_COOLDEM_QLT,data__->CLI_COOLDEM_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOWDEM,data__->CLI_WINDOWDEM,retain)
  PID_init__(&data__->LOOP,retain);
}

// Code part
static void COOLING_CONTROLLER_body__(COOLING_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,MEASURED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,KI,,0);

  __SET_VAR(data__->,MEASURED,,__GET_EXTERNAL(data__->CLI_TEMP_IN_QLT,));
  if ((__GET_EXTERNAL(data__->CLI_COOLTI_SEC,) > __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,KI,,(__GET_EXTERNAL(data__->CLI_COOLKP,) / __GET_EXTERNAL(data__->CLI_COOLTI_SEC,)));
  } else {
    __SET_VAR(data__->,KI,,__REAL_LITERAL(0.0));
  };
  __SET_VAR(data__->LOOP.,SP,,__GET_EXTERNAL(data__->CLI_TARGETTEMP_DEGC,));
  __SET_VAR(data__->LOOP.,PV,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  __SET_VAR(data__->LOOP.,KP,,__GET_EXTERNAL(data__->CLI_COOLKP,));
  __SET_VAR(data__->LOOP.,KI,,__GET_VAR(data__->KI,));
  __SET_VAR(data__->LOOP.,KD,,(__GET_EXTERNAL(data__->CLI_COOLKP,) * __GET_EXTERNAL(data__->CLI_COOLTD_SEC,)));
  __SET_VAR(data__->LOOP.,OUT_MIN,,__REAL_LITERAL(-1.0));
  __SET_VAR(data__->LOOP.,OUT_MAX,,__REAL_LITERAL(1.0));
  __SET_VAR(data__->LOOP.,D_TAU_S,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->LOOP.,REVERSE,,__BOOL_LITERAL(TRUE));
  __SET_VAR(data__->LOOP.,RESET,,!(__GET_VAR(data__->MEASURED,)));
  __SET_VAR(data__->LOOP.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  PID_body__(&data__->LOOP);
  __SET_EXTERNAL(data__->,CLI_COOLDEM,,__GET_VAR(data__->LOOP.Y,));
  __SET_EXTERNAL(data__->,CLI_COOLDEM_QLT,,__GET_VAR(data__->MEASURED,));
  if (__GET_VAR(data__->MEASURED,)) {
    __SET_EXTERNAL(data__->,CLI_WINDOWDEM,,___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->LOOP.Y,),
      (REAL)__GET_EXTERNAL(data__->CLI_WINDOWSTARTDEM,),
      (REAL)__GET_EXTERNAL(data__->CLI_WINDOWFULLDEM,)));
  } else {
    __SET_EXTERNAL(data__->,CLI_WINDOWDEM,,__REAL_LITERAL(0.25));
  };

  goto __end;

__end:
  return;
} // COOLING_CONTROLLER_body__() 





static void DAY_PHASE_CONTROLLER_init__(DAY_PHASE_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_IN,data__->CLI_TEMP_IN,retain)
  __INIT_EXTERNAL(BOOL,SOL_READY,data__->SOL_READY,retain)
  __INIT_EXTERNAL(INT,SOL_FROMRISE_MIN,data__->SOL_FROMRISE_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_FROMSET_MIN,data__->SOL_FROMSET_MIN,retain)
  __INIT_EXTERNAL(REAL,CLI_COLDBANKTRG_DEGC,data__->CLI_COLDBANKTRG_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_HEATBANKTRG_DEGC,data__->CLI_HEATBANKTRG_DEGC,retain)
  __INIT_EXTERNAL(INT,SOL_COLDBANKRISE_MIN,data__->SOL_COLDBANKRISE_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_BREEZERISE_MIN,data__->SOL_BREEZERISE_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_DAYRISE_MIN,data__->SOL_DAYRISE_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_HEATBANKSET_MIN,data__->SOL_HEATBANKSET_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_DUSKSET_MIN,data__->SOL_DUSKSET_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_NIGHTSET_MIN,data__->SOL_NIGHTSET_MIN,retain)
  __INIT_EXTERNAL(USINT,CLI_PHASE,data__->CLI_PHASE,retain)
  __INIT_EXTERNAL(BOOL,CLI_PHASE_QLT,data__->CLI_PHASE_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_COLDBANKARM,data__->CLI_COLDBANKARM,retain)
  __INIT_EXTERNAL(BOOL,CLI_HEATBANKARM,data__->CLI_HEATBANKARM,retain)
  __INIT_EXTERNAL(REAL,CLI_PREVDAYMAX_DEGC,data__->CLI_PREVDAYMAX_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_PREVNIGHTMIN_DEGC,data__->CLI_PREVNIGHTMIN_DEGC,retain)
  __INIT_VAR(data__->PHASE,__USINT_LITERAL(0),retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->DAY_MAX,0,retain)
  __INIT_VAR(data__->NIGHT_MIN,0,retain)
}

// Code part
static void DAY_PHASE_CONTROLLER_body__(DAY_PHASE_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,WAS,,0);

  if (!(__GET_VAR(data__->SEEDED,))) {
    __SET_VAR(data__->,DAY_MAX,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
    __SET_VAR(data__->,NIGHT_MIN,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  if ((__GET_EXTERNAL(data__->CLI_TEMP_IN,) > __GET_VAR(data__->DAY_MAX,))) {
    __SET_VAR(data__->,DAY_MAX,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  };
  if ((__GET_EXTERNAL(data__->CLI_TEMP_IN,) < __GET_VAR(data__->NIGHT_MIN,))) {
    __SET_VAR(data__->,NIGHT_MIN,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  };
  __SET_VAR(data__->,WAS,,__GET_VAR(data__->PHASE,));
  if (__GET_EXTERNAL(data__->SOL_READY,)) {
    if ((__GET_EXTERNAL(data__->SOL_FROMRISE_MIN,) < __GET_EXTERNAL(data__->SOL_COLDBANKRISE_MIN,))) {
      __SET_VAR(data__->,PHASE,,__USINT_LITERAL(0));
    } else if ((__GET_EXTERNAL(data__->SOL_FROMRISE_MIN,) < __GET_EXTERNAL(data__->SOL_BREEZERISE_MIN,))) {
      if (__GET_EXTERNAL(data__->CLI_COLDBANKARM,)) {
        __SET_VAR(data__->,PHASE,,__USINT_LITERAL(1));
      } else {
        __SET_VAR(data__->,PHASE,,__USINT_LITERAL(0));
      };
    } else if ((__GET_EXTERNAL(data__->SOL_FROMRISE_MIN,) < __GET_EXTERNAL(data__->SOL_DAYRISE_MIN,))) {
      __SET_VAR(data__->,PHASE,,__USINT_LITERAL(2));
    } else if ((__GET_EXTERNAL(data__->SOL_FROMSET_MIN,) < __GET_EXTERNAL(data__->SOL_HEATBANKSET_MIN,))) {
      __SET_VAR(data__->,PHASE,,__USINT_LITERAL(3));
    } else if ((__GET_EXTERNAL(data__->SOL_FROMSET_MIN,) < __GET_EXTERNAL(data__->SOL_DUSKSET_MIN,))) {
      if (__GET_EXTERNAL(data__->CLI_HEATBANKARM,)) {
        __SET_VAR(data__->,PHASE,,__USINT_LITERAL(4));
      } else {
        __SET_VAR(data__->,PHASE,,__USINT_LITERAL(3));
      };
    } else if ((__GET_EXTERNAL(data__->SOL_FROMSET_MIN,) < __GET_EXTERNAL(data__->SOL_NIGHTSET_MIN,))) {
      __SET_VAR(data__->,PHASE,,__USINT_LITERAL(5));
    } else {
      __SET_VAR(data__->,PHASE,,__USINT_LITERAL(0));
    };
  };
  if (((__GET_VAR(data__->PHASE,) == __USINT_LITERAL(3)) && (__GET_VAR(data__->WAS,) != __USINT_LITERAL(3)))) {
    __SET_VAR(data__->,DAY_MAX,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  };
  if (((__GET_VAR(data__->PHASE,) == __USINT_LITERAL(0)) && (__GET_VAR(data__->WAS,) != __USINT_LITERAL(0)))) {
    __SET_VAR(data__->,NIGHT_MIN,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  };
  __SET_EXTERNAL(data__->,CLI_PHASE,,__GET_VAR(data__->PHASE,));
  __SET_EXTERNAL(data__->,CLI_PHASE_QLT,,__GET_EXTERNAL(data__->SOL_READY,));
  __SET_EXTERNAL(data__->,CLI_PREVDAYMAX_DEGC,,__GET_VAR(data__->DAY_MAX,));
  __SET_EXTERNAL(data__->,CLI_PREVNIGHTMIN_DEGC,,__GET_VAR(data__->NIGHT_MIN,));
  __SET_EXTERNAL(data__->,CLI_COLDBANKARM,,(__GET_EXTERNAL(data__->CLI_PREVDAYMAX_DEGC,) > __GET_EXTERNAL(data__->CLI_COLDBANKTRG_DEGC,)));
  __SET_EXTERNAL(data__->,CLI_HEATBANKARM,,(__GET_EXTERNAL(data__->CLI_PREVNIGHTMIN_DEGC,) < __GET_EXTERNAL(data__->CLI_HEATBANKTRG_DEGC,)));

  goto __end;

__end:
  return;
} // DAY_PHASE_CONTROLLER_body__() 





static void DESTRAT_CONTROLLER_init__(DESTRAT_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_DTSTRAT_DEGC,data__->CLI_DTSTRAT_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRATLOW_DEGC,data__->CLI_DTSTRATLOW_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRATMED_DEGC,data__->CLI_DTSTRATMED_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRATHIGH_DEGC,data__->CLI_DTSTRATHIGH_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRATHYST_DEGC,data__->CLI_DTSTRATHYST_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_FANLOWDEM,data__->CLI_FANLOWDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANMEDDEM,data__->CLI_FANMEDDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_FANHIGHDEM,data__->CLI_FANHIGHDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_STRATDEM,data__->CLI_STRATDEM,retain)
  STAIRCASE3_init__(&data__->BANDS,retain);
}

// Code part
static void DESTRAT_CONTROLLER_body__(DESTRAT_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables

  __SET_VAR(data__->BANDS.,IN,,__GET_EXTERNAL(data__->CLI_DTSTRAT_DEGC,));
  __SET_VAR(data__->BANDS.,LO,,__GET_EXTERNAL(data__->CLI_DTSTRATLOW_DEGC,));
  __SET_VAR(data__->BANDS.,MED,,__GET_EXTERNAL(data__->CLI_DTSTRATMED_DEGC,));
  __SET_VAR(data__->BANDS.,HI,,__GET_EXTERNAL(data__->CLI_DTSTRATHIGH_DEGC,));
  __SET_VAR(data__->BANDS.,HYST,,__GET_EXTERNAL(data__->CLI_DTSTRATHYST_DEGC,));
  STAIRCASE3_body__(&data__->BANDS);
  {
    USINT __case_expression = __GET_VAR(data__->BANDS.Q,);
    if ((__case_expression == __USINT_LITERAL(1))) {
      __SET_EXTERNAL(data__->,CLI_STRATDEM,,__GET_EXTERNAL(data__->CLI_FANLOWDEM,));
    }
    else if ((__case_expression == __USINT_LITERAL(2))) {
      __SET_EXTERNAL(data__->,CLI_STRATDEM,,__GET_EXTERNAL(data__->CLI_FANMEDDEM,));
    }
    else if ((__case_expression == __USINT_LITERAL(3))) {
      __SET_EXTERNAL(data__->,CLI_STRATDEM,,__GET_EXTERNAL(data__->CLI_FANHIGHDEM,));
    }
    else {
      __SET_EXTERNAL(data__->,CLI_STRATDEM,,__REAL_LITERAL(0.0));
    }
  };

  goto __end;

__end:
  return;
} // DESTRAT_CONTROLLER_body__() 





static void HUMIDITY_CONTROLLER_init__(HUMIDITY_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(UINT,CLI_ROTRELEASE_SEC,data__->CLI_ROTRELEASE_SEC,retain)
  __INIT_EXTERNAL(REAL,CLI_DPMARGIN_DEGC,data__->CLI_DPMARGIN_DEGC,retain)
  __INIT_EXTERNAL(BOOL,CLI_DPMARGIN_DEGC_QLT,data__->CLI_DPMARGIN_DEGC_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_DEHUMFULL_DEGC,data__->CLI_DEHUMFULL_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DEHUMSTART_DEGC,data__->CLI_DEHUMSTART_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_HUMIDSTART_DEGC,data__->CLI_HUMIDSTART_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_HUMIDFULL_DEGC,data__->CLI_HUMIDFULL_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_ROTMARGIN_DEGC,data__->CLI_ROTMARGIN_DEGC,retain)
  __INIT_EXTERNAL(BOOL,ALARM_ROT,data__->ALARM_ROT,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,CLI_HUMIDDEM,data__->CLI_HUMIDDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_ROTLOCKOUT,data__->CLI_ROTLOCKOUT,retain)
  __INIT_EXTERNAL(REAL,CLI_ROTVENTDEM,data__->CLI_ROTVENTDEM,retain)
  TOF_MS_init__(&data__->LOCKOUT_FOR,retain);
}

// Code part
static void HUMIDITY_CONTROLLER_body__(HUMIDITY_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,ROTTING,,__BOOL_LITERAL(FALSE));

  __SET_EXTERNAL(data__->,CLI_HUMIDDEM,,___SIGNED_LADDER(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,),
    (REAL)__GET_EXTERNAL(data__->CLI_DEHUMFULL_DEGC,),
    (REAL)__GET_EXTERNAL(data__->CLI_DEHUMSTART_DEGC,),
    (REAL)__GET_EXTERNAL(data__->CLI_HUMIDSTART_DEGC,),
    (REAL)__GET_EXTERNAL(data__->CLI_HUMIDFULL_DEGC,)));
  if (__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC_QLT,)) {
    __SET_EXTERNAL(data__->,CLI_ROTVENTDEM,,___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)(__REAL_LITERAL(0.0) - __GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,)),
      (REAL)(__REAL_LITERAL(0.0) - __GET_EXTERNAL(data__->CLI_ROTMARGIN_DEGC,)),
      (REAL)__REAL_LITERAL(0.0)));
    __SET_VAR(data__->,ROTTING,,__GET_EXTERNAL(data__->ALARM_ROT,));
  } else {
    __SET_EXTERNAL(data__->,CLI_ROTVENTDEM,,__REAL_LITERAL(1.0));
    __SET_VAR(data__->,ROTTING,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->LOCKOUT_FOR.,IN,,__GET_VAR(data__->ROTTING,));
  __SET_VAR(data__->LOCKOUT_FOR.,PT,,(___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_ROTRELEASE_SEC,)) * __UDINT_LITERAL(1000)));
  __SET_VAR(data__->LOCKOUT_FOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TOF_MS_body__(&data__->LOCKOUT_FOR);
  __SET_EXTERNAL(data__->,CLI_ROTLOCKOUT,,__GET_VAR(data__->LOCKOUT_FOR.Q,));

  goto __end;

__end:
  return;
} // HUMIDITY_CONTROLLER_body__() 





static void INTAKE_DRY_GATE_init__(INTAKE_DRY_GATE_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI,data__->CLI_TEMP_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_HI,data__->CLI_RH_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_EXT,data__->CLI_TEMP_EXT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_EXT,data__->CLI_RH_EXT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI_QLT,data__->CLI_TEMP_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_HI_QLT,data__->CLI_RH_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_EXT_QLT,data__->CLI_TEMP_EXT_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_EXT_QLT,data__->CLI_RH_EXT_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_VENTDRYMGN_DEGC,data__->CLI_VENTDRYMGN_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_VENTHYST_DEGC,data__->CLI_VENTHYST_DEGC,retain)
  __INIT_EXTERNAL(BOOL,CLI_INTAKEDRYOK,data__->CLI_INTAKEDRYOK,retain)
  BAND_LATCH_init__(&data__->GATE,retain);
}

// Code part
static void INTAKE_DRY_GATE_body__(INTAKE_DRY_GATE_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,HAVE,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,DIFF,,0);

  __SET_VAR(data__->,HAVE,,(((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) && __GET_EXTERNAL(data__->CLI_RH_HI_QLT,)) && __GET_EXTERNAL(data__->CLI_TEMP_EXT_QLT,)) && __GET_EXTERNAL(data__->CLI_RH_EXT_QLT,)));
  if (__GET_VAR(data__->HAVE,)) {
    __SET_VAR(data__->,DIFF,,(___DEW_POINT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_TEMP_HI,),
      (REAL)__GET_EXTERNAL(data__->CLI_RH_HI,)) - ___DEW_POINT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_TEMP_EXT,),
      (REAL)__GET_EXTERNAL(data__->CLI_RH_EXT,))));
    __SET_VAR(data__->GATE.,IN,,__GET_VAR(data__->DIFF,));
    __SET_VAR(data__->GATE.,RISE,,__GET_EXTERNAL(data__->CLI_VENTDRYMGN_DEGC,));
    __SET_VAR(data__->GATE.,FALL,,(__GET_EXTERNAL(data__->CLI_VENTDRYMGN_DEGC,) - __GET_EXTERNAL(data__->CLI_VENTHYST_DEGC,)));
    BAND_LATCH_body__(&data__->GATE);
    __SET_EXTERNAL(data__->,CLI_INTAKEDRYOK,,__GET_VAR(data__->GATE.Q,));
  } else {
    __SET_EXTERNAL(data__->,CLI_INTAKEDRYOK,,__BOOL_LITERAL(FALSE));
  };

  goto __end;

__end:
  return;
} // INTAKE_DRY_GATE_body__() 





static void INTAKE_GATES_init__(INTAKE_GATES_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_IN,data__->CLI_TEMP_IN,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_CS,data__->CLI_TEMP_CS,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLDEM,data__->CLI_COOLDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_IN_QLT,data__->CLI_TEMP_IN_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_CS_QLT,data__->CLI_TEMP_CS_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_COOLDEM_QLT,data__->CLI_COOLDEM_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_VENTTEMPMGN_DEGC,data__->CLI_VENTTEMPMGN_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_VENTHYST_DEGC,data__->CLI_VENTHYST_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_GROUNDHEATDEM,data__->CLI_GROUNDHEATDEM,retain)
  __INIT_EXTERNAL(REAL,CLI_GROUNDDEMHYST,data__->CLI_GROUNDDEMHYST,retain)
  __INIT_EXTERNAL(BOOL,CLI_INTAKECOOLOK,data__->CLI_INTAKECOOLOK,retain)
  __INIT_EXTERNAL(BOOL,CLI_GROUNDHEATOK,data__->CLI_GROUNDHEATOK,retain)
  BAND_LATCH_init__(&data__->COOL_GATE,retain);
  BAND_LATCH_init__(&data__->GROUND_BAND,retain);
  BAND_LATCH_init__(&data__->GROUND_GATE,retain);
}

// Code part
static void INTAKE_GATES_body__(INTAKE_GATES_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,ASKING,,__BOOL_LITERAL(FALSE));

  if (((!(__GET_EXTERNAL(data__->CLI_TEMP_IN_QLT,)) || !(__GET_EXTERNAL(data__->CLI_COOLDEM_QLT,))) || !(__GET_EXTERNAL(data__->CLI_TEMP_CS_QLT,)))) {
    __SET_EXTERNAL(data__->,CLI_INTAKECOOLOK,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,CLI_GROUNDHEATOK,,__BOOL_LITERAL(FALSE));
  } else {
    __SET_VAR(data__->COOL_GATE.,IN,,(__GET_EXTERNAL(data__->CLI_TEMP_IN,) - __GET_EXTERNAL(data__->CLI_TEMP_CS,)));
    __SET_VAR(data__->COOL_GATE.,RISE,,__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,));
    __SET_VAR(data__->COOL_GATE.,FALL,,(__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,) - __GET_EXTERNAL(data__->CLI_VENTHYST_DEGC,)));
    BAND_LATCH_body__(&data__->COOL_GATE);
    __SET_EXTERNAL(data__->,CLI_INTAKECOOLOK,,__GET_VAR(data__->COOL_GATE.Q,));
    __SET_VAR(data__->GROUND_BAND.,IN,,(__REAL_LITERAL(0.0) - __GET_EXTERNAL(data__->CLI_COOLDEM,)));
    __SET_VAR(data__->GROUND_BAND.,RISE,,(__REAL_LITERAL(0.0) - __GET_EXTERNAL(data__->CLI_GROUNDHEATDEM,)));
    __SET_VAR(data__->GROUND_BAND.,FALL,,((__REAL_LITERAL(0.0) - __GET_EXTERNAL(data__->CLI_GROUNDHEATDEM,)) + __GET_EXTERNAL(data__->CLI_GROUNDDEMHYST,)));
    BAND_LATCH_body__(&data__->GROUND_BAND);
    __SET_VAR(data__->,ASKING,,__GET_VAR(data__->GROUND_BAND.Q,));
    __SET_VAR(data__->GROUND_GATE.,IN,,(__GET_EXTERNAL(data__->CLI_TEMP_CS,) - __GET_EXTERNAL(data__->CLI_TEMP_IN,)));
    __SET_VAR(data__->GROUND_GATE.,RISE,,__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,));
    __SET_VAR(data__->GROUND_GATE.,FALL,,(__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,) - __GET_EXTERNAL(data__->CLI_VENTHYST_DEGC,)));
    BAND_LATCH_body__(&data__->GROUND_GATE);
    __SET_EXTERNAL(data__->,CLI_GROUNDHEATOK,,(__GET_VAR(data__->ASKING,) && __GET_VAR(data__->GROUND_GATE.Q,)));
  };

  goto __end;

__end:
  return;
} // INTAKE_GATES_body__() 





static void TARGET_TEMP_CONTROLLER_init__(TARGET_TEMP_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TARGETMIN_DEGC,data__->CLI_TARGETMIN_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_TARGETMAX_DEGC,data__->CLI_TARGETMAX_DEGC,retain)
  __INIT_EXTERNAL(USINT,CLI_PHASE,data__->CLI_PHASE,retain)
  __INIT_EXTERNAL(BOOL,CLI_PHASE_QLT,data__->CLI_PHASE_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_COLDBANKARM,data__->CLI_COLDBANKARM,retain)
  __INIT_EXTERNAL(BOOL,CLI_HEATBANKARM,data__->CLI_HEATBANKARM,retain)
  __INIT_EXTERNAL(REAL,CLI_COOLSET_DEGC,data__->CLI_COOLSET_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_COLDBANKOFS_DEGC,data__->CLI_COLDBANKOFS_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_HEATBANKOFS_DEGC,data__->CLI_HEATBANKOFS_DEGC,retain)
  __INIT_EXTERNAL(UINT,CLI_TARGETTAU_S,data__->CLI_TARGETTAU_S,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,CLI_TARGETTEMP_DEGC,data__->CLI_TARGETTEMP_DEGC,retain)
  EWMA_init__(&data__->SMOOTH,retain);
}

// Code part
static void TARGET_TEMP_CONTROLLER_body__(TARGET_TEMP_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,HELD,,0);

  __SET_VAR(data__->,HELD,,__GET_EXTERNAL(data__->CLI_COOLSET_DEGC,));
  if (__GET_EXTERNAL(data__->CLI_PHASE_QLT,)) {
    if (((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(1)) && __GET_EXTERNAL(data__->CLI_COLDBANKARM,))) {
      __SET_VAR(data__->,HELD,,(__GET_VAR(data__->HELD,) - __GET_EXTERNAL(data__->CLI_COLDBANKOFS_DEGC,)));
    } else if (((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(4)) && __GET_EXTERNAL(data__->CLI_HEATBANKARM,))) {
      __SET_VAR(data__->,HELD,,(__GET_VAR(data__->HELD,) + __GET_EXTERNAL(data__->CLI_HEATBANKOFS_DEGC,)));
    };
  };
  __SET_VAR(data__->SMOOTH.,X,,__GET_VAR(data__->HELD,));
  __SET_VAR(data__->SMOOTH.,TAU_S,,___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_TARGETTAU_S,)));
  __SET_VAR(data__->SMOOTH.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  EWMA_body__(&data__->SMOOTH);
  __SET_EXTERNAL(data__->,CLI_TARGETTEMP_DEGC,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_TARGETMIN_DEGC,),
    (REAL)__GET_VAR(data__->SMOOTH.Y,),
    (REAL)__GET_EXTERNAL(data__->CLI_TARGETMAX_DEGC,)));

  goto __end;

__end:
  return;
} // TARGET_TEMP_CONTROLLER_body__() 





static void AC_GRID_WATCH_init__(AC_GRID_WATCH_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,GRID_LN_V,data__->GRID_LN_V,retain)
  __INIT_EXTERNAL(BOOL,GRID_LN_V_QLT,data__->GRID_LN_V_QLT,retain)
  __INIT_EXTERNAL(REAL,GRID_TOTAL_A,data__->GRID_TOTAL_A,retain)
  __INIT_EXTERNAL(BOOL,GRID_TOTAL_A_QLT,data__->GRID_TOTAL_A_QLT,retain)
  __INIT_EXTERNAL(REAL,GRID_TOTAL_W,data__->GRID_TOTAL_W,retain)
  __INIT_EXTERNAL(BOOL,GRID_TOTAL_W_QLT,data__->GRID_TOTAL_W_QLT,retain)
  __INIT_EXTERNAL(REAL,GRID_HZ,data__->GRID_HZ,retain)
  __INIT_EXTERNAL(BOOL,GRID_HZ_QLT,data__->GRID_HZ_QLT,retain)
  __INIT_EXTERNAL(REAL,PWR_GRIDLO_V,data__->PWR_GRIDLO_V,retain)
  __INIT_EXTERNAL(REAL,PWR_GRIDHI_V,data__->PWR_GRIDHI_V,retain)
  __INIT_EXTERNAL(REAL,PWR_GRIDLO_HZ,data__->PWR_GRIDLO_HZ,retain)
  __INIT_EXTERNAL(REAL,PWR_GRIDHI_HZ,data__->PWR_GRIDHI_HZ,retain)
  __INIT_EXTERNAL(REAL,SYS_GRID_V,data__->SYS_GRID_V,retain)
  __INIT_EXTERNAL(BOOL,SYS_GRID_V_QLT,data__->SYS_GRID_V_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_GRID_A,data__->SYS_GRID_A,retain)
  __INIT_EXTERNAL(BOOL,SYS_GRID_A_QLT,data__->SYS_GRID_A_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_GRID_W,data__->SYS_GRID_W,retain)
  __INIT_EXTERNAL(BOOL,SYS_GRID_W_QLT,data__->SYS_GRID_W_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_GRID_HZ,data__->SYS_GRID_HZ,retain)
  __INIT_EXTERNAL(BOOL,SYS_GRID_HZ_QLT,data__->SYS_GRID_HZ_QLT,retain)
  __INIT_EXTERNAL(BOOL,SYS_GRID_OK,data__->SYS_GRID_OK,retain)
  __INIT_EXTERNAL(BOOL,ALARM_AC_GRID,data__->ALARM_AC_GRID,retain)
  AC_SUPPLY_init__(&data__->SUPPLY,retain);
}

// Code part
static void AC_GRID_WATCH_body__(AC_GRID_WATCH_data__ *data__) {
  // Initialise TEMP variables

  __SET_VAR(data__->SUPPLY.,V,,__GET_EXTERNAL(data__->GRID_LN_V,));
  __SET_VAR(data__->SUPPLY.,V_OK,,__GET_EXTERNAL(data__->GRID_LN_V_QLT,));
  __SET_VAR(data__->SUPPLY.,A,,__GET_EXTERNAL(data__->GRID_TOTAL_A,));
  __SET_VAR(data__->SUPPLY.,A_OK,,__GET_EXTERNAL(data__->GRID_TOTAL_A_QLT,));
  __SET_VAR(data__->SUPPLY.,W,,__GET_EXTERNAL(data__->GRID_TOTAL_W,));
  __SET_VAR(data__->SUPPLY.,W_OK,,__GET_EXTERNAL(data__->GRID_TOTAL_W_QLT,));
  __SET_VAR(data__->SUPPLY.,HZ,,__GET_EXTERNAL(data__->GRID_HZ,));
  __SET_VAR(data__->SUPPLY.,HZ_OK,,__GET_EXTERNAL(data__->GRID_HZ_QLT,));
  __SET_VAR(data__->SUPPLY.,LO_V,,__GET_EXTERNAL(data__->PWR_GRIDLO_V,));
  __SET_VAR(data__->SUPPLY.,HI_V,,__GET_EXTERNAL(data__->PWR_GRIDHI_V,));
  __SET_VAR(data__->SUPPLY.,LO_HZ,,__GET_EXTERNAL(data__->PWR_GRIDLO_HZ,));
  __SET_VAR(data__->SUPPLY.,HI_HZ,,__GET_EXTERNAL(data__->PWR_GRIDHI_HZ,));
  AC_SUPPLY_body__(&data__->SUPPLY);
  __SET_EXTERNAL(data__->,SYS_GRID_V,,__GET_VAR(data__->SUPPLY.OUT_V,));
  __SET_EXTERNAL(data__->,SYS_GRID_V_QLT,,__GET_VAR(data__->SUPPLY.OUT_V_OK,));
  __SET_EXTERNAL(data__->,SYS_GRID_A,,__GET_VAR(data__->SUPPLY.OUT_A,));
  __SET_EXTERNAL(data__->,SYS_GRID_A_QLT,,__GET_VAR(data__->SUPPLY.OUT_A_OK,));
  __SET_EXTERNAL(data__->,SYS_GRID_W,,__GET_VAR(data__->SUPPLY.OUT_W,));
  __SET_EXTERNAL(data__->,SYS_GRID_W_QLT,,__GET_VAR(data__->SUPPLY.OUT_W_OK,));
  __SET_EXTERNAL(data__->,SYS_GRID_HZ,,__GET_VAR(data__->SUPPLY.OUT_HZ,));
  __SET_EXTERNAL(data__->,SYS_GRID_HZ_QLT,,__GET_VAR(data__->SUPPLY.OUT_HZ_OK,));
  __SET_EXTERNAL(data__->,SYS_GRID_OK,,__GET_VAR(data__->SUPPLY.OK,));
  __SET_EXTERNAL(data__->,ALARM_AC_GRID,,__GET_VAR(data__->SUPPLY.FAULT,));

  goto __end;

__end:
  return;
} // AC_GRID_WATCH_body__() 





static void AC_LOCAL_WATCH_init__(AC_LOCAL_WATCH_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,ATS_V,data__->ATS_V,retain)
  __INIT_EXTERNAL(BOOL,ATS_V_QLT,data__->ATS_V_QLT,retain)
  __INIT_EXTERNAL(REAL,ATS_A,data__->ATS_A,retain)
  __INIT_EXTERNAL(BOOL,ATS_A_QLT,data__->ATS_A_QLT,retain)
  __INIT_EXTERNAL(REAL,ATS_W,data__->ATS_W,retain)
  __INIT_EXTERNAL(BOOL,ATS_W_QLT,data__->ATS_W_QLT,retain)
  __INIT_EXTERNAL(REAL,ATS_HZ,data__->ATS_HZ,retain)
  __INIT_EXTERNAL(BOOL,ATS_HZ_QLT,data__->ATS_HZ_QLT,retain)
  __INIT_EXTERNAL(REAL,PWR_LOCALLO_V,data__->PWR_LOCALLO_V,retain)
  __INIT_EXTERNAL(REAL,PWR_LOCALHI_V,data__->PWR_LOCALHI_V,retain)
  __INIT_EXTERNAL(REAL,PWR_LOCALLO_HZ,data__->PWR_LOCALLO_HZ,retain)
  __INIT_EXTERNAL(REAL,PWR_LOCALHI_HZ,data__->PWR_LOCALHI_HZ,retain)
  __INIT_EXTERNAL(BOOL,SYS_GRID_OK,data__->SYS_GRID_OK,retain)
  __INIT_EXTERNAL(UINT,PWR_GENSTART_SEC,data__->PWR_GENSTART_SEC,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,SYS_LOCAL_V,data__->SYS_LOCAL_V,retain)
  __INIT_EXTERNAL(BOOL,SYS_LOCAL_V_QLT,data__->SYS_LOCAL_V_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_LOCAL_A,data__->SYS_LOCAL_A,retain)
  __INIT_EXTERNAL(BOOL,SYS_LOCAL_A_QLT,data__->SYS_LOCAL_A_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_LOCAL_W,data__->SYS_LOCAL_W,retain)
  __INIT_EXTERNAL(BOOL,SYS_LOCAL_W_QLT,data__->SYS_LOCAL_W_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_LOCAL_HZ,data__->SYS_LOCAL_HZ,retain)
  __INIT_EXTERNAL(BOOL,SYS_LOCAL_HZ_QLT,data__->SYS_LOCAL_HZ_QLT,retain)
  __INIT_EXTERNAL(BOOL,SYS_LOCAL_OK,data__->SYS_LOCAL_OK,retain)
  __INIT_EXTERNAL(BOOL,ALARM_AC_LOCAL,data__->ALARM_AC_LOCAL,retain)
  __INIT_EXTERNAL(BOOL,ALARM_GEN,data__->ALARM_GEN,retain)
  AC_SUPPLY_init__(&data__->SUPPLY,retain);
  TON_MS_init__(&data__->CRANKING,retain);
}

// Code part
static void AC_LOCAL_WATCH_body__(AC_LOCAL_WATCH_data__ *data__) {
  // Initialise TEMP variables

  __SET_VAR(data__->SUPPLY.,V,,__GET_EXTERNAL(data__->ATS_V,));
  __SET_VAR(data__->SUPPLY.,V_OK,,__GET_EXTERNAL(data__->ATS_V_QLT,));
  __SET_VAR(data__->SUPPLY.,A,,__GET_EXTERNAL(data__->ATS_A,));
  __SET_VAR(data__->SUPPLY.,A_OK,,__GET_EXTERNAL(data__->ATS_A_QLT,));
  __SET_VAR(data__->SUPPLY.,W,,__GET_EXTERNAL(data__->ATS_W,));
  __SET_VAR(data__->SUPPLY.,W_OK,,__GET_EXTERNAL(data__->ATS_W_QLT,));
  __SET_VAR(data__->SUPPLY.,HZ,,__GET_EXTERNAL(data__->ATS_HZ,));
  __SET_VAR(data__->SUPPLY.,HZ_OK,,__GET_EXTERNAL(data__->ATS_HZ_QLT,));
  __SET_VAR(data__->SUPPLY.,LO_V,,__GET_EXTERNAL(data__->PWR_LOCALLO_V,));
  __SET_VAR(data__->SUPPLY.,HI_V,,__GET_EXTERNAL(data__->PWR_LOCALHI_V,));
  __SET_VAR(data__->SUPPLY.,LO_HZ,,__GET_EXTERNAL(data__->PWR_LOCALLO_HZ,));
  __SET_VAR(data__->SUPPLY.,HI_HZ,,__GET_EXTERNAL(data__->PWR_LOCALHI_HZ,));
  AC_SUPPLY_body__(&data__->SUPPLY);
  __SET_EXTERNAL(data__->,SYS_LOCAL_V,,__GET_VAR(data__->SUPPLY.OUT_V,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_V_QLT,,__GET_VAR(data__->SUPPLY.OUT_V_OK,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_A,,__GET_VAR(data__->SUPPLY.OUT_A,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_A_QLT,,__GET_VAR(data__->SUPPLY.OUT_A_OK,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_W,,__GET_VAR(data__->SUPPLY.OUT_W,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_W_QLT,,__GET_VAR(data__->SUPPLY.OUT_W_OK,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_HZ,,__GET_VAR(data__->SUPPLY.OUT_HZ,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_HZ_QLT,,__GET_VAR(data__->SUPPLY.OUT_HZ_OK,));
  __SET_EXTERNAL(data__->,SYS_LOCAL_OK,,__GET_VAR(data__->SUPPLY.OK,));
  __SET_EXTERNAL(data__->,ALARM_AC_LOCAL,,__GET_VAR(data__->SUPPLY.FAULT,));
  __SET_VAR(data__->CRANKING.,IN,,(!(__GET_EXTERNAL(data__->SYS_GRID_OK,)) && !(__GET_VAR(data__->SUPPLY.ANSWERED,))));
  __SET_VAR(data__->CRANKING.,PT,,(___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->PWR_GENSTART_SEC,)) * __UDINT_LITERAL(1000)));
  __SET_VAR(data__->CRANKING.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TON_MS_body__(&data__->CRANKING);
  __SET_EXTERNAL(data__->,ALARM_GEN,,__GET_VAR(data__->CRANKING.Q,));

  goto __end;

__end:
  return;
} // AC_LOCAL_WATCH_body__() 





static void CLIMATE_WATCH_init__(CLIMATE_WATCH_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI,data__->CLI_TEMP_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_DPMARGIN_DEGC,data__->CLI_DPMARGIN_DEGC,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI_QLT,data__->CLI_TEMP_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_DPMARGIN_DEGC_QLT,data__->CLI_DPMARGIN_DEGC_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRAT_DEGC,data__->CLI_DTSTRAT_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_TOOHOT_DEGC,data__->CLI_TOOHOT_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_TOOCOLD_DEGC,data__->CLI_TOOCOLD_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DEHUMFULL_DEGC,data__->CLI_DEHUMFULL_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRATHIGH_DEGC,data__->CLI_DTSTRATHIGH_DEGC,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_CEILING,data__->ALARM_SNS_CEILING,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_FLOOR,data__->ALARM_SNS_FLOOR,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_CRAWLSPACE,data__->ALARM_SNS_CRAWLSPACE,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_EXTERIOR,data__->ALARM_SNS_EXTERIOR,retain)
  __INIT_EXTERNAL(BOOL,ALARM_TOO_HOT,data__->ALARM_TOO_HOT,retain)
  __INIT_EXTERNAL(BOOL,ALARM_TOO_COLD,data__->ALARM_TOO_COLD,retain)
  __INIT_EXTERNAL(BOOL,ALARM_TOO_HUMID,data__->ALARM_TOO_HUMID,retain)
  __INIT_EXTERNAL(BOOL,ALARM_STRAT,data__->ALARM_STRAT,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SENSOR_FAULT,data__->ALARM_SENSOR_FAULT,retain)
}

// Code part
static void CLIMATE_WATCH_body__(CLIMATE_WATCH_data__ *data__) {
  // Initialise TEMP variables

  __SET_EXTERNAL(data__->,ALARM_SENSOR_FAULT,,(((__GET_EXTERNAL(data__->ALARM_SNS_CEILING,) || __GET_EXTERNAL(data__->ALARM_SNS_FLOOR,)) || __GET_EXTERNAL(data__->ALARM_SNS_CRAWLSPACE,)) || __GET_EXTERNAL(data__->ALARM_SNS_EXTERIOR,)));
  if ((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) && __GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC_QLT,))) {
    __SET_EXTERNAL(data__->,ALARM_TOO_HOT,,(__GET_EXTERNAL(data__->CLI_TEMP_HI,) > __GET_EXTERNAL(data__->CLI_TOOHOT_DEGC,)));
    __SET_EXTERNAL(data__->,ALARM_TOO_COLD,,(__GET_EXTERNAL(data__->CLI_TEMP_HI,) < __GET_EXTERNAL(data__->CLI_TOOCOLD_DEGC,)));
    __SET_EXTERNAL(data__->,ALARM_TOO_HUMID,,(__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,) < __GET_EXTERNAL(data__->CLI_DEHUMFULL_DEGC,)));
  } else {
    __SET_EXTERNAL(data__->,ALARM_TOO_HOT,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,ALARM_TOO_COLD,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,ALARM_TOO_HUMID,,__BOOL_LITERAL(FALSE));
  };
  __SET_EXTERNAL(data__->,ALARM_STRAT,,(__GET_EXTERNAL(data__->CLI_DTSTRAT_DEGC,) > __GET_EXTERNAL(data__->CLI_DTSTRATHIGH_DEGC,)));

  goto __end;

__end:
  return;
} // CLIMATE_WATCH_body__() 





static void STRAT_SENSOR_init__(STRAT_SENSOR_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI,data__->CLI_TEMP_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_LO,data__->CLI_TEMP_LO,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI_QLT,data__->CLI_TEMP_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_LO_QLT,data__->CLI_TEMP_LO_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRAT_DEGC,data__->CLI_DTSTRAT_DEGC,retain)
  __INIT_EXTERNAL(BOOL,CLI_DTSTRAT_DEGC_QLT,data__->CLI_DTSTRAT_DEGC_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_DTSTRATLOW_DEGC,data__->CLI_DTSTRATLOW_DEGC,retain)
}

// Code part
static void STRAT_SENSOR_body__(STRAT_SENSOR_data__ *data__) {
  // Initialise TEMP variables

  if ((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) && __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,))) {
    __SET_EXTERNAL(data__->,CLI_DTSTRAT_DEGC,,___ABS(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)(__GET_EXTERNAL(data__->CLI_TEMP_HI,) - __GET_EXTERNAL(data__->CLI_TEMP_LO,))));
  } else {
    __SET_EXTERNAL(data__->,CLI_DTSTRAT_DEGC,,__GET_EXTERNAL(data__->CLI_DTSTRATLOW_DEGC,));
  };
  __SET_EXTERNAL(data__->,CLI_DTSTRAT_DEGC_QLT,,(__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) && __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,)));

  goto __end;

__end:
  return;
} // STRAT_SENSOR_body__() 





static void TEMP_RH_SENSORS_init__(TEMP_RH_SENSORS_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(UINT,CLI_SENSORTAU_MS,data__->CLI_SENSORTAU_MS,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI1,data__->CLI_TEMP_HI1,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI1_QLT,data__->CLI_TEMP_HI1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI2,data__->CLI_TEMP_HI2,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI2_QLT,data__->CLI_TEMP_HI2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI3,data__->CLI_TEMP_HI3,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI3_QLT,data__->CLI_TEMP_HI3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_HI1,data__->CLI_RH_HI1,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_HI1_QLT,data__->CLI_RH_HI1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_HI2,data__->CLI_RH_HI2,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_HI2_QLT,data__->CLI_RH_HI2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_HI3,data__->CLI_RH_HI3,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_HI3_QLT,data__->CLI_RH_HI3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_LO1,data__->CLI_TEMP_LO1,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_LO1_QLT,data__->CLI_TEMP_LO1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_LO2,data__->CLI_TEMP_LO2,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_LO2_QLT,data__->CLI_TEMP_LO2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_LO3,data__->CLI_TEMP_LO3,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_LO3_QLT,data__->CLI_TEMP_LO3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_LO1,data__->CLI_RH_LO1,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_LO1_QLT,data__->CLI_RH_LO1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_LO2,data__->CLI_RH_LO2,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_LO2_QLT,data__->CLI_RH_LO2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_LO3,data__->CLI_RH_LO3,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_LO3_QLT,data__->CLI_RH_LO3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_CS1,data__->CLI_TEMP_CS1,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_CS1_QLT,data__->CLI_TEMP_CS1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_CS2,data__->CLI_TEMP_CS2,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_CS2_QLT,data__->CLI_TEMP_CS2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_CS3,data__->CLI_TEMP_CS3,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_CS3_QLT,data__->CLI_TEMP_CS3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_CS1,data__->CLI_RH_CS1,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_CS1_QLT,data__->CLI_RH_CS1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_CS2,data__->CLI_RH_CS2,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_CS2_QLT,data__->CLI_RH_CS2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_CS3,data__->CLI_RH_CS3,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_CS3_QLT,data__->CLI_RH_CS3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_EXT1,data__->CLI_TEMP_EXT1,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_EXT1_QLT,data__->CLI_TEMP_EXT1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_EXT2,data__->CLI_TEMP_EXT2,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_EXT2_QLT,data__->CLI_TEMP_EXT2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_EXT3,data__->CLI_TEMP_EXT3,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_EXT3_QLT,data__->CLI_TEMP_EXT3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_EXT1,data__->CLI_RH_EXT1,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_EXT1_QLT,data__->CLI_RH_EXT1_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_EXT2,data__->CLI_RH_EXT2,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_EXT2_QLT,data__->CLI_RH_EXT2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_EXT3,data__->CLI_RH_EXT3,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_EXT3_QLT,data__->CLI_RH_EXT3_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI,data__->CLI_TEMP_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_HI,data__->CLI_RH_HI,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_CEILING,data__->ALARM_SNS_CEILING,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_LO,data__->CLI_TEMP_LO,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_LO,data__->CLI_RH_LO,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_FLOOR,data__->ALARM_SNS_FLOOR,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_CS,data__->CLI_TEMP_CS,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_CS,data__->CLI_RH_CS,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_CRAWLSPACE,data__->ALARM_SNS_CRAWLSPACE,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_EXT,data__->CLI_TEMP_EXT,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_EXT,data__->CLI_RH_EXT,retain)
  __INIT_EXTERNAL(BOOL,ALARM_SNS_EXTERIOR,data__->ALARM_SNS_EXTERIOR,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI_QLT,data__->CLI_TEMP_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_LO_QLT,data__->CLI_TEMP_LO_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_CS_QLT,data__->CLI_TEMP_CS_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_EXT_QLT,data__->CLI_TEMP_EXT_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_HI_QLT,data__->CLI_RH_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_LO_QLT,data__->CLI_RH_LO_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_CS_QLT,data__->CLI_RH_CS_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RH_EXT_QLT,data__->CLI_RH_EXT_QLT,retain)
  ZONE_PROBES_init__(&data__->CEILING,retain);
  ZONE_PROBES_init__(&data__->FLOOR,retain);
  ZONE_PROBES_init__(&data__->CRAWLSPACE,retain);
  ZONE_PROBES_init__(&data__->EXTERIOR,retain);
}

// Code part
static void TEMP_RH_SENSORS_body__(TEMP_RH_SENSORS_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,TAU_S,,0);

  __SET_VAR(data__->,TAU_S,,(___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SENSORTAU_MS,)) / __REAL_LITERAL(1000.0)));
  __SET_VAR(data__->CEILING.,T1,,__GET_EXTERNAL(data__->CLI_TEMP_HI1,));
  __SET_VAR(data__->CEILING.,T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_HI1_QLT,));
  __SET_VAR(data__->CEILING.,T2,,__GET_EXTERNAL(data__->CLI_TEMP_HI2,));
  __SET_VAR(data__->CEILING.,T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_HI2_QLT,));
  __SET_VAR(data__->CEILING.,T3,,__GET_EXTERNAL(data__->CLI_TEMP_HI3,));
  __SET_VAR(data__->CEILING.,T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_HI3_QLT,));
  __SET_VAR(data__->CEILING.,H1,,__GET_EXTERNAL(data__->CLI_RH_HI1,));
  __SET_VAR(data__->CEILING.,H1_OK,,__GET_EXTERNAL(data__->CLI_RH_HI1_QLT,));
  __SET_VAR(data__->CEILING.,H2,,__GET_EXTERNAL(data__->CLI_RH_HI2,));
  __SET_VAR(data__->CEILING.,H2_OK,,__GET_EXTERNAL(data__->CLI_RH_HI2_QLT,));
  __SET_VAR(data__->CEILING.,H3,,__GET_EXTERNAL(data__->CLI_RH_HI3,));
  __SET_VAR(data__->CEILING.,H3_OK,,__GET_EXTERNAL(data__->CLI_RH_HI3_QLT,));
  __SET_VAR(data__->CEILING.,TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->CEILING.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->CEILING);
  __SET_EXTERNAL(data__->,CLI_TEMP_HI,,__GET_VAR(data__->CEILING.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_HI,,__GET_VAR(data__->CEILING.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_CEILING,,__GET_VAR(data__->CEILING.FAULT,));
  __SET_VAR(data__->FLOOR.,T1,,__GET_EXTERNAL(data__->CLI_TEMP_LO1,));
  __SET_VAR(data__->FLOOR.,T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_LO1_QLT,));
  __SET_VAR(data__->FLOOR.,T2,,__GET_EXTERNAL(data__->CLI_TEMP_LO2,));
  __SET_VAR(data__->FLOOR.,T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_LO2_QLT,));
  __SET_VAR(data__->FLOOR.,T3,,__GET_EXTERNAL(data__->CLI_TEMP_LO3,));
  __SET_VAR(data__->FLOOR.,T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_LO3_QLT,));
  __SET_VAR(data__->FLOOR.,H1,,__GET_EXTERNAL(data__->CLI_RH_LO1,));
  __SET_VAR(data__->FLOOR.,H1_OK,,__GET_EXTERNAL(data__->CLI_RH_LO1_QLT,));
  __SET_VAR(data__->FLOOR.,H2,,__GET_EXTERNAL(data__->CLI_RH_LO2,));
  __SET_VAR(data__->FLOOR.,H2_OK,,__GET_EXTERNAL(data__->CLI_RH_LO2_QLT,));
  __SET_VAR(data__->FLOOR.,H3,,__GET_EXTERNAL(data__->CLI_RH_LO3,));
  __SET_VAR(data__->FLOOR.,H3_OK,,__GET_EXTERNAL(data__->CLI_RH_LO3_QLT,));
  __SET_VAR(data__->FLOOR.,TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->FLOOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->FLOOR);
  __SET_EXTERNAL(data__->,CLI_TEMP_LO,,__GET_VAR(data__->FLOOR.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_LO,,__GET_VAR(data__->FLOOR.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_FLOOR,,__GET_VAR(data__->FLOOR.FAULT,));
  __SET_VAR(data__->CRAWLSPACE.,T1,,__GET_EXTERNAL(data__->CLI_TEMP_CS1,));
  __SET_VAR(data__->CRAWLSPACE.,T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_CS1_QLT,));
  __SET_VAR(data__->CRAWLSPACE.,T2,,__GET_EXTERNAL(data__->CLI_TEMP_CS2,));
  __SET_VAR(data__->CRAWLSPACE.,T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_CS2_QLT,));
  __SET_VAR(data__->CRAWLSPACE.,T3,,__GET_EXTERNAL(data__->CLI_TEMP_CS3,));
  __SET_VAR(data__->CRAWLSPACE.,T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_CS3_QLT,));
  __SET_VAR(data__->CRAWLSPACE.,H1,,__GET_EXTERNAL(data__->CLI_RH_CS1,));
  __SET_VAR(data__->CRAWLSPACE.,H1_OK,,__GET_EXTERNAL(data__->CLI_RH_CS1_QLT,));
  __SET_VAR(data__->CRAWLSPACE.,H2,,__GET_EXTERNAL(data__->CLI_RH_CS2,));
  __SET_VAR(data__->CRAWLSPACE.,H2_OK,,__GET_EXTERNAL(data__->CLI_RH_CS2_QLT,));
  __SET_VAR(data__->CRAWLSPACE.,H3,,__GET_EXTERNAL(data__->CLI_RH_CS3,));
  __SET_VAR(data__->CRAWLSPACE.,H3_OK,,__GET_EXTERNAL(data__->CLI_RH_CS3_QLT,));
  __SET_VAR(data__->CRAWLSPACE.,TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->CRAWLSPACE.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->CRAWLSPACE);
  __SET_EXTERNAL(data__->,CLI_TEMP_CS,,__GET_VAR(data__->CRAWLSPACE.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_CS,,__GET_VAR(data__->CRAWLSPACE.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_CRAWLSPACE,,__GET_VAR(data__->CRAWLSPACE.FAULT,));
  __SET_VAR(data__->EXTERIOR.,T1,,__GET_EXTERNAL(data__->CLI_TEMP_EXT1,));
  __SET_VAR(data__->EXTERIOR.,T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_EXT1_QLT,));
  __SET_VAR(data__->EXTERIOR.,T2,,__GET_EXTERNAL(data__->CLI_TEMP_EXT2,));
  __SET_VAR(data__->EXTERIOR.,T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_EXT2_QLT,));
  __SET_VAR(data__->EXTERIOR.,T3,,__GET_EXTERNAL(data__->CLI_TEMP_EXT3,));
  __SET_VAR(data__->EXTERIOR.,T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_EXT3_QLT,));
  __SET_VAR(data__->EXTERIOR.,H1,,__GET_EXTERNAL(data__->CLI_RH_EXT1,));
  __SET_VAR(data__->EXTERIOR.,H1_OK,,__GET_EXTERNAL(data__->CLI_RH_EXT1_QLT,));
  __SET_VAR(data__->EXTERIOR.,H2,,__GET_EXTERNAL(data__->CLI_RH_EXT2,));
  __SET_VAR(data__->EXTERIOR.,H2_OK,,__GET_EXTERNAL(data__->CLI_RH_EXT2_QLT,));
  __SET_VAR(data__->EXTERIOR.,H3,,__GET_EXTERNAL(data__->CLI_RH_EXT3,));
  __SET_VAR(data__->EXTERIOR.,H3_OK,,__GET_EXTERNAL(data__->CLI_RH_EXT3_QLT,));
  __SET_VAR(data__->EXTERIOR.,TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->EXTERIOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->EXTERIOR);
  __SET_EXTERNAL(data__->,CLI_TEMP_EXT,,__GET_VAR(data__->EXTERIOR.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_EXT,,__GET_VAR(data__->EXTERIOR.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_EXTERIOR,,__GET_VAR(data__->EXTERIOR.FAULT,));
  __SET_EXTERNAL(data__->,CLI_TEMP_HI_QLT,,__GET_VAR(data__->CEILING.TEMP_OK,));
  __SET_EXTERNAL(data__->,CLI_TEMP_LO_QLT,,__GET_VAR(data__->FLOOR.TEMP_OK,));
  __SET_EXTERNAL(data__->,CLI_TEMP_CS_QLT,,__GET_VAR(data__->CRAWLSPACE.TEMP_OK,));
  __SET_EXTERNAL(data__->,CLI_TEMP_EXT_QLT,,__GET_VAR(data__->EXTERIOR.TEMP_OK,));
  __SET_EXTERNAL(data__->,CLI_RH_HI_QLT,,__GET_VAR(data__->CEILING.RH_OK,));
  __SET_EXTERNAL(data__->,CLI_RH_LO_QLT,,__GET_VAR(data__->FLOOR.RH_OK,));
  __SET_EXTERNAL(data__->,CLI_RH_CS_QLT,,__GET_VAR(data__->CRAWLSPACE.RH_OK,));
  __SET_EXTERNAL(data__->,CLI_RH_EXT_QLT,,__GET_VAR(data__->EXTERIOR.RH_OK,));

  goto __end;

__end:
  return;
} // TEMP_RH_SENSORS_body__() 





static void TEMP_RH_ZONE_init__(TEMP_RH_ZONE_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_TEMP_HI,data__->CLI_TEMP_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_HI,data__->CLI_RH_HI,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_LO,data__->CLI_TEMP_LO,retain)
  __INIT_EXTERNAL(REAL,CLI_RH_LO,data__->CLI_RH_LO,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_HI_QLT,data__->CLI_TEMP_HI_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_LO_QLT,data__->CLI_TEMP_LO_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_ROTMARGIN_DEGC,data__->CLI_ROTMARGIN_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_IN,data__->CLI_TEMP_IN,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_IN_QLT,data__->CLI_TEMP_IN_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_DEWPOINT_DEGC,data__->CLI_DEWPOINT_DEGC,retain)
  __INIT_EXTERNAL(REAL,CLI_DPMARGIN_DEGC,data__->CLI_DPMARGIN_DEGC,retain)
  __INIT_EXTERNAL(BOOL,CLI_DPMARGIN_DEGC_QLT,data__->CLI_DPMARGIN_DEGC_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_SATPRESS_KPA,data__->CLI_SATPRESS_KPA,retain)
  __INIT_EXTERNAL(REAL,CLI_VPD_KPA,data__->CLI_VPD_KPA,retain)
  __INIT_EXTERNAL(BOOL,ALARM_ROT,data__->ALARM_ROT,retain)
}

// Code part
static void TEMP_RH_ZONE_body__(TEMP_RH_ZONE_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,DP_HI,,0);
  __SET_VAR(data__->,DP_LO,,0);
  __SET_VAR(data__->,M_HI,,0);
  __SET_VAR(data__->,M_LO,,0);
  __SET_VAR(data__->,VPD_HI,,0);
  __SET_VAR(data__->,VPD_LO,,0);
  __SET_VAR(data__->,HI_WORST,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,T_WORST,,0);
  __SET_VAR(data__->,SEEN,,__BOOL_LITERAL(FALSE));

  __SET_VAR(data__->,SEEN,,(__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) || __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,)));
  if ((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) && !(__GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,)))) {
    __SET_EXTERNAL(data__->,CLI_TEMP_IN,,__GET_EXTERNAL(data__->CLI_TEMP_HI,));
  } else if ((__GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,) && !(__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,)))) {
    __SET_EXTERNAL(data__->,CLI_TEMP_IN,,__GET_EXTERNAL(data__->CLI_TEMP_LO,));
  } else {
    __SET_EXTERNAL(data__->,CLI_TEMP_IN,,___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_TEMP_HI,),
      (REAL)__GET_EXTERNAL(data__->CLI_TEMP_LO,)));
  };
  __SET_EXTERNAL(data__->,CLI_TEMP_IN_QLT,,__GET_VAR(data__->SEEN,));
  __SET_VAR(data__->,DP_HI,,___DEW_POINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_TEMP_HI,),
    (REAL)__GET_EXTERNAL(data__->CLI_RH_HI,)));
  __SET_VAR(data__->,DP_LO,,___DEW_POINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_TEMP_LO,),
    (REAL)__GET_EXTERNAL(data__->CLI_RH_LO,)));
  __SET_VAR(data__->,M_HI,,(__GET_EXTERNAL(data__->CLI_TEMP_HI,) - __GET_VAR(data__->DP_HI,)));
  __SET_VAR(data__->,M_LO,,(__GET_EXTERNAL(data__->CLI_TEMP_LO,) - __GET_VAR(data__->DP_LO,)));
  if ((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) == __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,))) {
    __SET_VAR(data__->,HI_WORST,,(__GET_VAR(data__->M_HI,) <= __GET_VAR(data__->M_LO,)));
  } else {
    __SET_VAR(data__->,HI_WORST,,__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,));
  };
  if (__GET_VAR(data__->HI_WORST,)) {
    __SET_EXTERNAL(data__->,CLI_DEWPOINT_DEGC,,__GET_VAR(data__->DP_HI,));
    __SET_EXTERNAL(data__->,CLI_DPMARGIN_DEGC,,__GET_VAR(data__->M_HI,));
    __SET_VAR(data__->,T_WORST,,__GET_EXTERNAL(data__->CLI_TEMP_HI,));
  } else {
    __SET_EXTERNAL(data__->,CLI_DEWPOINT_DEGC,,__GET_VAR(data__->DP_LO,));
    __SET_EXTERNAL(data__->,CLI_DPMARGIN_DEGC,,__GET_VAR(data__->M_LO,));
    __SET_VAR(data__->,T_WORST,,__GET_EXTERNAL(data__->CLI_TEMP_LO,));
  };
  __SET_EXTERNAL(data__->,CLI_DPMARGIN_DEGC_QLT,,__GET_VAR(data__->SEEN,));
  __SET_EXTERNAL(data__->,CLI_SATPRESS_KPA,,___SAT_VAPOUR_PRESSURE(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->T_WORST,)));
  __SET_VAR(data__->,VPD_HI,,(___SAT_VAPOUR_PRESSURE(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_TEMP_HI,)) - ___SAT_VAPOUR_PRESSURE(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->DP_HI,))));
  __SET_VAR(data__->,VPD_LO,,(___SAT_VAPOUR_PRESSURE(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_TEMP_LO,)) - ___SAT_VAPOUR_PRESSURE(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->DP_LO,))));
  if ((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) == __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,))) {
    __SET_EXTERNAL(data__->,CLI_VPD_KPA,,___MIN(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->VPD_HI,),
      (REAL)__GET_VAR(data__->VPD_LO,)));
  } else if (__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,)) {
    __SET_EXTERNAL(data__->,CLI_VPD_KPA,,__GET_VAR(data__->VPD_HI,));
  } else {
    __SET_EXTERNAL(data__->,CLI_VPD_KPA,,__GET_VAR(data__->VPD_LO,));
  };
  __SET_EXTERNAL(data__->,ALARM_ROT,,(__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,) < __GET_EXTERNAL(data__->CLI_ROTMARGIN_DEGC,)));

  goto __end;

__end:
  return;
} // TEMP_RH_ZONE_body__() 





static void WEATHER_WATCH_init__(WEATHER_WATCH_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_WIND_MPS,data__->CLI_WIND_MPS,retain)
  __INIT_EXTERNAL(REAL,CLI_RAIN_MMPH,data__->CLI_RAIN_MMPH,retain)
  __INIT_EXTERNAL(BOOL,CLI_WIND_MPS_QLT,data__->CLI_WIND_MPS_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RAIN_MMPH_QLT,data__->CLI_RAIN_MMPH_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDSHUTEND_MPS,data__->CLI_WINDSHUTEND_MPS,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(BOOL,ALARM_WEATHER_STORM,data__->ALARM_WEATHER_STORM,retain)
  __INIT_EXTERNAL(UINT,CLI_SINCERAIN_MIN,data__->CLI_SINCERAIN_MIN,retain)
  __INIT_VAR(data__->SEEN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->DRY_MS,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->PRIMED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void WEATHER_WATCH_body__(WEATHER_WATCH_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,MINS,,0);

  if ((__GET_EXTERNAL(data__->CLI_WIND_MPS_QLT,) && __GET_EXTERNAL(data__->CLI_RAIN_MMPH_QLT,))) {
    __SET_VAR(data__->,SEEN,,__BOOL_LITERAL(TRUE));
  };
  __SET_EXTERNAL(data__->,ALARM_WEATHER_STORM,,(__GET_VAR(data__->SEEN,) && (__GET_EXTERNAL(data__->CLI_WIND_MPS,) >= __GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,))));
  if (__GET_VAR(data__->PRIMED,)) {
    if (__GET_EXTERNAL(data__->CLI_RAIN_MMPH_QLT,)) {
      if ((__GET_EXTERNAL(data__->CLI_RAIN_MMPH,) > __REAL_LITERAL(0.0))) {
        __SET_VAR(data__->,DRY_MS,,__UDINT_LITERAL(0));
      } else {
        __SET_VAR(data__->,DRY_MS,,(__GET_VAR(data__->DRY_MS,) + (__GET_EXTERNAL(data__->SYS_NOW_MS,) - __GET_VAR(data__->LAST,))));
      };
    };
  } else {
    __SET_VAR(data__->,PRIMED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,LAST,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,MINS,,(__GET_VAR(data__->DRY_MS,) / __UDINT_LITERAL(60000)));
  if ((__GET_VAR(data__->MINS,) > __UDINT_LITERAL(65535))) {
    __SET_VAR(data__->,MINS,,__UDINT_LITERAL(65535));
  };
  __SET_EXTERNAL(data__->,CLI_SINCERAIN_MIN,,___UDINT_TO_UINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UDINT)__GET_VAR(data__->MINS,)));

  goto __end;

__end:
  return;
} // WEATHER_WATCH_body__() 





static void WX_SENSORS_init__(WX_SENSORS_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_WIND1_MPS,data__->CLI_WIND1_MPS,retain)
  __INIT_EXTERNAL(BOOL,CLI_WIND1_MPS_QLT,data__->CLI_WIND1_MPS_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WIND2_MPS,data__->CLI_WIND2_MPS,retain)
  __INIT_EXTERNAL(BOOL,CLI_WIND2_MPS_QLT,data__->CLI_WIND2_MPS_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDGUST1_MPS,data__->CLI_WINDGUST1_MPS,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDGUST1_MPS_QLT,data__->CLI_WINDGUST1_MPS_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDGUST2_MPS,data__->CLI_WINDGUST2_MPS,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDGUST2_MPS_QLT,data__->CLI_WINDGUST2_MPS_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RAIN1_MMPH,data__->CLI_RAIN1_MMPH,retain)
  __INIT_EXTERNAL(BOOL,CLI_RAIN1_MMPH_QLT,data__->CLI_RAIN1_MMPH_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_RAIN2_MMPH,data__->CLI_RAIN2_MMPH,retain)
  __INIT_EXTERNAL(BOOL,CLI_RAIN2_MMPH_QLT,data__->CLI_RAIN2_MMPH_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_IRRAD1_WPM2,data__->CLI_IRRAD1_WPM2,retain)
  __INIT_EXTERNAL(BOOL,CLI_IRRAD1_WPM2_QLT,data__->CLI_IRRAD1_WPM2_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_IRRAD2_WPM2,data__->CLI_IRRAD2_WPM2,retain)
  __INIT_EXTERNAL(BOOL,CLI_IRRAD2_WPM2_QLT,data__->CLI_IRRAD2_WPM2_QLT,retain)
  __INIT_EXTERNAL(UINT,CLI_WXTAU_MS,data__->CLI_WXTAU_MS,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,CLI_WIND_MPS,data__->CLI_WIND_MPS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDGUST_MPS,data__->CLI_WINDGUST_MPS,retain)
  __INIT_EXTERNAL(REAL,CLI_RAIN_MMPH,data__->CLI_RAIN_MMPH,retain)
  __INIT_EXTERNAL(REAL,CLI_IRRAD_WPM2,data__->CLI_IRRAD_WPM2,retain)
  __INIT_EXTERNAL(BOOL,CLI_WIND_MPS_QLT,data__->CLI_WIND_MPS_QLT,retain)
  __INIT_EXTERNAL(BOOL,CLI_RAIN_MMPH_QLT,data__->CLI_RAIN_MMPH_QLT,retain)
  __INIT_EXTERNAL(BOOL,ALARM_WX_STATION,data__->ALARM_WX_STATION,retain)
  WX_WORSE_init__(&data__->WIND_VOTE,retain);
  WX_WORSE_init__(&data__->GUST_VOTE,retain);
  WX_WORSE_init__(&data__->RAIN_VOTE,retain);
  WX_MEAN_init__(&data__->SUN_VOTE,retain);
  WX_PUBLISH_init__(&data__->WIND_PUB,retain);
  WX_PUBLISH_init__(&data__->GUST_PUB,retain);
  WX_PUBLISH_init__(&data__->RAIN_PUB,retain);
  WX_PUBLISH_init__(&data__->SUN_PUB,retain);
  TON_MS_init__(&data__->QUORUM_FOR,retain);
}

// Code part
static void WX_SENSORS_body__(WX_SENSORS_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,TAU,,0);
  __SET_VAR(data__->,WORST,,0);

  __SET_VAR(data__->,TAU,,(___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_WXTAU_MS,)) / __REAL_LITERAL(1000.0)));
  __SET_VAR(data__->WIND_VOTE.,A,,__GET_EXTERNAL(data__->CLI_WIND1_MPS,));
  __SET_VAR(data__->WIND_VOTE.,A_OK,,__GET_EXTERNAL(data__->CLI_WIND1_MPS_QLT,));
  __SET_VAR(data__->WIND_VOTE.,B,,__GET_EXTERNAL(data__->CLI_WIND2_MPS,));
  __SET_VAR(data__->WIND_VOTE.,B_OK,,__GET_EXTERNAL(data__->CLI_WIND2_MPS_QLT,));
  WX_WORSE_body__(&data__->WIND_VOTE);
  __SET_VAR(data__->GUST_VOTE.,A,,__GET_EXTERNAL(data__->CLI_WINDGUST1_MPS,));
  __SET_VAR(data__->GUST_VOTE.,A_OK,,__GET_EXTERNAL(data__->CLI_WINDGUST1_MPS_QLT,));
  __SET_VAR(data__->GUST_VOTE.,B,,__GET_EXTERNAL(data__->CLI_WINDGUST2_MPS,));
  __SET_VAR(data__->GUST_VOTE.,B_OK,,__GET_EXTERNAL(data__->CLI_WINDGUST2_MPS_QLT,));
  WX_WORSE_body__(&data__->GUST_VOTE);
  __SET_VAR(data__->RAIN_VOTE.,A,,__GET_EXTERNAL(data__->CLI_RAIN1_MMPH,));
  __SET_VAR(data__->RAIN_VOTE.,A_OK,,__GET_EXTERNAL(data__->CLI_RAIN1_MMPH_QLT,));
  __SET_VAR(data__->RAIN_VOTE.,B,,__GET_EXTERNAL(data__->CLI_RAIN2_MMPH,));
  __SET_VAR(data__->RAIN_VOTE.,B_OK,,__GET_EXTERNAL(data__->CLI_RAIN2_MMPH_QLT,));
  WX_WORSE_body__(&data__->RAIN_VOTE);
  __SET_VAR(data__->SUN_VOTE.,A,,__GET_EXTERNAL(data__->CLI_IRRAD1_WPM2,));
  __SET_VAR(data__->SUN_VOTE.,A_OK,,__GET_EXTERNAL(data__->CLI_IRRAD1_WPM2_QLT,));
  __SET_VAR(data__->SUN_VOTE.,B,,__GET_EXTERNAL(data__->CLI_IRRAD2_WPM2,));
  __SET_VAR(data__->SUN_VOTE.,B_OK,,__GET_EXTERNAL(data__->CLI_IRRAD2_WPM2_QLT,));
  WX_MEAN_body__(&data__->SUN_VOTE);
  __SET_VAR(data__->WIND_PUB.,V,,__GET_VAR(data__->WIND_VOTE.Y,));
  __SET_VAR(data__->WIND_PUB.,USED,,__GET_VAR(data__->WIND_VOTE.USED,));
  __SET_VAR(data__->WIND_PUB.,TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->WIND_PUB.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->WIND_PUB.,LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->WIND_PUB.,HI,,__REAL_LITERAL(80.0));
  WX_PUBLISH_body__(&data__->WIND_PUB);
  __SET_VAR(data__->GUST_PUB.,V,,__GET_VAR(data__->GUST_VOTE.Y,));
  __SET_VAR(data__->GUST_PUB.,USED,,__GET_VAR(data__->GUST_VOTE.USED,));
  __SET_VAR(data__->GUST_PUB.,TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->GUST_PUB.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->GUST_PUB.,LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->GUST_PUB.,HI,,__REAL_LITERAL(80.0));
  WX_PUBLISH_body__(&data__->GUST_PUB);
  __SET_VAR(data__->RAIN_PUB.,V,,__GET_VAR(data__->RAIN_VOTE.Y,));
  __SET_VAR(data__->RAIN_PUB.,USED,,__GET_VAR(data__->RAIN_VOTE.USED,));
  __SET_VAR(data__->RAIN_PUB.,TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->RAIN_PUB.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->RAIN_PUB.,LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->RAIN_PUB.,HI,,__REAL_LITERAL(200.0));
  WX_PUBLISH_body__(&data__->RAIN_PUB);
  __SET_VAR(data__->SUN_PUB.,V,,__GET_VAR(data__->SUN_VOTE.Y,));
  __SET_VAR(data__->SUN_PUB.,USED,,__GET_VAR(data__->SUN_VOTE.USED,));
  __SET_VAR(data__->SUN_PUB.,TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->SUN_PUB.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->SUN_PUB.,LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->SUN_PUB.,HI,,__REAL_LITERAL(1500.0));
  WX_PUBLISH_body__(&data__->SUN_PUB);
  __SET_EXTERNAL(data__->,CLI_WIND_MPS,,__GET_VAR(data__->WIND_PUB.Y,));
  __SET_EXTERNAL(data__->,CLI_WINDGUST_MPS,,__GET_VAR(data__->GUST_PUB.Y,));
  __SET_EXTERNAL(data__->,CLI_RAIN_MMPH,,__GET_VAR(data__->RAIN_PUB.Y,));
  __SET_EXTERNAL(data__->,CLI_IRRAD_WPM2,,__GET_VAR(data__->SUN_PUB.Y,));
  __SET_EXTERNAL(data__->,CLI_WIND_MPS_QLT,,__GET_VAR(data__->WIND_PUB.OK,));
  __SET_EXTERNAL(data__->,CLI_RAIN_MMPH_QLT,,__GET_VAR(data__->RAIN_PUB.OK,));
  __SET_VAR(data__->,WORST,,__GET_VAR(data__->WIND_VOTE.USED,));
  if ((__GET_VAR(data__->GUST_VOTE.USED,) < __GET_VAR(data__->WORST,))) {
    __SET_VAR(data__->,WORST,,__GET_VAR(data__->GUST_VOTE.USED,));
  };
  if ((__GET_VAR(data__->RAIN_VOTE.USED,) < __GET_VAR(data__->WORST,))) {
    __SET_VAR(data__->,WORST,,__GET_VAR(data__->RAIN_VOTE.USED,));
  };
  if ((__GET_VAR(data__->SUN_VOTE.USED,) < __GET_VAR(data__->WORST,))) {
    __SET_VAR(data__->,WORST,,__GET_VAR(data__->SUN_VOTE.USED,));
  };
  __SET_VAR(data__->QUORUM_FOR.,IN,,(__GET_VAR(data__->WORST,) < __USINT_LITERAL(2)));
  __SET_VAR(data__->QUORUM_FOR.,PT,,__UDINT_LITERAL(5000));
  __SET_VAR(data__->QUORUM_FOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TON_MS_body__(&data__->QUORUM_FOR);
  __SET_EXTERNAL(data__->,ALARM_WX_STATION,,__GET_VAR(data__->QUORUM_FOR.Q,));

  goto __end;

__end:
  return;
} // WX_SENSORS_body__() 





