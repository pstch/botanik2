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
static REAL ___POW(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN1, 
  REAL IN2);
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
static INT ___UINT_TO_INT(
  BOOL EN, 
  BOOL *__ENO, 
  UINT IN);
static UINT ___USINT_TO_UINT(
  BOOL EN, 
  BOOL *__ENO, 
  USINT IN);
static UDINT ___USINT_TO_UDINT(
  BOOL EN, 
  BOOL *__ENO, 
  USINT IN);
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
static REAL ___MEDIAN3(
  BOOL EN, 
  BOOL *__ENO, 
  REAL A, 
  REAL B, 
  REAL C);
static REAL ___AVG2(
  BOOL EN, 
  BOOL *__ENO, 
  REAL A, 
  REAL B);
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
static UDINT ___SEC_MS(
  BOOL EN, 
  BOOL *__ENO, 
  UINT S);
static void TOF_MS_init__(TOF_MS_data__ *data__, BOOL retain);
static void TOF_MS_body__(TOF_MS_data__ *data__);
static void TON_MS_init__(TON_MS_data__ *data__, BOOL retain);
static void TON_MS_body__(TON_MS_data__ *data__);
static void EWMA_init__(EWMA_data__ *data__, BOOL retain);
static void EWMA_body__(EWMA_data__ *data__);
static void BAND_LATCH_init__(BAND_LATCH_data__ *data__, BOOL retain);
static void BAND_LATCH_body__(BAND_LATCH_data__ *data__);
static void STAIRCASE3_init__(STAIRCASE3_data__ *data__, BOOL retain);
static void STAIRCASE3_body__(STAIRCASE3_data__ *data__);
static void DEADBAND_init__(DEADBAND_data__ *data__, BOOL retain);
static void DEADBAND_body__(DEADBAND_data__ *data__);
static void DC_RAIL_init__(DC_RAIL_data__ *data__, BOOL retain);
static void DC_RAIL_body__(DC_RAIL_data__ *data__);
static void PID_init__(PID_data__ *data__, BOOL retain);
static void PID_body__(PID_data__ *data__);
static USINT ___ACT_WANT(
  BOOL EN, 
  BOOL *__ENO, 
  USINT MODE, 
  REAL P, 
  REAL TGT, 
  REAL DB);
static void ACTUATOR_init__(ACTUATOR_data__ *data__, BOOL retain);
static void ACTUATOR_body__(ACTUATOR_data__ *data__);
static void INTEGRAL_init__(INTEGRAL_data__ *data__, BOOL retain);
static void INTEGRAL_body__(INTEGRAL_data__ *data__);
static void DERIVATIVE_init__(DERIVATIVE_data__ *data__, BOOL retain);
static void DERIVATIVE_body__(DERIVATIVE_data__ *data__);
static void AC_MIRROR_init__(AC_MIRROR_data__ *data__, BOOL retain);
static void AC_MIRROR_body__(AC_MIRROR_data__ *data__);
static void AC_SUPPLY_init__(AC_SUPPLY_data__ *data__, BOOL retain);
static void AC_SUPPLY_body__(AC_SUPPLY_data__ *data__);
static void TRAY_DUE_init__(TRAY_DUE_data__ *data__, BOOL retain);
static void TRAY_DUE_body__(TRAY_DUE_data__ *data__);
static void VALVE_PULSE_init__(VALVE_PULSE_data__ *data__, BOOL retain);
static void VALVE_PULSE_body__(VALVE_PULSE_data__ *data__);
static void WX_WORSE_init__(WX_WORSE_data__ *data__, BOOL retain);
static void WX_WORSE_body__(WX_WORSE_data__ *data__);
static void WX_MEAN_init__(WX_MEAN_data__ *data__, BOOL retain);
static void WX_MEAN_body__(WX_MEAN_data__ *data__);
static void ZONE_VOTE_init__(ZONE_VOTE_data__ *data__, BOOL retain);
static void ZONE_VOTE_body__(ZONE_VOTE_data__ *data__);
static void ZONE_PROBES_init__(ZONE_PROBES_data__ *data__, BOOL retain);
static void ZONE_PROBES_body__(ZONE_PROBES_data__ *data__);
static void FAN_CONTROLLER_init__(FAN_CONTROLLER_data__ *data__, BOOL retain);
static void FAN_CONTROLLER_body__(FAN_CONTROLLER_data__ *data__);
static void INTRACTOR_CONTROLLER_init__(INTRACTOR_CONTROLLER_data__ *data__, BOOL retain);
static void INTRACTOR_CONTROLLER_body__(INTRACTOR_CONTROLLER_data__ *data__);
static void ROOF_LOCKOUT_init__(ROOF_LOCKOUT_data__ *data__, BOOL retain);
static void ROOF_LOCKOUT_body__(ROOF_LOCKOUT_data__ *data__);
static void SCREEN_CONTROLLER_init__(SCREEN_CONTROLLER_data__ *data__, BOOL retain);
static void SCREEN_CONTROLLER_body__(SCREEN_CONTROLLER_data__ *data__);
static void SKIRT_CONTROLLER_init__(SKIRT_CONTROLLER_data__ *data__, BOOL retain);
static void SKIRT_CONTROLLER_body__(SKIRT_CONTROLLER_data__ *data__);
static void WINDOWS_CONTROLLER_init__(WINDOWS_CONTROLLER_data__ *data__, BOOL retain);
static void WINDOWS_CONTROLLER_body__(WINDOWS_CONTROLLER_data__ *data__);
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
static void IRR_MTR_INPUT_init__(IRR_MTR_INPUT_data__ *data__, BOOL retain);
static void IRR_MTR_INPUT_body__(IRR_MTR_INPUT_data__ *data__);
static void IRR_MTR_OUTPUT_init__(IRR_MTR_OUTPUT_data__ *data__, BOOL retain);
static void IRR_MTR_OUTPUT_body__(IRR_MTR_OUTPUT_data__ *data__);
static void IRR_VALVES_init__(IRR_VALVES_data__ *data__, BOOL retain);
static void IRR_VALVES_body__(IRR_VALVES_data__ *data__);
static void IRRIGATION_CYCLE_init__(IRRIGATION_CYCLE_data__ *data__, BOOL retain);
static void IRRIGATION_CYCLE_body__(IRRIGATION_CYCLE_data__ *data__);
static void IRRIGATION_MODULATION_init__(IRRIGATION_MODULATION_data__ *data__, BOOL retain);
static void IRRIGATION_MODULATION_body__(IRRIGATION_MODULATION_data__ *data__);
static void IRRIGATION_SCHEDULE_init__(IRRIGATION_SCHEDULE_data__ *data__, BOOL retain);
static void IRRIGATION_SCHEDULE_body__(IRRIGATION_SCHEDULE_data__ *data__);
static void AC_GRID_WATCH_init__(AC_GRID_WATCH_data__ *data__, BOOL retain);
static void AC_GRID_WATCH_body__(AC_GRID_WATCH_data__ *data__);
static void AC_LOCAL_WATCH_init__(AC_LOCAL_WATCH_data__ *data__, BOOL retain);
static void AC_LOCAL_WATCH_body__(AC_LOCAL_WATCH_data__ *data__);
static void CLIMATE_WATCH_init__(CLIMATE_WATCH_data__ *data__, BOOL retain);
static void CLIMATE_WATCH_body__(CLIMATE_WATCH_data__ *data__);
static void DC_WATCH_init__(DC_WATCH_data__ *data__, BOOL retain);
static void DC_WATCH_body__(DC_WATCH_data__ *data__);
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
    ABS =  -(IN);
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


static REAL ___POW(
  BOOL EN, 
  BOOL *__ENO, 
  REAL IN1, 
  REAL IN2)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL POW = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return POW;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 POW = powf(IN1, IN2); 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return POW;
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


static INT ___UINT_TO_INT(
  BOOL EN, 
  BOOL *__ENO, 
  UINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  INT UINT_TO_INT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return UINT_TO_INT;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 UINT_TO_INT = (INT)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return UINT_TO_INT;
}


static UINT ___USINT_TO_UINT(
  BOOL EN, 
  BOOL *__ENO, 
  USINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  UINT USINT_TO_UINT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return USINT_TO_UINT;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 USINT_TO_UINT = (UINT)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return USINT_TO_UINT;
}


static UDINT ___USINT_TO_UDINT(
  BOOL EN, 
  BOOL *__ENO, 
  USINT IN)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  UDINT USINT_TO_UDINT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return USINT_TO_UDINT;
  }
  __IL_DEFVAR_T __IL_DEFVAR;
(void)__IL_DEFVAR;
  __IL_DEFVAR_T __IL_DEFVAR_BACK;
(void)__IL_DEFVAR_BACK;
  #define GetFbVar(var,...) __GET_VAR(data__->var,__VA_ARGS__)
  #define SetFbVar(var,val,...) __SET_VAR(data__->,var,__VA_ARGS__,val)
 USINT_TO_UDINT = (UDINT)IN; 
  #undef GetFbVar
  #undef SetFbVar
;

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return USINT_TO_UDINT;
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
    SIGNED_LADDER =  -(___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL) -(V),
      (REAL) -(NEG_START),
      (REAL) -(NEG_FULL)));
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


static REAL ___MEDIAN3(
  BOOL EN, 
  BOOL *__ENO, 
  REAL A, 
  REAL B, 
  REAL C)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL MEDIAN3 = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return MEDIAN3;
  }
  if ((((A <= B) && (B <= C)) || ((C <= B) && (B <= A)))) {
    MEDIAN3 = B;
  } else if ((((B <= A) && (A <= C)) || ((C <= A) && (A <= B)))) {
    MEDIAN3 = A;
  } else {
    MEDIAN3 = C;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return MEDIAN3;
}


static REAL ___AVG2(
  BOOL EN, 
  BOOL *__ENO, 
  REAL A, 
  REAL B)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL AVG2 = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return AVG2;
  }
  AVG2 = (__REAL_LITERAL(0.5) * (A + B));

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return AVG2;
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


static UDINT ___SEC_MS(
  BOOL EN, 
  BOOL *__ENO, 
  UINT S)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  UDINT SEC_MS = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return SEC_MS;
  }
  SEC_MS = (___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)S) * __UDINT_LITERAL(1000));

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return SEC_MS;
}


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
  __INIT_VAR(data__->OK_IN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->TAU_S,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->HI,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->AVG,0,retain)
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

  __SET_VAR(data__->,OK,,__GET_VAR(data__->OK_IN,));
  if (!(__GET_VAR(data__->OK_IN,))) {
    __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
    if (!(__GET_VAR(data__->SEEDED,))) {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->LO,));
    };
    goto __end;
  };
  if (!(__GET_VAR(data__->SEEDED,))) {
    __SET_VAR(data__->,AVG,,__GET_VAR(data__->X,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  } else if ((__GET_VAR(data__->TAU_S,) <= __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,AVG,,__GET_VAR(data__->X,));
  } else {
    __SET_VAR(data__->,DT_S,,(___UDINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UDINT)(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,))) / __REAL_LITERAL(1000.0)));
    __SET_VAR(data__->,ALPHA,,(__GET_VAR(data__->DT_S,) / (__GET_VAR(data__->TAU_S,) + __GET_VAR(data__->DT_S,))));
    __SET_VAR(data__->,AVG,,(__GET_VAR(data__->AVG,) + (__GET_VAR(data__->ALPHA,) * (__GET_VAR(data__->X,) - __GET_VAR(data__->AVG,)))));
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if ((__GET_VAR(data__->HI,) > __GET_VAR(data__->LO,))) {
    __SET_VAR(data__->,Y,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->LO,),
      (REAL)__GET_VAR(data__->AVG,),
      (REAL)__GET_VAR(data__->HI,)));
  } else {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->AVG,));
  };

  goto __end;

__end:
  return;
} // EWMA_body__() 





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
  if ((!(__GET_VAR(data__->SEEDED,)) || (___ABS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->DIFF,)) > __GET_VAR(data__->BAND,)))) {
    __SET_VAR(data__->,HELD,,__GET_VAR(data__->V,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,Y,,__GET_VAR(data__->HELD,));

  goto __end;

__end:
  return;
} // DEADBAND_body__() 





static void DC_RAIL_init__(DC_RAIL_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->V,0,retain)
  __INIT_VAR(data__->V_OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->AVG_MS,0,retain)
  __INIT_VAR(data__->BAND,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->OK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->HEALTHY,__BOOL_LITERAL(FALSE),retain)
  EWMA_init__(&data__->AVG,retain);
  DEADBAND_init__(&data__->DB,retain);
  __INIT_VAR(data__->SEEN,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void DC_RAIL_body__(DC_RAIL_data__ *data__) {
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
  __SET_VAR(data__->,AVG.X,,__GET_VAR(data__->V,));
  __SET_VAR(data__->,AVG.OK_IN,,__GET_VAR(data__->V_OK,));
  __SET_VAR(data__->,AVG.TAU_S,,(___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_VAR(data__->AVG_MS,)) / __REAL_LITERAL(1000.0)));
  __SET_VAR(data__->,AVG.NOW,,__GET_VAR(data__->NOW,));
  __SET_VAR(data__->,AVG.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,AVG.HI,,__REAL_LITERAL(0.0));
  EWMA_body__(&data__->AVG);
  if (__GET_VAR(data__->V_OK,)) {
    __SET_VAR(data__->DB.,V,,__GET_VAR(data__->AVG.Y,));
    __SET_VAR(data__->DB.,BAND,,__GET_VAR(data__->BAND,));
    DEADBAND_body__(&data__->DB);
    __SET_VAR(data__->,SEEN,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,Y,,__GET_VAR(data__->DB.Y,));
  if (__GET_VAR(data__->V_OK,)) {
    __SET_VAR(data__->,HEALTHY,,(__GET_VAR(data__->AVG.Y,) >= __GET_VAR(data__->LO,)));
  } else {
    __SET_VAR(data__->,HEALTHY,,!(__GET_VAR(data__->SEEN,)));
  };

  goto __end;

__end:
  return;
} // DC_RAIL_body__() 





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
  __SET_VAR(data__->,D_RAW,,0);
  __SET_VAR(data__->,ALPHA,,0);
  __SET_VAR(data__->,P_TERM,,0);
  __SET_VAR(data__->,D_TERM,,0);
  __SET_VAR(data__->,TRIAL,,0);
  __SET_VAR(data__->,WINDING,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,UNCLAMPED,,0);
  __SET_VAR(data__->,I_LO,,0);
  __SET_VAR(data__->,I_HI,,0);

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
    __SET_VAR(data__->,D_RAW,,((__GET_VAR(data__->PV,) - __GET_VAR(data__->PREV_PV,)) / __GET_VAR(data__->DT_S,)));
    if ((__GET_VAR(data__->D_TAU_S,) > __REAL_LITERAL(0.0))) {
      __SET_VAR(data__->,ALPHA,,(__GET_VAR(data__->DT_S,) / (__GET_VAR(data__->D_TAU_S,) + __GET_VAR(data__->DT_S,))));
      __SET_VAR(data__->,DERIV,,(__GET_VAR(data__->DERIV,) + (__GET_VAR(data__->ALPHA,) * (__GET_VAR(data__->D_RAW,) - __GET_VAR(data__->DERIV,)))));
    } else {
      __SET_VAR(data__->,DERIV,,__GET_VAR(data__->D_RAW,));
    };
    __SET_VAR(data__->,PREV_PV,,__GET_VAR(data__->PV,));
  };
  __SET_VAR(data__->,P_TERM,,(__GET_VAR(data__->KP,) * __GET_VAR(data__->ERR,)));
  if (__GET_VAR(data__->REVERSE,)) {
    __SET_VAR(data__->,D_TERM,,(__GET_VAR(data__->KD,) * __GET_VAR(data__->DERIV,)));
  } else {
    __SET_VAR(data__->,D_TERM,, -((__GET_VAR(data__->KD,) * __GET_VAR(data__->DERIV,))));
  };
  if ((__GET_VAR(data__->KI,) == __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,INTEGRAL,,__REAL_LITERAL(0.0));
  } else if ((__GET_VAR(data__->DT_S,) > __REAL_LITERAL(0.0))) {
    __SET_VAR(data__->,TRIAL,,((__GET_VAR(data__->P_TERM,) + __GET_VAR(data__->INTEGRAL,)) + __GET_VAR(data__->D_TERM,)));
    __SET_VAR(data__->,WINDING,,(((__GET_VAR(data__->TRIAL,) >= __GET_VAR(data__->OUT_MAX,)) && (__GET_VAR(data__->ERR,) > __REAL_LITERAL(0.0))) || ((__GET_VAR(data__->TRIAL,) <= __GET_VAR(data__->OUT_MIN,)) && (__GET_VAR(data__->ERR,) < __REAL_LITERAL(0.0)))));
    if (!(__GET_VAR(data__->WINDING,))) {
      __SET_VAR(data__->,INTEGRAL,,(__GET_VAR(data__->INTEGRAL,) + ((__GET_VAR(data__->KI,) * __GET_VAR(data__->ERR,)) * __GET_VAR(data__->DT_S,))));
      if ((__GET_VAR(data__->OUT_MIN,) < __REAL_LITERAL(0.0))) {
        __SET_VAR(data__->,I_LO,,__GET_VAR(data__->OUT_MIN,));
      } else {
        __SET_VAR(data__->,I_LO,,__REAL_LITERAL(0.0));
      };
      if ((__GET_VAR(data__->OUT_MAX,) > __REAL_LITERAL(0.0))) {
        __SET_VAR(data__->,I_HI,,__GET_VAR(data__->OUT_MAX,));
      } else {
        __SET_VAR(data__->,I_HI,,__REAL_LITERAL(0.0));
      };
      __SET_VAR(data__->,INTEGRAL,,___LIMIT(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (REAL)__GET_VAR(data__->I_LO,),
        (REAL)__GET_VAR(data__->INTEGRAL,),
        (REAL)__GET_VAR(data__->I_HI,)));
    };
  };
  __SET_VAR(data__->,UNCLAMPED,,((__GET_VAR(data__->P_TERM,) + __GET_VAR(data__->INTEGRAL,)) + __GET_VAR(data__->D_TERM,)));
  __SET_VAR(data__->,SATURATED,,((__GET_VAR(data__->UNCLAMPED,) > __GET_VAR(data__->OUT_MAX,)) || (__GET_VAR(data__->UNCLAMPED,) < __GET_VAR(data__->OUT_MIN,))));
  __SET_VAR(data__->,Y,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->OUT_MIN,),
    (REAL)__GET_VAR(data__->UNCLAMPED,),
    (REAL)__GET_VAR(data__->OUT_MAX,)));

  goto __end;

__end:
  return;
} // PID_body__() 





static USINT ___ACT_WANT(
  BOOL EN, 
  BOOL *__ENO, 
  USINT MODE, 
  REAL P, 
  REAL TGT, 
  REAL DB)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  REAL ERR = 0;
  USINT ACT_WANT = 0;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return ACT_WANT;
  }
  if ((MODE == __USINT_LITERAL(0))) {
    if ((TGT >= (__REAL_LITERAL(1.0) - DB))) {
      ACT_WANT = __USINT_LITERAL(1);
    } else {
      ACT_WANT = __USINT_LITERAL(2);
    };
  } else {
    ERR = (TGT - P);
    if ((ERR > DB)) {
      ACT_WANT = __USINT_LITERAL(1);
    } else if ((ERR <  -(DB))) {
      ACT_WANT = __USINT_LITERAL(2);
    } else {
      ACT_WANT = __USINT_LITERAL(0);
    };
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return ACT_WANT;
}


static void ACTUATOR_init__(ACTUATOR_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->TARGET,0,retain)
  __INIT_VAR(data__->OPEN_MS,0,retain)
  __INIT_VAR(data__->CLOSE_MS,0,retain)
  __INIT_VAR(data__->SEAT_MS,0,retain)
  __INIT_VAR(data__->MIN_RUN_MS,0,retain)
  __INIT_VAR(data__->REVERSE_MS,0,retain)
  __INIT_VAR(data__->DB,0,retain)
  __INIT_VAR(data__->REDATUM,0,retain)
  __INIT_VAR(data__->HOLD,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->PRESET_KNOWN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->PRESET_POS,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->OPEN_CMD,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->CLOSE_CMD,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->POS,0,retain)
  __INIT_VAR(data__->KNOWN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->MOVING,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->STATE,0,retain)
  __INIT_VAR(data__->DRIFT,0,retain)
  __INIT_VAR(data__->P,0,retain)
  __INIT_VAR(data__->RUN_MS,0,retain)
  __INIT_VAR(data__->WAIT_MS,0,retain)
  __INIT_VAR(data__->MODE,0,retain)
  __INIT_VAR(data__->DIR,0,retain)
  __INIT_VAR(data__->SEATING,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->DATUM,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->MOVED,0,retain)
  __INIT_VAR(data__->PENDING,__REAL_LITERAL(-1.0),retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void ACTUATOR_body__(ACTUATOR_data__ *data__) {
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
  __SET_VAR(data__->,TGT,,0);
  __SET_VAR(data__->,RAN,,0);
  __SET_VAR(data__->,SPAN,,0);
  __SET_VAR(data__->,DELTA,,0);
  __SET_VAR(data__->,WANT,,0);
  __SET_VAR(data__->,PARTIAL,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,IS_DATUM,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,TO_END,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,DISTANCE,,0);
  __SET_VAR(data__->,STROKE_MS,,0);
  __SET_VAR(data__->,COMMIT,,__BOOL_LITERAL(FALSE));

  if (__GET_VAR(data__->SEEDED,)) {
    __SET_VAR(data__->,ELAPSED,,(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,)));
  } else {
    __SET_VAR(data__->,ELAPSED,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
    if (__GET_VAR(data__->PRESET_KNOWN,)) {
      __SET_VAR(data__->,P,,___LIMIT(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (REAL)__REAL_LITERAL(0.0),
        (REAL)__GET_VAR(data__->PRESET_POS,),
        (REAL)__REAL_LITERAL(1.0)));
      if ((__GET_VAR(data__->P,) <= __REAL_LITERAL(0.0))) {
        __SET_VAR(data__->,MODE,,__USINT_LITERAL(2));
      } else {
        __SET_VAR(data__->,MODE,,__USINT_LITERAL(4));
      };
    };
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if (!(__GET_VAR(data__->HOLD,))) {
    __SET_VAR(data__->,TGT,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.0),
      (REAL)__GET_VAR(data__->TARGET,),
      (REAL)__REAL_LITERAL(1.0)));
    if ((__GET_VAR(data__->PENDING,) >= __REAL_LITERAL(0.0))) {
      if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(2))) {
        __SET_VAR(data__->,TGT,,__GET_VAR(data__->PENDING,));
        __SET_VAR(data__->,PENDING,,__REAL_LITERAL(-1.0));
      } else {
        __SET_VAR(data__->,TGT,,__REAL_LITERAL(0.0));
      };
    };
    if ((__GET_VAR(data__->RUN_MS,) > __UDINT_LITERAL(0))) {
      if ((__GET_VAR(data__->ELAPSED,) < __GET_VAR(data__->RUN_MS,))) {
        __SET_VAR(data__->,RAN,,__GET_VAR(data__->ELAPSED,));
      } else {
        __SET_VAR(data__->,RAN,,__GET_VAR(data__->RUN_MS,));
      };
      if ((__GET_VAR(data__->DIR,) == __USINT_LITERAL(1))) {
        __SET_VAR(data__->,SPAN,,__GET_VAR(data__->OPEN_MS,));
      } else {
        __SET_VAR(data__->,SPAN,,__GET_VAR(data__->CLOSE_MS,));
      };
      if ((__GET_VAR(data__->SPAN,) > __UDINT_LITERAL(0))) {
        __SET_VAR(data__->,DELTA,,(___UDINT_TO_REAL(
          (BOOL)__BOOL_LITERAL(TRUE),
          NULL,
          (UDINT)__GET_VAR(data__->RAN,)) / ___UDINT_TO_REAL(
          (BOOL)__BOOL_LITERAL(TRUE),
          NULL,
          (UDINT)__GET_VAR(data__->SPAN,))));
      } else {
        __SET_VAR(data__->,DELTA,,__REAL_LITERAL(1.0));
      };
      __SET_VAR(data__->,MOVED,,(__GET_VAR(data__->MOVED,) + __GET_VAR(data__->DELTA,)));
      if ((__GET_VAR(data__->DIR,) == __USINT_LITERAL(1))) {
        __SET_VAR(data__->,P,,___LIMIT(
          (BOOL)__BOOL_LITERAL(TRUE),
          NULL,
          (REAL)__REAL_LITERAL(0.0),
          (REAL)(__GET_VAR(data__->P,) + __GET_VAR(data__->DELTA,)),
          (REAL)__REAL_LITERAL(1.0)));
      } else {
        __SET_VAR(data__->,P,,___LIMIT(
          (BOOL)__BOOL_LITERAL(TRUE),
          NULL,
          (REAL)__REAL_LITERAL(0.0),
          (REAL)(__GET_VAR(data__->P,) - __GET_VAR(data__->DELTA,)),
          (REAL)__REAL_LITERAL(1.0)));
      };
      __SET_VAR(data__->,RUN_MS,,(__GET_VAR(data__->RUN_MS,) - __GET_VAR(data__->RAN,)));
      if ((__GET_VAR(data__->RUN_MS,) == __UDINT_LITERAL(0))) {
        if (__GET_VAR(data__->SEATING,)) {
          if ((__GET_VAR(data__->DIR,) == __USINT_LITERAL(1))) {
            __SET_VAR(data__->,P,,__REAL_LITERAL(1.0));
          } else {
            __SET_VAR(data__->,P,,__REAL_LITERAL(0.0));
          };
          __SET_VAR(data__->,MOVED,,__REAL_LITERAL(0.0));
        };
        __SET_VAR(data__->,DIR,,__USINT_LITERAL(0));
        __SET_VAR(data__->,SEATING,,__BOOL_LITERAL(FALSE));
        __SET_VAR(data__->,DATUM,,__BOOL_LITERAL(FALSE));
        __SET_VAR(data__->,WAIT_MS,,__GET_VAR(data__->REVERSE_MS,));
        if ((__GET_VAR(data__->P,) <= __REAL_LITERAL(0.0))) {
          __SET_VAR(data__->,MODE,,__USINT_LITERAL(2));
        } else {
          __SET_VAR(data__->,MODE,,__USINT_LITERAL(4));
        };
      } else if (!(__GET_VAR(data__->DATUM,))) {
        __SET_VAR(data__->,WANT,,___ACT_WANT(
          (BOOL)__BOOL_LITERAL(TRUE),
          NULL,
          (USINT)__GET_VAR(data__->MODE,),
          (REAL)__GET_VAR(data__->P,),
          (REAL)__GET_VAR(data__->TGT,),
          (REAL)__GET_VAR(data__->DB,)));
        if (((__GET_VAR(data__->WANT,) != __USINT_LITERAL(0)) && (__GET_VAR(data__->WANT,) != __GET_VAR(data__->DIR,)))) {
          __SET_VAR(data__->,RUN_MS,,__UDINT_LITERAL(0));
          __SET_VAR(data__->,SEATING,,__BOOL_LITERAL(FALSE));
          __SET_VAR(data__->,DIR,,__USINT_LITERAL(0));
          __SET_VAR(data__->,WAIT_MS,,__GET_VAR(data__->REVERSE_MS,));
          if ((__GET_VAR(data__->P,) <= __REAL_LITERAL(0.0))) {
            __SET_VAR(data__->,MODE,,__USINT_LITERAL(2));
          } else {
            __SET_VAR(data__->,MODE,,__USINT_LITERAL(4));
          };
        };
      };
    } else if ((__GET_VAR(data__->WAIT_MS,) > __UDINT_LITERAL(0))) {
      if ((__GET_VAR(data__->WAIT_MS,) > __GET_VAR(data__->ELAPSED,))) {
        __SET_VAR(data__->,WAIT_MS,,(__GET_VAR(data__->WAIT_MS,) - __GET_VAR(data__->ELAPSED,)));
      } else {
        __SET_VAR(data__->,WAIT_MS,,__UDINT_LITERAL(0));
      };
    };
    if (((__GET_VAR(data__->RUN_MS,) == __UDINT_LITERAL(0)) && (__GET_VAR(data__->WAIT_MS,) == __UDINT_LITERAL(0)))) {
      __SET_VAR(data__->,WANT,,___ACT_WANT(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (USINT)__GET_VAR(data__->MODE,),
        (REAL)__GET_VAR(data__->P,),
        (REAL)__GET_VAR(data__->TGT,),
        (REAL)__GET_VAR(data__->DB,)));
      if ((__GET_VAR(data__->WANT,) != __USINT_LITERAL(0))) {
        __SET_VAR(data__->,PARTIAL,,((__GET_VAR(data__->TGT,) > __GET_VAR(data__->DB,)) && (__GET_VAR(data__->TGT,) < (__REAL_LITERAL(1.0) - __GET_VAR(data__->DB,)))));
        if ((((((__GET_VAR(data__->PENDING,) < __REAL_LITERAL(0.0)) && (__GET_VAR(data__->REDATUM,) > __REAL_LITERAL(0.0))) && (__GET_VAR(data__->MOVED,) >= __GET_VAR(data__->REDATUM,))) && (__GET_VAR(data__->MODE,) != __USINT_LITERAL(0))) && __GET_VAR(data__->PARTIAL,))) {
          __SET_VAR(data__->,PENDING,,__GET_VAR(data__->TGT,));
          __SET_VAR(data__->,TGT,,__REAL_LITERAL(0.0));
          __SET_VAR(data__->,WANT,,___ACT_WANT(
            (BOOL)__BOOL_LITERAL(TRUE),
            NULL,
            (USINT)__GET_VAR(data__->MODE,),
            (REAL)__GET_VAR(data__->P,),
            (REAL)__GET_VAR(data__->TGT,),
            (REAL)__GET_VAR(data__->DB,)));
          if ((__GET_VAR(data__->WANT,) == __USINT_LITERAL(0))) {
            __SET_VAR(data__->,PENDING,,__REAL_LITERAL(-1.0));
          };
        };
        if ((__GET_VAR(data__->WANT,) != __USINT_LITERAL(0))) {
          __SET_VAR(data__->,IS_DATUM,,(__GET_VAR(data__->MODE,) == __USINT_LITERAL(0)));
          __SET_VAR(data__->,TO_END,,((__GET_VAR(data__->IS_DATUM,) || (__GET_VAR(data__->TGT,) <= __GET_VAR(data__->DB,))) || (__GET_VAR(data__->TGT,) >= (__REAL_LITERAL(1.0) - __GET_VAR(data__->DB,)))));
          if ((__GET_VAR(data__->WANT,) == __USINT_LITERAL(1))) {
            __SET_VAR(data__->,SPAN,,__GET_VAR(data__->OPEN_MS,));
          } else {
            __SET_VAR(data__->,SPAN,,__GET_VAR(data__->CLOSE_MS,));
          };
          if (__GET_VAR(data__->IS_DATUM,)) {
            __SET_VAR(data__->,DISTANCE,,__REAL_LITERAL(1.0));
          } else {
            __SET_VAR(data__->,DISTANCE,,(__GET_VAR(data__->TGT,) - __GET_VAR(data__->P,)));
          };
          __SET_VAR(data__->,DISTANCE,,___ABS(
            (BOOL)__BOOL_LITERAL(TRUE),
            NULL,
            (REAL)__GET_VAR(data__->DISTANCE,)));
          __SET_VAR(data__->,STROKE_MS,,___REAL_TO_UDINT(
            (BOOL)__BOOL_LITERAL(TRUE),
            NULL,
            (REAL)(__GET_VAR(data__->DISTANCE,) * ___UDINT_TO_REAL(
              (BOOL)__BOOL_LITERAL(TRUE),
              NULL,
              (UDINT)__GET_VAR(data__->SPAN,)))));
          __SET_VAR(data__->,COMMIT,,__BOOL_LITERAL(TRUE));
          if (__GET_VAR(data__->TO_END,)) {
            __SET_VAR(data__->,STROKE_MS,,(__GET_VAR(data__->STROKE_MS,) + __GET_VAR(data__->SEAT_MS,)));
          } else if ((__GET_VAR(data__->STROKE_MS,) < __GET_VAR(data__->MIN_RUN_MS,))) {
            __SET_VAR(data__->,COMMIT,,__BOOL_LITERAL(FALSE));
          };
          if (__GET_VAR(data__->COMMIT,)) {
            if ((__GET_VAR(data__->STROKE_MS,) == __UDINT_LITERAL(0))) {
              __SET_VAR(data__->,STROKE_MS,,__UDINT_LITERAL(1));
            };
            __SET_VAR(data__->,DIR,,__GET_VAR(data__->WANT,));
            __SET_VAR(data__->,RUN_MS,,__GET_VAR(data__->STROKE_MS,));
            __SET_VAR(data__->,SEATING,,__GET_VAR(data__->TO_END,));
            __SET_VAR(data__->,DATUM,,__GET_VAR(data__->IS_DATUM,));
            if ((__GET_VAR(data__->WANT,) == __USINT_LITERAL(1))) {
              __SET_VAR(data__->,MODE,,__USINT_LITERAL(3));
            } else {
              __SET_VAR(data__->,MODE,,__USINT_LITERAL(1));
            };
          };
        };
      };
    };
  };
  __SET_VAR(data__->,OPEN_CMD,,((__GET_VAR(data__->DIR,) == __USINT_LITERAL(1)) && (__GET_VAR(data__->RUN_MS,) > __UDINT_LITERAL(0))));
  __SET_VAR(data__->,CLOSE_CMD,,((__GET_VAR(data__->DIR,) == __USINT_LITERAL(2)) && (__GET_VAR(data__->RUN_MS,) > __UDINT_LITERAL(0))));
  __SET_VAR(data__->,MOVING,,(__GET_VAR(data__->OPEN_CMD,) || __GET_VAR(data__->CLOSE_CMD,)));
  __SET_VAR(data__->,POS,,__GET_VAR(data__->P,));
  __SET_VAR(data__->,KNOWN,,(__GET_VAR(data__->MODE,) != __USINT_LITERAL(0)));
  __SET_VAR(data__->,STATE,,__GET_VAR(data__->MODE,));
  __SET_VAR(data__->,DRIFT,,__GET_VAR(data__->MOVED,));

  goto __end;

__end:
  return;
} // ACTUATOR_body__() 





static void INTEGRAL_init__(INTEGRAL_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->RATE_PER_S,0,retain)
  __INIT_VAR(data__->CUTOFF,0,retain)
  __INIT_VAR(data__->HOLD,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->SEED,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->TOTAL,0,retain)
  __INIT_VAR(data__->RATE,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void INTEGRAL_body__(INTEGRAL_data__ *data__) {
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
  __SET_VAR(data__->,R,,0);

  if (__GET_VAR(data__->SEEDED,)) {
    __SET_VAR(data__->,ELAPSED,,(__GET_VAR(data__->NOW,) - __GET_VAR(data__->LAST,)));
  } else {
    __SET_VAR(data__->,ELAPSED,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
    __SET_VAR(data__->,TOTAL,,__GET_VAR(data__->SEED,));
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if (!(__GET_VAR(data__->HOLD,))) {
    __SET_VAR(data__->,R,,__GET_VAR(data__->RATE_PER_S,));
    if (((__GET_VAR(data__->CUTOFF,) > __REAL_LITERAL(0.0)) && (___ABS(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->R,)) < __GET_VAR(data__->CUTOFF,)))) {
      __SET_VAR(data__->,R,,__REAL_LITERAL(0.0));
    };
    __SET_VAR(data__->,RATE,,__GET_VAR(data__->R,));
    __SET_VAR(data__->,TOTAL,,(__GET_VAR(data__->TOTAL,) + (__GET_VAR(data__->R,) * (___UDINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UDINT)__GET_VAR(data__->ELAPSED,)) / __REAL_LITERAL(1000.0)))));
  };

  goto __end;

__end:
  return;
} // INTEGRAL_body__() 





static void DERIVATIVE_init__(DERIVATIVE_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->QUANTITY,0,retain)
  __INIT_VAR(data__->WINDOW_MS,0,retain)
  __INIT_VAR(data__->PRESET,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->RATE,0,retain)
  __INIT_VAR(data__->PREV,0,retain)
  __INIT_VAR(data__->WINDOW,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void DERIVATIVE_body__(DERIVATIVE_data__ *data__) {
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
  };
  __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
  if ((__GET_VAR(data__->PRESET,) || !(__GET_VAR(data__->SEEDED,)))) {
    __SET_VAR(data__->,PREV,,__GET_VAR(data__->QUANTITY,));
    __SET_VAR(data__->,WINDOW,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
    __SET_VAR(data__->,RATE,,__REAL_LITERAL(0.0));
  } else {
    __SET_VAR(data__->,WINDOW,,(__GET_VAR(data__->WINDOW,) + __GET_VAR(data__->ELAPSED,)));
    if (((__GET_VAR(data__->WINDOW,) > __UDINT_LITERAL(0)) && (__GET_VAR(data__->WINDOW,) >= __GET_VAR(data__->WINDOW_MS,)))) {
      __SET_VAR(data__->,RATE,,((__GET_VAR(data__->QUANTITY,) - __GET_VAR(data__->PREV,)) / (___UDINT_TO_REAL(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (UDINT)__GET_VAR(data__->WINDOW,)) / __REAL_LITERAL(1000.0))));
      __SET_VAR(data__->,PREV,,__GET_VAR(data__->QUANTITY,));
      __SET_VAR(data__->,WINDOW,,__UDINT_LITERAL(0));
    };
  };

  goto __end;

__end:
  return;
} // DERIVATIVE_body__() 





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





static void TRAY_DUE_init__(TRAY_DUE_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->DAYS,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->FORCE_OFF,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->DAY,0,retain)
  __INIT_VAR(data__->STRETCH,0,retain)
  __INIT_VAR(data__->DUE,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->OVERDUE,0,retain)
  __INIT_VAR(data__->INTERVAL,0,retain)
  __INIT_VAR(data__->SKIP,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void TRAY_DUE_body__(TRAY_DUE_data__ *data__) {
  // Control execution
  if (!__GET_VAR(data__->EN)) {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(FALSE));
    return;
  }
  else {
    __SET_VAR(data__->,ENO,,__BOOL_LITERAL(TRUE));
  }
  // Initialise TEMP variables
  __SET_VAR(data__->,SUMMER_DAYS,,0);
  __SET_VAR(data__->,DUE_AFTER,,0);
  __SET_VAR(data__->,ELAPSED,,0);

  __SET_VAR(data__->,DUE,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,SKIP,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,OVERDUE,,__UDINT_LITERAL(0));
  __SET_VAR(data__->,INTERVAL,,__REAL_LITERAL(0.0));
  if (((__GET_VAR(data__->DAY,) > __UDINT_LITERAL(0)) && (__GET_VAR(data__->DAYS,) > __UINT_LITERAL(0)))) {
    __SET_VAR(data__->,SUMMER_DAYS,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_VAR(data__->DAYS,)));
    if ((__GET_VAR(data__->STRETCH,) > __REAL_LITERAL(0.0))) {
      __SET_VAR(data__->,INTERVAL,,(__GET_VAR(data__->SUMMER_DAYS,) / __GET_VAR(data__->STRETCH,)));
    } else {
      __SET_VAR(data__->,INTERVAL,,__GET_VAR(data__->SUMMER_DAYS,));
    };
    if ((__GET_VAR(data__->INTERVAL,) < __REAL_LITERAL(1.0))) {
      __SET_VAR(data__->,DUE_AFTER,,__UDINT_LITERAL(1));
    } else {
      __SET_VAR(data__->,DUE_AFTER,,___REAL_TO_UDINT(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (REAL)(__GET_VAR(data__->INTERVAL,) + __REAL_LITERAL(0.5))));
    };
    if (((__GET_VAR(data__->LAST,) == __UDINT_LITERAL(0)) || (__GET_VAR(data__->LAST,) > __GET_VAR(data__->DAY,)))) {
      __SET_VAR(data__->,ELAPSED,,__GET_VAR(data__->DUE_AFTER,));
    } else {
      __SET_VAR(data__->,ELAPSED,,(__GET_VAR(data__->DAY,) - __GET_VAR(data__->LAST,)));
    };
    if ((__GET_VAR(data__->ELAPSED,) >= __GET_VAR(data__->DUE_AFTER,))) {
      if (__GET_VAR(data__->FORCE_OFF,)) {
        __SET_VAR(data__->,SKIP,,__BOOL_LITERAL(TRUE));
      } else {
        __SET_VAR(data__->,DUE,,__BOOL_LITERAL(TRUE));
        __SET_VAR(data__->,OVERDUE,,(__GET_VAR(data__->ELAPSED,) - __GET_VAR(data__->DUE_AFTER,)));
      };
    };
  };

  goto __end;

__end:
  return;
} // TRAY_DUE_body__() 





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
  if ((__GET_VAR(data__->A_OK,) && __GET_VAR(data__->B_OK,))) {
    __SET_VAR(data__->,Y,,___AVG2(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->A,),
      (REAL)__GET_VAR(data__->B,)));
    __SET_VAR(data__->,USED,,__USINT_LITERAL(2));
  } else if (__GET_VAR(data__->A_OK,)) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->A,));
    __SET_VAR(data__->,USED,,__USINT_LITERAL(1));
  } else if (__GET_VAR(data__->B_OK,)) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->B,));
    __SET_VAR(data__->,USED,,__USINT_LITERAL(1));
  };

  goto __end;

__end:
  return;
} // WX_MEAN_body__() 





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
    __SET_VAR(data__->,Y,,___MEDIAN3(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->V1,),
      (REAL)__GET_VAR(data__->V2,),
      (REAL)__GET_VAR(data__->V3,)));
  } else if ((__GET_VAR(data__->N,) == __USINT_LITERAL(2))) {
    __SET_VAR(data__->,Y,,___AVG2(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->V1,),
      (REAL)__GET_VAR(data__->V2,)));
  } else if ((__GET_VAR(data__->N,) == __USINT_LITERAL(1))) {
    __SET_VAR(data__->,Y,,__GET_VAR(data__->V1,));
  } else {
    __SET_VAR(data__->,Y,,__REAL_LITERAL(0.0));
  };

  goto __end;

__end:
  return;
} // ZONE_VOTE_body__() 





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
  EWMA_init__(&data__->T_PUB,retain);
  EWMA_init__(&data__->H_PUB,retain);
  TON_MS_init__(&data__->QUORUM,retain);
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
  __SET_VAR(data__->,T_PUB.X,,__GET_VAR(data__->T_VOTE.Y,));
  __SET_VAR(data__->,T_PUB.OK_IN,,(__GET_VAR(data__->T_VOTE.USED,) > __USINT_LITERAL(0)));
  __SET_VAR(data__->,T_PUB.TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->,T_PUB.NOW,,__GET_VAR(data__->NOW,));
  __SET_VAR(data__->,T_PUB.LO,,__REAL_LITERAL(-100.0));
  __SET_VAR(data__->,T_PUB.HI,,__REAL_LITERAL(200.0));
  EWMA_body__(&data__->T_PUB);
  __SET_VAR(data__->,H_PUB.X,,__GET_VAR(data__->H_RAW,));
  __SET_VAR(data__->,H_PUB.OK_IN,,(__GET_VAR(data__->H_VOTE.USED,) > __USINT_LITERAL(0)));
  __SET_VAR(data__->,H_PUB.TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->,H_PUB.NOW,,__GET_VAR(data__->NOW,));
  __SET_VAR(data__->,H_PUB.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,H_PUB.HI,,__REAL_LITERAL(1.0));
  EWMA_body__(&data__->H_PUB);
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
  TON_MS_body__(&data__->QUORUM);
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
  __SET_VAR(data__->,STEPS.IN,,__GET_VAR(data__->DEMAND,));
  __SET_VAR(data__->,STEPS.LO,,__GET_EXTERNAL(data__->CLI_FANLOWDEM,));
  __SET_VAR(data__->,STEPS.MED,,__GET_EXTERNAL(data__->CLI_FANMEDDEM,));
  __SET_VAR(data__->,STEPS.HI,,__GET_EXTERNAL(data__->CLI_FANHIGHDEM,));
  __SET_VAR(data__->,STEPS.HYST,,__GET_EXTERNAL(data__->CLI_FANDEMHYST,));
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
  __SET_VAR(data__->,PEAK,,0);
  __SET_VAR(data__->,CEILING,,0);

  __SET_VAR(data__->,BLIND,,(!(__GET_EXTERNAL(data__->CLI_RAIN_MMPH_QLT,)) || !(__GET_EXTERNAL(data__->CLI_WIND_MPS_QLT,))));
  __SET_VAR(data__->,RAIN_FOR.IN,,(__GET_EXTERNAL(data__->CLI_RAIN_MMPH,) > __REAL_LITERAL(0.0)));
  __SET_VAR(data__->,RAIN_FOR.PT,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_RAINLOCKOUT_SEC,)));
  __SET_VAR(data__->,RAIN_FOR.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TOF_MS_body__(&data__->RAIN_FOR);
  __SET_EXTERNAL(data__->,CLI_RAINLOCKOUT,,(__GET_VAR(data__->RAIN_FOR.Q,) || __GET_VAR(data__->BLIND,)));
  __SET_VAR(data__->,PEAK,,___MAX(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->CLI_WINDGUST_MPS,),
    (REAL)__GET_EXTERNAL(data__->CLI_WIND_MPS,)));
  __SET_EXTERNAL(data__->,CLI_WINDLOCKOUT,,(__GET_VAR(data__->BLIND,) || (__GET_VAR(data__->PEAK,) >= __GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,))));
  __SET_EXTERNAL(data__->,CLI_WINDOWLOCKOUT,,((__GET_EXTERNAL(data__->CLI_RAINLOCKOUT,) || __GET_EXTERNAL(data__->CLI_WINDLOCKOUT,)) || __GET_EXTERNAL(data__->SYS_SUPPLY_LOCKOUT,)));
  __SET_VAR(data__->,CEILING,,__REAL_LITERAL(1.0));
  if (((__GET_VAR(data__->PEAK,) >= __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,)) && (__GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,) > __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,)))) {
    __SET_VAR(data__->,CEILING,,(__GET_EXTERNAL(data__->CLI_WINDSHUTMAX,) * (__REAL_LITERAL(1.0) - ((__GET_VAR(data__->PEAK,) - __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,)) / (__GET_EXTERNAL(data__->CLI_WINDSHUTEND_MPS,) - __GET_EXTERNAL(data__->CLI_WINDSHUTBEGIN_MPS,))))));
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





static void SCREEN_CONTROLLER_init__(SCREEN_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_COOLDEM,data__->CLI_COOLDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_COOLDEM_QLT,data__->CLI_COOLDEM_QLT,retain)
  __INIT_EXTERNAL(REAL,CLI_THERMALSCREENDEM,data__->CLI_THERMALSCREENDEM,retain)
  __INIT_EXTERNAL(USINT,CLI_PHASE,data__->CLI_PHASE,retain)
  __INIT_EXTERNAL(UINT,CLI_SCREENTRAVEL_SEC,data__->CLI_SCREENTRAVEL_SEC,retain)
  __INIT_EXTERNAL(UINT,CLI_SCREENTMO_SEC,data__->CLI_SCREENTMO_SEC,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(BOOL,CLI_SCREEN,data__->CLI_SCREEN,retain)
  __INIT_EXTERNAL(BOOL,CLI_SCREENMOVING,data__->CLI_SCREENMOVING,retain)
  ACTUATOR_init__(&data__->FABRIC,retain);
}

// Code part
static void SCREEN_CONTROLLER_body__(SCREEN_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,COLD,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,ALLOWS,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,TRAVEL,,0);
  __SET_VAR(data__->,TMO,,0);
  __SET_VAR(data__->,STROKE,,0);
  __SET_VAR(data__->,WANT,,0);

  __SET_VAR(data__->,COLD,,(!(__GET_EXTERNAL(data__->CLI_COOLDEM_QLT,)) || (__GET_EXTERNAL(data__->CLI_COOLDEM,) <= __GET_EXTERNAL(data__->CLI_THERMALSCREENDEM,))));
  if (__GET_VAR(data__->COLD,)) {
    __SET_VAR(data__->,ALLOWS,,(((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(0)) || (__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(5))) || (__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(4))));
  } else {
    __SET_VAR(data__->,ALLOWS,,(((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(3)) || (__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(2))) || (__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(1))));
  };
  if (__GET_VAR(data__->ALLOWS,)) {
    __SET_EXTERNAL(data__->,CLI_SCREEN,,__GET_VAR(data__->COLD,));
  };
  __SET_VAR(data__->,TRAVEL,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SCREENTRAVEL_SEC,)));
  __SET_VAR(data__->,TMO,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SCREENTMO_SEC,)));
  if ((__GET_VAR(data__->TRAVEL,) < __GET_VAR(data__->TMO,))) {
    __SET_VAR(data__->,STROKE,,__GET_VAR(data__->TRAVEL,));
  } else {
    __SET_VAR(data__->,STROKE,,__GET_VAR(data__->TMO,));
  };
  if (__GET_EXTERNAL(data__->CLI_SCREEN,)) {
    __SET_VAR(data__->,WANT,,__REAL_LITERAL(1.0));
  } else {
    __SET_VAR(data__->,WANT,,__REAL_LITERAL(0.0));
  };
  __SET_VAR(data__->,FABRIC.TARGET,,__GET_VAR(data__->WANT,));
  __SET_VAR(data__->,FABRIC.OPEN_MS,,__GET_VAR(data__->STROKE,));
  __SET_VAR(data__->,FABRIC.CLOSE_MS,,__GET_VAR(data__->STROKE,));
  __SET_VAR(data__->,FABRIC.SEAT_MS,,__UDINT_LITERAL(0));
  __SET_VAR(data__->,FABRIC.MIN_RUN_MS,,__UDINT_LITERAL(250));
  __SET_VAR(data__->,FABRIC.REVERSE_MS,,__UDINT_LITERAL(0));
  __SET_VAR(data__->,FABRIC.DB,,__REAL_LITERAL(0.02));
  __SET_VAR(data__->,FABRIC.REDATUM,,__REAL_LITERAL(3.0));
  __SET_VAR(data__->,FABRIC.HOLD,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,FABRIC.PRESET_KNOWN,,__BOOL_LITERAL(TRUE));
  __SET_VAR(data__->,FABRIC.PRESET_POS,,__REAL_LITERAL(1.0));
  __SET_VAR(data__->,FABRIC.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ACTUATOR_body__(&data__->FABRIC);
  __SET_EXTERNAL(data__->,CLI_SCREENMOVING,,__GET_VAR(data__->FABRIC.MOVING,));

  goto __end;

__end:
  return;
} // SCREEN_CONTROLLER_body__() 





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
  __SET_VAR(data__->,WANT,,___MAX(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->HUMID,),
    (REAL)__GET_VAR(data__->COOL,)));
  if (__GET_EXTERNAL(data__->CLI_ROTLOCKOUT,)) {
    __SET_VAR(data__->,WANT,,__REAL_LITERAL(0.0));
  };
  __SET_VAR(data__->,PULSE.DUTY_IN,,__GET_VAR(data__->WANT,));
  __SET_VAR(data__->,PULSE.PERIOD_MS,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SKIRTPERIOD_SEC,)));
  __SET_VAR(data__->,PULSE.MIN_ON_MS,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_SKIRTMINON_SEC,)));
  __SET_VAR(data__->,PULSE.RESET,,__GET_EXTERNAL(data__->CLI_ROTLOCKOUT,));
  __SET_VAR(data__->,PULSE.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
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





static void WINDOWS_CONTROLLER_init__(WINDOWS_CONTROLLER_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_WINDOWDEM,data__->CLI_WINDOWDEM,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOWLOCKOUT,data__->CLI_WINDOWLOCKOUT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOWCAP,data__->CLI_WINDOWCAP,retain)
  __INIT_EXTERNAL(UINT,CLI_WINDOWTRAVEL_SEC,data__->CLI_WINDOWTRAVEL_SEC,retain)
  __INIT_EXTERNAL(UINT,CLI_WINDOWTMO_SEC,data__->CLI_WINDOWTMO_SEC,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW1_OPN,data__->CLI_WINDOW1_OPN,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW2_OPN,data__->CLI_WINDOW2_OPN,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW3_OPN,data__->CLI_WINDOW3_OPN,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW4_OPN,data__->CLI_WINDOW4_OPN,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW1_CLO,data__->CLI_WINDOW1_CLO,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW2_CLO,data__->CLI_WINDOW2_CLO,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW3_CLO,data__->CLI_WINDOW3_CLO,retain)
  __INIT_EXTERNAL(BOOL,CLI_WINDOW4_CLO,data__->CLI_WINDOW4_CLO,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW1_TGT,data__->CLI_WINDOW1_TGT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW2_TGT,data__->CLI_WINDOW2_TGT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW3_TGT,data__->CLI_WINDOW3_TGT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW4_TGT,data__->CLI_WINDOW4_TGT,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW1_POS,data__->CLI_WINDOW1_POS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW2_POS,data__->CLI_WINDOW2_POS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW3_POS,data__->CLI_WINDOW3_POS,retain)
  __INIT_EXTERNAL(REAL,CLI_WINDOW4_POS,data__->CLI_WINDOW4_POS,retain)
  ACTUATOR_init__(&data__->RAM1,retain);
  ACTUATOR_init__(&data__->RAM2,retain);
  ACTUATOR_init__(&data__->RAM3,retain);
  ACTUATOR_init__(&data__->RAM4,retain);
}

// Code part
static void WINDOWS_CONTROLLER_body__(WINDOWS_CONTROLLER_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,CEILING,,0);
  __SET_VAR(data__->,AIM,,0);
  __SET_VAR(data__->,TRAVEL,,0);
  __SET_VAR(data__->,TMO,,0);
  __SET_VAR(data__->,SEAT,,0);
  __SET_VAR(data__->,OWNER,,0);
  __SET_VAR(data__->,TGT1,,0);
  __SET_VAR(data__->,TGT2,,0);
  __SET_VAR(data__->,TGT3,,0);
  __SET_VAR(data__->,TGT4,,0);

  __SET_VAR(data__->,CEILING,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__REAL_LITERAL(0.0),
    (REAL)__GET_EXTERNAL(data__->CLI_WINDOWCAP,),
    (REAL)__REAL_LITERAL(1.0)));
  if (__GET_EXTERNAL(data__->CLI_WINDOWLOCKOUT,)) {
    __SET_VAR(data__->,AIM,,__REAL_LITERAL(0.0));
  } else {
    __SET_VAR(data__->,AIM,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.0),
      (REAL)__GET_EXTERNAL(data__->CLI_WINDOWDEM,),
      (REAL)__REAL_LITERAL(1.0)));
  };
  if ((__GET_VAR(data__->AIM,) > __GET_VAR(data__->CEILING,))) {
    __SET_VAR(data__->,AIM,,__GET_VAR(data__->CEILING,));
  };
  __SET_VAR(data__->,TRAVEL,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_WINDOWTRAVEL_SEC,)));
  __SET_VAR(data__->,TMO,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_WINDOWTMO_SEC,)));
  if ((__GET_VAR(data__->TMO,) > __GET_VAR(data__->TRAVEL,))) {
    __SET_VAR(data__->,SEAT,,(__GET_VAR(data__->TMO,) - __GET_VAR(data__->TRAVEL,)));
  } else {
    __SET_VAR(data__->,SEAT,,__UDINT_LITERAL(2000));
  };
  if (__GET_VAR(data__->RAM1.MOVING,)) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(1));
  } else if (__GET_VAR(data__->RAM2.MOVING,)) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(2));
  } else if (__GET_VAR(data__->RAM3.MOVING,)) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(3));
  } else if (__GET_VAR(data__->RAM4.MOVING,)) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(4));
  } else if ((!(__GET_VAR(data__->RAM1.KNOWN,)) || (___ABS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)(__GET_VAR(data__->AIM,) - __GET_VAR(data__->RAM1.POS,))) > __REAL_LITERAL(0.02)))) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(1));
  } else if ((!(__GET_VAR(data__->RAM2.KNOWN,)) || (___ABS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)(__GET_VAR(data__->AIM,) - __GET_VAR(data__->RAM2.POS,))) > __REAL_LITERAL(0.02)))) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(2));
  } else if ((!(__GET_VAR(data__->RAM3.KNOWN,)) || (___ABS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)(__GET_VAR(data__->AIM,) - __GET_VAR(data__->RAM3.POS,))) > __REAL_LITERAL(0.02)))) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(3));
  } else if ((!(__GET_VAR(data__->RAM4.KNOWN,)) || (___ABS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)(__GET_VAR(data__->AIM,) - __GET_VAR(data__->RAM4.POS,))) > __REAL_LITERAL(0.02)))) {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(4));
  } else {
    __SET_VAR(data__->,OWNER,,__USINT_LITERAL(0));
  };
  __SET_VAR(data__->,TGT1,,__GET_VAR(data__->RAM1.POS,));
  __SET_VAR(data__->,TGT2,,__GET_VAR(data__->RAM2.POS,));
  __SET_VAR(data__->,TGT3,,__GET_VAR(data__->RAM3.POS,));
  __SET_VAR(data__->,TGT4,,__GET_VAR(data__->RAM4.POS,));
  if ((__GET_VAR(data__->OWNER,) == __USINT_LITERAL(1))) {
    __SET_VAR(data__->,TGT1,,__GET_VAR(data__->AIM,));
  };
  if ((__GET_VAR(data__->OWNER,) == __USINT_LITERAL(2))) {
    __SET_VAR(data__->,TGT2,,__GET_VAR(data__->AIM,));
  };
  if ((__GET_VAR(data__->OWNER,) == __USINT_LITERAL(3))) {
    __SET_VAR(data__->,TGT3,,__GET_VAR(data__->AIM,));
  };
  if ((__GET_VAR(data__->OWNER,) == __USINT_LITERAL(4))) {
    __SET_VAR(data__->,TGT4,,__GET_VAR(data__->AIM,));
  };
  __SET_VAR(data__->,RAM1.TARGET,,__GET_VAR(data__->TGT1,));
  __SET_VAR(data__->,RAM1.OPEN_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM1.CLOSE_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM1.SEAT_MS,,__GET_VAR(data__->SEAT,));
  __SET_VAR(data__->,RAM1.MIN_RUN_MS,,__UDINT_LITERAL(250));
  __SET_VAR(data__->,RAM1.REVERSE_MS,,__UDINT_LITERAL(500));
  __SET_VAR(data__->,RAM1.DB,,__REAL_LITERAL(0.02));
  __SET_VAR(data__->,RAM1.REDATUM,,__REAL_LITERAL(3.0));
  __SET_VAR(data__->,RAM1.HOLD,,(__GET_VAR(data__->OWNER,) != __USINT_LITERAL(1)));
  __SET_VAR(data__->,RAM1.PRESET_KNOWN,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,RAM1.PRESET_POS,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,RAM1.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ACTUATOR_body__(&data__->RAM1);
  __SET_VAR(data__->,RAM2.TARGET,,__GET_VAR(data__->TGT2,));
  __SET_VAR(data__->,RAM2.OPEN_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM2.CLOSE_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM2.SEAT_MS,,__GET_VAR(data__->SEAT,));
  __SET_VAR(data__->,RAM2.MIN_RUN_MS,,__UDINT_LITERAL(250));
  __SET_VAR(data__->,RAM2.REVERSE_MS,,__UDINT_LITERAL(500));
  __SET_VAR(data__->,RAM2.DB,,__REAL_LITERAL(0.02));
  __SET_VAR(data__->,RAM2.REDATUM,,__REAL_LITERAL(3.0));
  __SET_VAR(data__->,RAM2.HOLD,,(__GET_VAR(data__->OWNER,) != __USINT_LITERAL(2)));
  __SET_VAR(data__->,RAM2.PRESET_KNOWN,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,RAM2.PRESET_POS,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,RAM2.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ACTUATOR_body__(&data__->RAM2);
  __SET_VAR(data__->,RAM3.TARGET,,__GET_VAR(data__->TGT3,));
  __SET_VAR(data__->,RAM3.OPEN_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM3.CLOSE_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM3.SEAT_MS,,__GET_VAR(data__->SEAT,));
  __SET_VAR(data__->,RAM3.MIN_RUN_MS,,__UDINT_LITERAL(250));
  __SET_VAR(data__->,RAM3.REVERSE_MS,,__UDINT_LITERAL(500));
  __SET_VAR(data__->,RAM3.DB,,__REAL_LITERAL(0.02));
  __SET_VAR(data__->,RAM3.REDATUM,,__REAL_LITERAL(3.0));
  __SET_VAR(data__->,RAM3.HOLD,,(__GET_VAR(data__->OWNER,) != __USINT_LITERAL(3)));
  __SET_VAR(data__->,RAM3.PRESET_KNOWN,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,RAM3.PRESET_POS,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,RAM3.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ACTUATOR_body__(&data__->RAM3);
  __SET_VAR(data__->,RAM4.TARGET,,__GET_VAR(data__->TGT4,));
  __SET_VAR(data__->,RAM4.OPEN_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM4.CLOSE_MS,,__GET_VAR(data__->TRAVEL,));
  __SET_VAR(data__->,RAM4.SEAT_MS,,__GET_VAR(data__->SEAT,));
  __SET_VAR(data__->,RAM4.MIN_RUN_MS,,__UDINT_LITERAL(250));
  __SET_VAR(data__->,RAM4.REVERSE_MS,,__UDINT_LITERAL(500));
  __SET_VAR(data__->,RAM4.DB,,__REAL_LITERAL(0.02));
  __SET_VAR(data__->,RAM4.REDATUM,,__REAL_LITERAL(3.0));
  __SET_VAR(data__->,RAM4.HOLD,,(__GET_VAR(data__->OWNER,) != __USINT_LITERAL(4)));
  __SET_VAR(data__->,RAM4.PRESET_KNOWN,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,RAM4.PRESET_POS,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,RAM4.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ACTUATOR_body__(&data__->RAM4);
  __SET_EXTERNAL(data__->,CLI_WINDOW1_OPN,,__GET_VAR(data__->RAM1.OPEN_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW1_CLO,,__GET_VAR(data__->RAM1.CLOSE_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW2_OPN,,__GET_VAR(data__->RAM2.OPEN_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW2_CLO,,__GET_VAR(data__->RAM2.CLOSE_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW3_OPN,,__GET_VAR(data__->RAM3.OPEN_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW3_CLO,,__GET_VAR(data__->RAM3.CLOSE_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW4_OPN,,__GET_VAR(data__->RAM4.OPEN_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW4_CLO,,__GET_VAR(data__->RAM4.CLOSE_CMD,));
  __SET_EXTERNAL(data__->,CLI_WINDOW1_TGT,,__GET_VAR(data__->AIM,));
  __SET_EXTERNAL(data__->,CLI_WINDOW2_TGT,,__GET_VAR(data__->AIM,));
  __SET_EXTERNAL(data__->,CLI_WINDOW3_TGT,,__GET_VAR(data__->AIM,));
  __SET_EXTERNAL(data__->,CLI_WINDOW4_TGT,,__GET_VAR(data__->AIM,));
  if (__GET_VAR(data__->RAM1.KNOWN,)) {
    __SET_EXTERNAL(data__->,CLI_WINDOW1_POS,,__GET_VAR(data__->RAM1.POS,));
  };
  if (__GET_VAR(data__->RAM2.KNOWN,)) {
    __SET_EXTERNAL(data__->,CLI_WINDOW2_POS,,__GET_VAR(data__->RAM2.POS,));
  };
  if (__GET_VAR(data__->RAM3.KNOWN,)) {
    __SET_EXTERNAL(data__->,CLI_WINDOW3_POS,,__GET_VAR(data__->RAM3.POS,));
  };
  if (__GET_VAR(data__->RAM4.KNOWN,)) {
    __SET_EXTERNAL(data__->,CLI_WINDOW4_POS,,__GET_VAR(data__->RAM4.POS,));
  };

  goto __end;

__end:
  return;
} // WINDOWS_CONTROLLER_body__() 





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
  __SET_VAR(data__->,LOOP.SP,,__GET_EXTERNAL(data__->CLI_TARGETTEMP_DEGC,));
  __SET_VAR(data__->,LOOP.PV,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  __SET_VAR(data__->,LOOP.KP,,__GET_EXTERNAL(data__->CLI_COOLKP,));
  __SET_VAR(data__->,LOOP.KI,,__GET_VAR(data__->KI,));
  __SET_VAR(data__->,LOOP.KD,,(__GET_EXTERNAL(data__->CLI_COOLKP,) * __GET_EXTERNAL(data__->CLI_COOLTD_SEC,)));
  __SET_VAR(data__->,LOOP.OUT_MIN,,__REAL_LITERAL(-1.0));
  __SET_VAR(data__->,LOOP.OUT_MAX,,__REAL_LITERAL(1.0));
  __SET_VAR(data__->,LOOP.D_TAU_S,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,LOOP.REVERSE,,__BOOL_LITERAL(TRUE));
  __SET_VAR(data__->,LOOP.RESET,,!(__GET_VAR(data__->MEASURED,)));
  __SET_VAR(data__->,LOOP.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
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
  __SET_VAR(data__->,PREV_PHASE,,0);

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
  __SET_VAR(data__->,PREV_PHASE,,__GET_VAR(data__->PHASE,));
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
  if (((__GET_VAR(data__->PHASE,) == __USINT_LITERAL(3)) && (__GET_VAR(data__->PREV_PHASE,) != __USINT_LITERAL(3)))) {
    __SET_VAR(data__->,DAY_MAX,,__GET_EXTERNAL(data__->CLI_TEMP_IN,));
  };
  if (((__GET_VAR(data__->PHASE,) == __USINT_LITERAL(0)) && (__GET_VAR(data__->PREV_PHASE,) != __USINT_LITERAL(0)))) {
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

  __SET_VAR(data__->,BANDS.IN,,__GET_EXTERNAL(data__->CLI_DTSTRAT_DEGC,));
  __SET_VAR(data__->,BANDS.LO,,__GET_EXTERNAL(data__->CLI_DTSTRATLOW_DEGC,));
  __SET_VAR(data__->,BANDS.MED,,__GET_EXTERNAL(data__->CLI_DTSTRATMED_DEGC,));
  __SET_VAR(data__->,BANDS.HI,,__GET_EXTERNAL(data__->CLI_DTSTRATHIGH_DEGC,));
  __SET_VAR(data__->,BANDS.HYST,,__GET_EXTERNAL(data__->CLI_DTSTRATHYST_DEGC,));
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
      (REAL) -(__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,)),
      (REAL) -(__GET_EXTERNAL(data__->CLI_ROTMARGIN_DEGC,)),
      (REAL)__REAL_LITERAL(0.0)));
  } else {
    __SET_EXTERNAL(data__->,CLI_ROTVENTDEM,,__REAL_LITERAL(1.0));
  };
  __SET_VAR(data__->,LOCKOUT_FOR.IN,,__GET_EXTERNAL(data__->ALARM_ROT,));
  __SET_VAR(data__->,LOCKOUT_FOR.PT,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_ROTRELEASE_SEC,)));
  __SET_VAR(data__->,LOCKOUT_FOR.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TOF_MS_body__(&data__->LOCKOUT_FOR);
  __SET_EXTERNAL(data__->,CLI_ROTLOCKOUT,,(__GET_VAR(data__->LOCKOUT_FOR.Q,) || !(__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC_QLT,))));

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
  __SET_VAR(data__->,MEASURED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,DP_DIFF,,0);

  __SET_VAR(data__->,MEASURED,,(((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) && __GET_EXTERNAL(data__->CLI_RH_HI_QLT,)) && __GET_EXTERNAL(data__->CLI_TEMP_EXT_QLT,)) && __GET_EXTERNAL(data__->CLI_RH_EXT_QLT,)));
  if (__GET_VAR(data__->MEASURED,)) {
    __SET_VAR(data__->,DP_DIFF,,(___DEW_POINT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_TEMP_HI,),
      (REAL)__GET_EXTERNAL(data__->CLI_RH_HI,)) - ___DEW_POINT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->CLI_TEMP_EXT,),
      (REAL)__GET_EXTERNAL(data__->CLI_RH_EXT,))));
    __SET_VAR(data__->,GATE.IN,,__GET_VAR(data__->DP_DIFF,));
    __SET_VAR(data__->,GATE.RISE,,__GET_EXTERNAL(data__->CLI_VENTDRYMGN_DEGC,));
    __SET_VAR(data__->,GATE.FALL,,(__GET_EXTERNAL(data__->CLI_VENTDRYMGN_DEGC,) - __GET_EXTERNAL(data__->CLI_VENTHYST_DEGC,)));
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
    __SET_VAR(data__->,COOL_GATE.IN,,(__GET_EXTERNAL(data__->CLI_TEMP_IN,) - __GET_EXTERNAL(data__->CLI_TEMP_CS,)));
    __SET_VAR(data__->,COOL_GATE.RISE,,__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,));
    __SET_VAR(data__->,COOL_GATE.FALL,,(__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,) - __GET_EXTERNAL(data__->CLI_VENTHYST_DEGC,)));
    BAND_LATCH_body__(&data__->COOL_GATE);
    __SET_EXTERNAL(data__->,CLI_INTAKECOOLOK,,__GET_VAR(data__->COOL_GATE.Q,));
    __SET_VAR(data__->,GROUND_BAND.IN,, -(__GET_EXTERNAL(data__->CLI_COOLDEM,)));
    __SET_VAR(data__->,GROUND_BAND.RISE,, -(__GET_EXTERNAL(data__->CLI_GROUNDHEATDEM,)));
    __SET_VAR(data__->,GROUND_BAND.FALL,,( -(__GET_EXTERNAL(data__->CLI_GROUNDHEATDEM,)) + __GET_EXTERNAL(data__->CLI_GROUNDDEMHYST,)));
    BAND_LATCH_body__(&data__->GROUND_BAND);
    __SET_VAR(data__->,ASKING,,__GET_VAR(data__->GROUND_BAND.Q,));
    __SET_VAR(data__->,GROUND_GATE.IN,,(__GET_EXTERNAL(data__->CLI_TEMP_CS,) - __GET_EXTERNAL(data__->CLI_TEMP_IN,)));
    __SET_VAR(data__->,GROUND_GATE.RISE,,__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,));
    __SET_VAR(data__->,GROUND_GATE.FALL,,(__GET_EXTERNAL(data__->CLI_VENTTEMPMGN_DEGC,) - __GET_EXTERNAL(data__->CLI_VENTHYST_DEGC,)));
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
  __SET_VAR(data__->,SMOOTH.X,,__GET_VAR(data__->HELD,));
  __SET_VAR(data__->,SMOOTH.OK_IN,,__BOOL_LITERAL(TRUE));
  __SET_VAR(data__->,SMOOTH.TAU_S,,___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->CLI_TARGETTAU_S,)));
  __SET_VAR(data__->,SMOOTH.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,SMOOTH.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,SMOOTH.HI,,__REAL_LITERAL(0.0));
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





static void IRR_MTR_INPUT_init__(IRR_MTR_INPUT_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(UDINT,IRR_INCOUNT,data__->IRR_INCOUNT,retain)
  __INIT_EXTERNAL(REAL,IRR_INPUT_QTYPERIMP,data__->IRR_INPUT_QTYPERIMP,retain)
  __INIT_EXTERNAL(UINT,IRR_RATEWIN_MS,data__->IRR_RATEWIN_MS,retain)
  __INIT_EXTERNAL(REAL,IRR_LEAK_LPM,data__->IRR_LEAK_LPM,retain)
  __INIT_EXTERNAL(REAL,IRR_MINFILL_LPM,data__->IRR_MINFILL_LPM,retain)
  __INIT_EXTERNAL(USINT,IRR_STATE,data__->IRR_STATE,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,IRR_INTOTAL_L,data__->IRR_INTOTAL_L,retain)
  __INIT_EXTERNAL(REAL,IRR_INFLOW_LPM,data__->IRR_INFLOW_LPM,retain)
  __INIT_EXTERNAL(BOOL,IRR_INPULSE,data__->IRR_INPULSE,retain)
  __INIT_EXTERNAL(BOOL,ALARM_WATER_LEAK,data__->ALARM_WATER_LEAK,retain)
  __INIT_EXTERNAL(BOOL,ALARM_WATER_PRESSURE,data__->ALARM_WATER_PRESSURE,retain)
  DERIVATIVE_init__(&data__->RATE,retain);
  TON_MS_init__(&data__->IDLE_FOR,retain);
  TON_MS_init__(&data__->FILL_FOR,retain);
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->BASE_L,0,retain)
  __INIT_VAR(data__->BASE_CNT,0,retain)
  __INIT_VAR(data__->LAST_CNT,0,retain)
}

// Code part
static void IRR_MTR_INPUT_body__(IRR_MTR_INPUT_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,TOTAL,,0);
  __SET_VAR(data__->,FLOW,,0);
  __SET_VAR(data__->,WIN,,0);
  __SET_VAR(data__->,IDLE,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,FILL,,__BOOL_LITERAL(FALSE));

  if (!(__GET_VAR(data__->SEEDED,))) {
    __SET_VAR(data__->,BASE_L,,__GET_EXTERNAL(data__->IRR_INTOTAL_L,));
    __SET_VAR(data__->,BASE_CNT,,__GET_EXTERNAL(data__->IRR_INCOUNT,));
    __SET_VAR(data__->,LAST_CNT,,__GET_EXTERNAL(data__->IRR_INCOUNT,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,TOTAL,,(__GET_VAR(data__->BASE_L,) + (___UDINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UDINT)(__GET_EXTERNAL(data__->IRR_INCOUNT,) - __GET_VAR(data__->BASE_CNT,))) * __GET_EXTERNAL(data__->IRR_INPUT_QTYPERIMP,))));
  __SET_EXTERNAL(data__->,IRR_INPULSE,,(__GET_EXTERNAL(data__->IRR_INCOUNT,) != __GET_VAR(data__->LAST_CNT,)));
  __SET_VAR(data__->,LAST_CNT,,__GET_EXTERNAL(data__->IRR_INCOUNT,));
  __SET_EXTERNAL(data__->,IRR_INTOTAL_L,,__GET_VAR(data__->TOTAL,));
  __SET_VAR(data__->,RATE.QUANTITY,,__GET_VAR(data__->TOTAL,));
  __SET_VAR(data__->,RATE.WINDOW_MS,,___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->IRR_RATEWIN_MS,)));
  __SET_VAR(data__->,RATE.PRESET,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,RATE.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  DERIVATIVE_body__(&data__->RATE);
  __SET_VAR(data__->,FLOW,,(__REAL_LITERAL(60.0) * __GET_VAR(data__->RATE.RATE,)));
  __SET_EXTERNAL(data__->,IRR_INFLOW_LPM,,__GET_VAR(data__->FLOW,));
  __SET_VAR(data__->,WIN,,(__UDINT_LITERAL(2) * ___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->IRR_RATEWIN_MS,))));
  __SET_VAR(data__->IDLE_FOR.,IN,,(__GET_EXTERNAL(data__->IRR_STATE,) == __USINT_LITERAL(0)));
  __SET_VAR(data__->IDLE_FOR.,PT,,__GET_VAR(data__->WIN,));
  __SET_VAR(data__->IDLE_FOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TON_MS_body__(&data__->IDLE_FOR);
  __SET_VAR(data__->FILL_FOR.,IN,,(__GET_EXTERNAL(data__->IRR_STATE,) == __USINT_LITERAL(1)));
  __SET_VAR(data__->FILL_FOR.,PT,,__GET_VAR(data__->WIN,));
  __SET_VAR(data__->FILL_FOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TON_MS_body__(&data__->FILL_FOR);
  __SET_VAR(data__->,IDLE,,__GET_VAR(data__->IDLE_FOR.Q,));
  __SET_VAR(data__->,FILL,,__GET_VAR(data__->FILL_FOR.Q,));
  __SET_EXTERNAL(data__->,ALARM_WATER_LEAK,,(__GET_VAR(data__->IDLE,) && (__GET_VAR(data__->FLOW,) > __GET_EXTERNAL(data__->IRR_LEAK_LPM,))));
  __SET_EXTERNAL(data__->,ALARM_WATER_PRESSURE,,(__GET_VAR(data__->FILL,) && (__GET_VAR(data__->FLOW,) < __GET_EXTERNAL(data__->IRR_MINFILL_LPM,))));

  goto __end;

__end:
  return;
} // IRR_MTR_INPUT_body__() 





static void IRR_MTR_OUTPUT_init__(IRR_MTR_OUTPUT_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,IRR_OUT_LPM,data__->IRR_OUT_LPM,retain)
  __INIT_EXTERNAL(BOOL,IRR_OUT_LPM_QLT,data__->IRR_OUT_LPM_QLT,retain)
  __INIT_EXTERNAL(UINT,IRR_OUT_AVG_MS,data__->IRR_OUT_AVG_MS,retain)
  __INIT_EXTERNAL(REAL,IRR_FLOWBAND_LPM,data__->IRR_FLOWBAND_LPM,retain)
  __INIT_EXTERNAL(REAL,IRR_FLOWCUTOFF_LPM,data__->IRR_FLOWCUTOFF_LPM,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,IRR_OUTFLOW_LPM,data__->IRR_OUTFLOW_LPM,retain)
  __INIT_EXTERNAL(BOOL,IRR_OUTFLOW_LPM_QLT,data__->IRR_OUTFLOW_LPM_QLT,retain)
  __INIT_EXTERNAL(REAL,IRR_OUTTOTAL_L,data__->IRR_OUTTOTAL_L,retain)
  __INIT_EXTERNAL(BOOL,IRR_OUTTOTAL_L_QLT,data__->IRR_OUTTOTAL_L_QLT,retain)
  EWMA_init__(&data__->FLOW_AVG,retain);
  DEADBAND_init__(&data__->FLOW_BAND,retain);
  INTEGRAL_init__(&data__->METER,retain);
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->BASE_L,0,retain)
}

// Code part
static void IRR_MTR_OUTPUT_body__(IRR_MTR_OUTPUT_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,MEASURED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,LPM,,0);

  __SET_VAR(data__->,MEASURED,,__GET_EXTERNAL(data__->IRR_OUT_LPM_QLT,));
  if (!(__GET_VAR(data__->SEEDED,))) {
    __SET_VAR(data__->,BASE_L,,__GET_EXTERNAL(data__->IRR_OUTTOTAL_L,));
    __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,FLOW_AVG.X,,__GET_EXTERNAL(data__->IRR_OUT_LPM,));
  __SET_VAR(data__->,FLOW_AVG.OK_IN,,__GET_VAR(data__->MEASURED,));
  __SET_VAR(data__->,FLOW_AVG.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,FLOW_AVG.TAU_S,,(___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->IRR_OUT_AVG_MS,)) / __REAL_LITERAL(1000.0)));
  __SET_VAR(data__->,FLOW_AVG.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,FLOW_AVG.HI,,__REAL_LITERAL(0.0));
  EWMA_body__(&data__->FLOW_AVG);
  __SET_VAR(data__->,LPM,,__GET_VAR(data__->FLOW_AVG.Y,));
  if (__GET_VAR(data__->MEASURED,)) {
    if ((__GET_VAR(data__->LPM,) < __GET_EXTERNAL(data__->IRR_FLOWBAND_LPM,))) {
      __SET_EXTERNAL(data__->,IRR_OUTFLOW_LPM,,__REAL_LITERAL(0.0));
    } else {
      __SET_VAR(data__->FLOW_BAND.,V,,__GET_VAR(data__->LPM,));
      __SET_VAR(data__->FLOW_BAND.,BAND,,__GET_EXTERNAL(data__->IRR_FLOWBAND_LPM,));
      DEADBAND_body__(&data__->FLOW_BAND);
      __SET_EXTERNAL(data__->,IRR_OUTFLOW_LPM,,__GET_VAR(data__->FLOW_BAND.Y,));
    };
  };
  __SET_EXTERNAL(data__->,IRR_OUTFLOW_LPM_QLT,,__GET_VAR(data__->MEASURED,));
  __SET_VAR(data__->,METER.RATE_PER_S,,(__GET_VAR(data__->LPM,) / __REAL_LITERAL(60.0)));
  __SET_VAR(data__->,METER.CUTOFF,,(__GET_EXTERNAL(data__->IRR_FLOWCUTOFF_LPM,) / __REAL_LITERAL(60.0)));
  __SET_VAR(data__->,METER.HOLD,,!(__GET_VAR(data__->MEASURED,)));
  __SET_VAR(data__->,METER.SEED,,__GET_VAR(data__->BASE_L,));
  __SET_VAR(data__->,METER.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  INTEGRAL_body__(&data__->METER);
  __SET_EXTERNAL(data__->,IRR_OUTTOTAL_L,,__GET_VAR(data__->METER.TOTAL,));
  __SET_EXTERNAL(data__->,IRR_OUTTOTAL_L_QLT,,__GET_VAR(data__->MEASURED,));

  goto __end;

__end:
  return;
} // IRR_MTR_OUTPUT_body__() 





static void IRR_VALVES_init__(IRR_VALVES_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(USINT,IRR_STATE,data__->IRR_STATE,retain)
  __INIT_EXTERNAL(USINT,IRR_ACTIVETRAY,data__->IRR_ACTIVETRAY,retain)
  __INIT_EXTERNAL(BOOL,IRR_INPUT_RO1,data__->IRR_INPUT_RO1,retain)
  __INIT_EXTERNAL(BOOL,IRR_INPUT_RO2,data__->IRR_INPUT_RO2,retain)
  __INIT_EXTERNAL(BOOL,IRR_OUTPUT_RO1,data__->IRR_OUTPUT_RO1,retain)
  __INIT_EXTERNAL(BOOL,IRR_OUTPUT_RO2,data__->IRR_OUTPUT_RO2,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY1,data__->IRR_TRAY1,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY2,data__->IRR_TRAY2,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY3,data__->IRR_TRAY3,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY4,data__->IRR_TRAY4,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY5,data__->IRR_TRAY5,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY6,data__->IRR_TRAY6,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY7,data__->IRR_TRAY7,retain)
  __INIT_EXTERNAL(BOOL,IRR_TRAY8,data__->IRR_TRAY8,retain)
}

// Code part
static void IRR_VALVES_body__(IRR_VALVES_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,FILLING,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,DRAINING,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,FLUSHING,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,SOAKING,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,ONTO,,__BOOL_LITERAL(FALSE));

  __SET_VAR(data__->,FILLING,,(__GET_EXTERNAL(data__->IRR_STATE,) == __USINT_LITERAL(1)));
  __SET_VAR(data__->,DRAINING,,(__GET_EXTERNAL(data__->IRR_STATE,) == __USINT_LITERAL(2)));
  __SET_VAR(data__->,FLUSHING,,(__GET_EXTERNAL(data__->IRR_STATE,) == __USINT_LITERAL(3)));
  __SET_VAR(data__->,SOAKING,,(__GET_EXTERNAL(data__->IRR_STATE,) == __USINT_LITERAL(4)));
  __SET_EXTERNAL(data__->,IRR_INPUT_RO1,,(__GET_VAR(data__->FILLING,) || __GET_VAR(data__->FLUSHING,)));
  __SET_EXTERNAL(data__->,IRR_INPUT_RO2,,(__GET_VAR(data__->FILLING,) || __GET_VAR(data__->FLUSHING,)));
  __SET_EXTERNAL(data__->,IRR_OUTPUT_RO1,,(__GET_VAR(data__->DRAINING,) || __GET_VAR(data__->FLUSHING,)));
  __SET_EXTERNAL(data__->,IRR_OUTPUT_RO2,,(__GET_VAR(data__->DRAINING,) || __GET_VAR(data__->FLUSHING,)));
  __SET_VAR(data__->,ONTO,,((__GET_VAR(data__->FILLING,) || __GET_VAR(data__->SOAKING,)) || __GET_VAR(data__->DRAINING,)));
  __SET_EXTERNAL(data__->,IRR_TRAY1,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(1))));
  __SET_EXTERNAL(data__->,IRR_TRAY2,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(2))));
  __SET_EXTERNAL(data__->,IRR_TRAY3,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(3))));
  __SET_EXTERNAL(data__->,IRR_TRAY4,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(4))));
  __SET_EXTERNAL(data__->,IRR_TRAY5,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(5))));
  __SET_EXTERNAL(data__->,IRR_TRAY6,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(6))));
  __SET_EXTERNAL(data__->,IRR_TRAY7,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(7))));
  __SET_EXTERNAL(data__->,IRR_TRAY8,,(__GET_VAR(data__->ONTO,) && (__GET_EXTERNAL(data__->IRR_ACTIVETRAY,) == __USINT_LITERAL(8))));

  goto __end;

__end:
  return;
} // IRR_VALVES_body__() 





static void IRRIGATION_CYCLE_init__(IRRIGATION_CYCLE_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(USINT,IRR_DUETRAY,data__->IRR_DUETRAY,retain)
  __INIT_EXTERNAL(REAL,IRR_MOD,data__->IRR_MOD,retain)
  __INIT_EXTERNAL(BOOL,IRR_MOD_QLT,data__->IRR_MOD_QLT,retain)
  __INIT_EXTERNAL(REAL,IRR_INTOTAL_L,data__->IRR_INTOTAL_L,retain)
  __INIT_EXTERNAL(REAL,IRR_OUTTOTAL_L,data__->IRR_OUTTOTAL_L,retain)
  __INIT_EXTERNAL(BOOL,IRR_OUTFLOW_LPM_QLT,data__->IRR_OUTFLOW_LPM_QLT,retain)
  __INIT_EXTERNAL(REAL,IRR_VOLFREQSPLIT,data__->IRR_VOLFREQSPLIT,retain)
  __INIT_EXTERNAL(UINT,IRR_MAXFILL_SEC,data__->IRR_MAXFILL_SEC,retain)
  __INIT_EXTERNAL(UINT,IRR_SOAK_SEC,data__->IRR_SOAK_SEC,retain)
  __INIT_EXTERNAL(UINT,IRR_DRAINWINDOW_SEC,data__->IRR_DRAINWINDOW_SEC,retain)
  __INIT_EXTERNAL(UINT,IRR_FLUSH_SEC,data__->IRR_FLUSH_SEC,retain)
  __INIT_EXTERNAL(REAL,IRR_EXPDRAINFRAC,data__->IRR_EXPDRAINFRAC,retain)
  __INIT_EXTERNAL(REAL,IRR_DRAINMARGIN,data__->IRR_DRAINMARGIN,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY1,data__->IRR_LITERS_TRAY1,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY2,data__->IRR_LITERS_TRAY2,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY3,data__->IRR_LITERS_TRAY3,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY4,data__->IRR_LITERS_TRAY4,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY5,data__->IRR_LITERS_TRAY5,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY6,data__->IRR_LITERS_TRAY6,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY7,data__->IRR_LITERS_TRAY7,retain)
  __INIT_EXTERNAL(UINT,IRR_LITERS_TRAY8,data__->IRR_LITERS_TRAY8,retain)
  __INIT_EXTERNAL(REAL,IRR_VOLUME_L,data__->IRR_VOLUME_L,retain)
  __INIT_EXTERNAL(USINT,IRR_STATE,data__->IRR_STATE,retain)
  __INIT_EXTERNAL(USINT,IRR_ACTIVETRAY,data__->IRR_ACTIVETRAY,retain)
  __INIT_EXTERNAL(REAL,IRR_DELIVERED_L,data__->IRR_DELIVERED_L,retain)
  __INIT_EXTERNAL(REAL,IRR_DRAINED_L,data__->IRR_DRAINED_L,retain)
  __INIT_EXTERNAL(UDINT,IRR_TODAY,data__->IRR_TODAY,retain)
  __INIT_EXTERNAL(USINT,IRR_SERVEDTODAY,data__->IRR_SERVEDTODAY,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCENOW,data__->IRR_FORCENOW,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL,data__->ALARM_IRR_FILL,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN,data__->ALARM_IRR_DRAIN,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY1,data__->IRR_LAST_TRAY1,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY2,data__->IRR_LAST_TRAY2,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY3,data__->IRR_LAST_TRAY3,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY4,data__->IRR_LAST_TRAY4,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY5,data__->IRR_LAST_TRAY5,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY6,data__->IRR_LAST_TRAY6,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY7,data__->IRR_LAST_TRAY7,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY8,data__->IRR_LAST_TRAY8,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY1,data__->IRR_FORCEON_TRAY1,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY2,data__->IRR_FORCEON_TRAY2,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY3,data__->IRR_FORCEON_TRAY3,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY4,data__->IRR_FORCEON_TRAY4,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY5,data__->IRR_FORCEON_TRAY5,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY6,data__->IRR_FORCEON_TRAY6,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY7,data__->IRR_FORCEON_TRAY7,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY8,data__->IRR_FORCEON_TRAY8,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY1,data__->ALARM_IRR_FILL_TRAY1,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY2,data__->ALARM_IRR_FILL_TRAY2,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY3,data__->ALARM_IRR_FILL_TRAY3,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY4,data__->ALARM_IRR_FILL_TRAY4,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY5,data__->ALARM_IRR_FILL_TRAY5,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY6,data__->ALARM_IRR_FILL_TRAY6,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY7,data__->ALARM_IRR_FILL_TRAY7,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_FILL_TRAY8,data__->ALARM_IRR_FILL_TRAY8,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY1,data__->ALARM_IRR_DRAIN_TRAY1,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY2,data__->ALARM_IRR_DRAIN_TRAY2,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY3,data__->ALARM_IRR_DRAIN_TRAY3,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY4,data__->ALARM_IRR_DRAIN_TRAY4,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY5,data__->ALARM_IRR_DRAIN_TRAY5,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY6,data__->ALARM_IRR_DRAIN_TRAY6,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY7,data__->ALARM_IRR_DRAIN_TRAY7,retain)
  __INIT_EXTERNAL(BOOL,ALARM_IRR_DRAIN_TRAY8,data__->ALARM_IRR_DRAIN_TRAY8,retain)
  TON_MS_init__(&data__->DWELL,retain);
  __INIT_VAR(data__->TRAY,0,retain)
  __INIT_VAR(data__->MODE,0,retain)
  __INIT_VAR(data__->FORCED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->FILL_AT,0,retain)
  __INIT_VAR(data__->DRAIN_AT,0,retain)
  __INIT_VAR(data__->DOSE_L,0,retain)
}

// Code part
static void IRRIGATION_CYCLE_body__(IRRIGATION_CYCLE_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,SPLIT_LIM,,0);
  __SET_VAR(data__->,DOSE,,0);
  __SET_VAR(data__->,LITRES,,0);
  __SET_VAR(data__->,DELIVERED_L,,0);
  __SET_VAR(data__->,DWELL_MS,,0);
  __SET_VAR(data__->,EXPIRED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,SERVED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,BLAME,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,FRAC,,0);
  __SET_VAR(data__->,MIN_FRAC,,0);
  __SET_VAR(data__->,CLOG,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,JUDGED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,SERVED_TODAY,,0);

  __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,SERVED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,JUDGED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,BLAME,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,CLOG,,__BOOL_LITERAL(FALSE));
  if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(1))) {
    __SET_VAR(data__->,DWELL_MS,,___SEC_MS(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_MAXFILL_SEC,)));
  } else if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(4))) {
    __SET_VAR(data__->,DWELL_MS,,___SEC_MS(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_SOAK_SEC,)));
  } else if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(2))) {
    __SET_VAR(data__->,DWELL_MS,,___SEC_MS(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_DRAINWINDOW_SEC,)));
  } else if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(3))) {
    __SET_VAR(data__->,DWELL_MS,,___SEC_MS(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_FLUSH_SEC,)));
  } else {
    __SET_VAR(data__->,DWELL_MS,,__UDINT_LITERAL(0));
  };
  __SET_VAR(data__->DWELL.,IN,,(__GET_VAR(data__->MODE,) != __USINT_LITERAL(0)));
  __SET_VAR(data__->DWELL.,PT,,__GET_VAR(data__->DWELL_MS,));
  __SET_VAR(data__->DWELL.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TON_MS_body__(&data__->DWELL);
  __SET_VAR(data__->,EXPIRED,,__GET_VAR(data__->DWELL.Q,));
  __SET_VAR(data__->,SPLIT_LIM,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__REAL_LITERAL(0.0),
    (REAL)__GET_EXTERNAL(data__->IRR_VOLFREQSPLIT,),
    (REAL)__REAL_LITERAL(1.0)));
  __SET_VAR(data__->,LITRES,,__REAL_LITERAL(0.0));
  if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(1))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY1,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(2))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY2,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(3))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY3,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(4))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY4,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(5))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY5,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(6))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY6,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(7))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY7,)));
  } else if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(8))) {
    __SET_VAR(data__->,LITRES,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_LITERS_TRAY8,)));
  };
  if ((__GET_VAR(data__->TRAY,) > __USINT_LITERAL(0))) {
    __SET_VAR(data__->,DOSE,,(__GET_VAR(data__->LITRES,) * ___POW(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_EXTERNAL(data__->IRR_MOD,),
      (REAL)__GET_VAR(data__->SPLIT_LIM,))));
  } else {
    __SET_VAR(data__->,DOSE,,__REAL_LITERAL(0.0));
  };
  __SET_EXTERNAL(data__->,IRR_VOLUME_L,,__GET_VAR(data__->DOSE,));
  if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(0))) {
    __SET_EXTERNAL(data__->,IRR_DELIVERED_L,,__REAL_LITERAL(0.0));
    __SET_EXTERNAL(data__->,IRR_DRAINED_L,,__REAL_LITERAL(0.0));
    if ((__GET_EXTERNAL(data__->IRR_DUETRAY,) != __USINT_LITERAL(0))) {
      __SET_VAR(data__->,TRAY,,__GET_EXTERNAL(data__->IRR_DUETRAY,));
      __SET_VAR(data__->,FORCED,,__GET_EXTERNAL(data__->IRR_FORCENOW,));
      __SET_VAR(data__->,MODE,,__USINT_LITERAL(1));
      __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(TRUE));
    };
  } else if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(1))) {
    __SET_EXTERNAL(data__->,IRR_DELIVERED_L,,(__GET_EXTERNAL(data__->IRR_INTOTAL_L,) - __GET_VAR(data__->FILL_AT,)));
    __SET_VAR(data__->,DELIVERED_L,,__GET_EXTERNAL(data__->IRR_DELIVERED_L,));
    if (((__GET_VAR(data__->DELIVERED_L,) >= __GET_VAR(data__->DOSE,)) && (__GET_VAR(data__->DOSE,) > __REAL_LITERAL(0.0)))) {
      __SET_EXTERNAL(data__->,ALARM_IRR_FILL,,__BOOL_LITERAL(FALSE));
      __SET_VAR(data__->,BLAME,,__BOOL_LITERAL(FALSE));
      __SET_VAR(data__->,SERVED,,__BOOL_LITERAL(TRUE));
      __SET_VAR(data__->,DOSE_L,,__GET_VAR(data__->DELIVERED_L,));
      if ((__GET_EXTERNAL(data__->IRR_SOAK_SEC,) > __UINT_LITERAL(0))) {
        __SET_VAR(data__->,MODE,,__USINT_LITERAL(4));
      } else {
        __SET_VAR(data__->,MODE,,__USINT_LITERAL(2));
      };
      __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_VAR(data__->EXPIRED,)) {
      __SET_EXTERNAL(data__->,ALARM_IRR_FILL,,__BOOL_LITERAL(TRUE));
      __SET_VAR(data__->,BLAME,,__BOOL_LITERAL(TRUE));
      __SET_VAR(data__->,SERVED,,__BOOL_LITERAL(TRUE));
      __SET_VAR(data__->,DOSE_L,,__GET_VAR(data__->DELIVERED_L,));
      __SET_VAR(data__->,MODE,,__USINT_LITERAL(2));
      __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(TRUE));
    };
  } else if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(4))) {
    if (__GET_VAR(data__->EXPIRED,)) {
      __SET_VAR(data__->,MODE,,__USINT_LITERAL(2));
      __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(TRUE));
    };
  } else if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(2))) {
    __SET_EXTERNAL(data__->,IRR_DRAINED_L,,(__GET_EXTERNAL(data__->IRR_OUTTOTAL_L,) - __GET_VAR(data__->DRAIN_AT,)));
    if (__GET_VAR(data__->EXPIRED,)) {
      __SET_VAR(data__->,JUDGED,,__BOOL_LITERAL(TRUE));
      if (((__GET_VAR(data__->DOSE_L,) > __REAL_LITERAL(0.0)) && __GET_EXTERNAL(data__->IRR_OUTFLOW_LPM_QLT,))) {
        __SET_VAR(data__->,FRAC,,(__GET_EXTERNAL(data__->IRR_DRAINED_L,) / __GET_VAR(data__->DOSE_L,)));
        __SET_VAR(data__->,MIN_FRAC,,(__GET_EXTERNAL(data__->IRR_EXPDRAINFRAC,) - __GET_EXTERNAL(data__->IRR_DRAINMARGIN,)));
        __SET_VAR(data__->,CLOG,,(__GET_VAR(data__->FRAC,) < __GET_VAR(data__->MIN_FRAC,)));
      };
      __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN,,__GET_VAR(data__->CLOG,));
    };
  } else {
    if (__GET_VAR(data__->EXPIRED,)) {
      __SET_VAR(data__->,MODE,,__USINT_LITERAL(0));
      __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(TRUE));
    };
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(1)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY1,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(2)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY2,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(3)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY3,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(4)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY4,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(5)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY5,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(6)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY6,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(7)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY7,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->SERVED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(8)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_FILL_TRAY8,,__GET_VAR(data__->BLAME,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(1)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY1,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(2)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY2,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(3)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY3,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(4)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY4,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(5)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY5,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(6)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY6,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(7)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY7,,__GET_VAR(data__->CLOG,));
  };
  if ((__GET_VAR(data__->JUDGED,) && (__GET_VAR(data__->TRAY,) == __USINT_LITERAL(8)))) {
    __SET_EXTERNAL(data__->,ALARM_IRR_DRAIN_TRAY8,,__GET_VAR(data__->CLOG,));
  };
  if (__GET_VAR(data__->SERVED,)) {
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(1))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY1,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY1,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(2))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY2,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY2,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(3))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY3,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY3,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(4))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY4,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY4,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(5))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY5,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY5,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(6))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY6,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY6,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(7))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY7,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY7,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if ((__GET_VAR(data__->TRAY,) == __USINT_LITERAL(8))) {
      __SET_EXTERNAL(data__->,IRR_FORCEON_TRAY8,,__BOOL_LITERAL(FALSE));
      if ((__GET_EXTERNAL(data__->IRR_TODAY,) != __UDINT_LITERAL(0))) {
        __SET_EXTERNAL(data__->,IRR_LAST_TRAY8,,__GET_EXTERNAL(data__->IRR_TODAY,));
      };
    };
    if (__GET_VAR(data__->FORCED,)) {
      __SET_EXTERNAL(data__->,IRR_FORCENOW,,__BOOL_LITERAL(FALSE));
      __SET_VAR(data__->,FORCED,,__BOOL_LITERAL(FALSE));
    };
    __SET_VAR(data__->,SERVED_TODAY,,__GET_EXTERNAL(data__->IRR_SERVEDTODAY,));
    if ((__GET_VAR(data__->SERVED_TODAY,) < __USINT_LITERAL(255))) {
      __SET_EXTERNAL(data__->,IRR_SERVEDTODAY,,(__GET_VAR(data__->SERVED_TODAY,) + __USINT_LITERAL(1)));
    };
  };
  if (__GET_VAR(data__->JUDGED,)) {
    __SET_VAR(data__->,TRAY,,__USINT_LITERAL(0));
    __SET_VAR(data__->,MODE,,__USINT_LITERAL(3));
    __SET_VAR(data__->,MOVED,,__BOOL_LITERAL(TRUE));
  };
  if (__GET_VAR(data__->MOVED,)) {
    if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(1))) {
      __SET_VAR(data__->,FILL_AT,,__GET_EXTERNAL(data__->IRR_INTOTAL_L,));
    };
    if ((__GET_VAR(data__->MODE,) == __USINT_LITERAL(2))) {
      __SET_VAR(data__->,DRAIN_AT,,__GET_EXTERNAL(data__->IRR_OUTTOTAL_L,));
      __SET_EXTERNAL(data__->,IRR_DRAINED_L,,__REAL_LITERAL(0.0));
    };
    __SET_VAR(data__->DWELL.,IN,,__BOOL_LITERAL(FALSE));
    __SET_VAR(data__->DWELL.,PT,,__GET_VAR(data__->DWELL_MS,));
    __SET_VAR(data__->DWELL.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
    TON_MS_body__(&data__->DWELL);
  };
  __SET_EXTERNAL(data__->,IRR_STATE,,__GET_VAR(data__->MODE,));
  __SET_EXTERNAL(data__->,IRR_ACTIVETRAY,,__GET_VAR(data__->TRAY,));

  goto __end;

__end:
  return;
} // IRRIGATION_CYCLE_body__() 





static void IRRIGATION_MODULATION_init__(IRRIGATION_MODULATION_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,CLI_VPD_KPA,data__->CLI_VPD_KPA,retain)
  __INIT_EXTERNAL(BOOL,CLI_DPMARGIN_DEGC_QLT,data__->CLI_DPMARGIN_DEGC_QLT,retain)
  __INIT_EXTERNAL(UINT,IRR_DRYEWMADAYS,data__->IRR_DRYEWMADAYS,retain)
  __INIT_EXTERNAL(REAL,IRR_DRYLO_KPA,data__->IRR_DRYLO_KPA,retain)
  __INIT_EXTERNAL(REAL,IRR_DRYHI_KPA,data__->IRR_DRYHI_KPA,retain)
  __INIT_EXTERNAL(REAL,IRR_MODMIN,data__->IRR_MODMIN,retain)
  __INIT_EXTERNAL(REAL,IRR_MODGAMMA,data__->IRR_MODGAMMA,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,IRR_VPDAVG_KPA,data__->IRR_VPDAVG_KPA,retain)
  __INIT_EXTERNAL(REAL,IRR_MOD,data__->IRR_MOD,retain)
  __INIT_EXTERNAL(BOOL,IRR_MOD_QLT,data__->IRR_MOD_QLT,retain)
  __INIT_VAR(data__->AVG,0,retain)
  __INIT_VAR(data__->FOLDED_AT,0,retain)
  __INIT_VAR(data__->STARTED,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->SEEDED,__BOOL_LITERAL(FALSE),retain)
}

// Code part
static void IRRIGATION_MODULATION_body__(IRRIGATION_MODULATION_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,MEASURED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,DUE,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,ALPHA,,0);
  __SET_VAR(data__->,DEMAND,,0);
  __SET_VAR(data__->,MIN_MOD,,0);
  __SET_VAR(data__->,GAMMA,,0);

  __SET_VAR(data__->,MEASURED,,__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC_QLT,));
  if (!(__GET_VAR(data__->STARTED,))) {
    __SET_VAR(data__->,FOLDED_AT,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
    __SET_VAR(data__->,STARTED,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,DUE,,(((__GET_EXTERNAL(data__->SYS_NOW_MS,) - __GET_VAR(data__->FOLDED_AT,)) >= __UDINT_LITERAL(3600000)) || !(__GET_VAR(data__->SEEDED,))));
  if ((__GET_VAR(data__->DUE,) && __GET_VAR(data__->MEASURED,))) {
    if ((__GET_EXTERNAL(data__->IRR_DRYEWMADAYS,) == __UINT_LITERAL(0))) {
      __SET_VAR(data__->,ALPHA,,__REAL_LITERAL(1.0));
    } else {
      __SET_VAR(data__->,ALPHA,,(__REAL_LITERAL(1.0) / (__REAL_LITERAL(24.0) * ___UINT_TO_REAL(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (UINT)__GET_EXTERNAL(data__->IRR_DRYEWMADAYS,)))));
    };
    if (__GET_VAR(data__->SEEDED,)) {
      __SET_VAR(data__->,AVG,,(__GET_VAR(data__->AVG,) + (__GET_VAR(data__->ALPHA,) * (__GET_EXTERNAL(data__->CLI_VPD_KPA,) - __GET_VAR(data__->AVG,)))));
    } else {
      __SET_VAR(data__->,AVG,,__GET_EXTERNAL(data__->CLI_VPD_KPA,));
      __SET_VAR(data__->,SEEDED,,__BOOL_LITERAL(TRUE));
    };
    __SET_VAR(data__->,FOLDED_AT,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  };
  if (__GET_VAR(data__->SEEDED,)) {
    __SET_EXTERNAL(data__->,IRR_VPDAVG_KPA,,__GET_VAR(data__->AVG,));
    __SET_VAR(data__->,DEMAND,,___LADDER(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->AVG,),
      (REAL)__GET_EXTERNAL(data__->IRR_DRYLO_KPA,),
      (REAL)__GET_EXTERNAL(data__->IRR_DRYHI_KPA,)));
    __SET_VAR(data__->,MIN_MOD,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(0.0),
      (REAL)__GET_EXTERNAL(data__->IRR_MODMIN,),
      (REAL)__REAL_LITERAL(1.0)));
    __SET_VAR(data__->,GAMMA,,___MAX(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__REAL_LITERAL(1.0),
      (REAL)__GET_EXTERNAL(data__->IRR_MODGAMMA,)));
    __SET_EXTERNAL(data__->,IRR_MOD,,___LIMIT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (REAL)__GET_VAR(data__->MIN_MOD,),
      (REAL)(__GET_VAR(data__->MIN_MOD,) + ((__REAL_LITERAL(1.0) - __GET_VAR(data__->MIN_MOD,)) * ___POW(
        (BOOL)__BOOL_LITERAL(TRUE),
        NULL,
        (REAL)__GET_VAR(data__->DEMAND,),
        (REAL)__GET_VAR(data__->GAMMA,)))),
      (REAL)__REAL_LITERAL(1.0)));
  };
  __SET_EXTERNAL(data__->,IRR_MOD_QLT,,(__GET_VAR(data__->SEEDED,) && __GET_VAR(data__->MEASURED,)));

  goto __end;

__end:
  return;
} // IRRIGATION_MODULATION_body__() 





static void IRRIGATION_SCHEDULE_init__(IRRIGATION_SCHEDULE_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,IRR_MOD,data__->IRR_MOD,retain)
  __INIT_EXTERNAL(REAL,IRR_VOLFREQSPLIT,data__->IRR_VOLFREQSPLIT,retain)
  __INIT_EXTERNAL(UINT,IRR_TRAYSPERDAY,data__->IRR_TRAYSPERDAY,retain)
  __INIT_EXTERNAL(REAL,CLI_TEMP_EXT,data__->CLI_TEMP_EXT,retain)
  __INIT_EXTERNAL(BOOL,CLI_TEMP_EXT_QLT,data__->CLI_TEMP_EXT_QLT,retain)
  __INIT_EXTERNAL(REAL,IRR_DAWNGATE_DEGC,data__->IRR_DAWNGATE_DEGC,retain)
  __INIT_EXTERNAL(USINT,CLI_PHASE,data__->CLI_PHASE,retain)
  __INIT_EXTERNAL(BOOL,CLI_PHASE_QLT,data__->CLI_PHASE_QLT,retain)
  __INIT_EXTERNAL(BOOL,SOL_READY,data__->SOL_READY,retain)
  __INIT_EXTERNAL(INT,SOL_FROMRISE_MIN,data__->SOL_FROMRISE_MIN,retain)
  __INIT_EXTERNAL(INT,SOL_DAYRISE_MIN,data__->SOL_DAYRISE_MIN,retain)
  __INIT_EXTERNAL(UINT,IRR_MORNINGWIN_MIN,data__->IRR_MORNINGWIN_MIN,retain)
  __INIT_EXTERNAL(UDINT,SYS_TODAY,data__->SYS_TODAY,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY1,data__->IRR_DAYS_TRAY1,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY2,data__->IRR_DAYS_TRAY2,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY3,data__->IRR_DAYS_TRAY3,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY4,data__->IRR_DAYS_TRAY4,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY5,data__->IRR_DAYS_TRAY5,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY6,data__->IRR_DAYS_TRAY6,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY7,data__->IRR_DAYS_TRAY7,retain)
  __INIT_EXTERNAL(UINT,IRR_DAYS_TRAY8,data__->IRR_DAYS_TRAY8,retain)
  __INIT_EXTERNAL(USINT,IRR_DUETRAY,data__->IRR_DUETRAY,retain)
  __INIT_EXTERNAL(BOOL,IRR_SLOTOPEN,data__->IRR_SLOTOPEN,retain)
  __INIT_EXTERNAL(REAL,IRR_PERIOD_DAYS,data__->IRR_PERIOD_DAYS,retain)
  __INIT_EXTERNAL(UDINT,IRR_TODAY,data__->IRR_TODAY,retain)
  __INIT_EXTERNAL(USINT,IRR_SERVEDTODAY,data__->IRR_SERVEDTODAY,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCENOW,data__->IRR_FORCENOW,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY1,data__->IRR_LAST_TRAY1,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY2,data__->IRR_LAST_TRAY2,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY3,data__->IRR_LAST_TRAY3,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY4,data__->IRR_LAST_TRAY4,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY5,data__->IRR_LAST_TRAY5,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY6,data__->IRR_LAST_TRAY6,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY7,data__->IRR_LAST_TRAY7,retain)
  __INIT_EXTERNAL(UDINT,IRR_LAST_TRAY8,data__->IRR_LAST_TRAY8,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY1,data__->IRR_FORCEON_TRAY1,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY2,data__->IRR_FORCEON_TRAY2,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY3,data__->IRR_FORCEON_TRAY3,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY4,data__->IRR_FORCEON_TRAY4,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY5,data__->IRR_FORCEON_TRAY5,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY6,data__->IRR_FORCEON_TRAY6,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY7,data__->IRR_FORCEON_TRAY7,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEON_TRAY8,data__->IRR_FORCEON_TRAY8,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY1,data__->IRR_FORCEOFF_TRAY1,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY2,data__->IRR_FORCEOFF_TRAY2,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY3,data__->IRR_FORCEOFF_TRAY3,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY4,data__->IRR_FORCEOFF_TRAY4,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY5,data__->IRR_FORCEOFF_TRAY5,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY6,data__->IRR_FORCEOFF_TRAY6,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY7,data__->IRR_FORCEOFF_TRAY7,retain)
  __INIT_EXTERNAL(BOOL,IRR_FORCEOFF_TRAY8,data__->IRR_FORCEOFF_TRAY8,retain)
  TRAY_DUE_init__(&data__->TRAY1,retain);
  TRAY_DUE_init__(&data__->TRAY2,retain);
  TRAY_DUE_init__(&data__->TRAY3,retain);
  TRAY_DUE_init__(&data__->TRAY4,retain);
  TRAY_DUE_init__(&data__->TRAY5,retain);
  TRAY_DUE_init__(&data__->TRAY6,retain);
  TRAY_DUE_init__(&data__->TRAY7,retain);
  TRAY_DUE_init__(&data__->TRAY8,retain);
  __INIT_VAR(data__->SERVED_DAY,0,retain)
}

// Code part
static void IRRIGATION_SCHEDULE_body__(IRRIGATION_SCHEDULE_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,FORCED,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,OPEN_SLOT,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,SPLIT_LIM,,0);
  __SET_VAR(data__->,STRETCH,,0);
  __SET_VAR(data__->,SHORTEST,,0);
  __SET_VAR(data__->,BEST,,0);
  __SET_VAR(data__->,WORST,,0);
  __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(FALSE));

  __SET_EXTERNAL(data__->,IRR_TODAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  if (((__GET_EXTERNAL(data__->SYS_TODAY,) != __UDINT_LITERAL(0)) && (__GET_EXTERNAL(data__->SYS_TODAY,) != __GET_VAR(data__->SERVED_DAY,)))) {
    __SET_VAR(data__->,SERVED_DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
    __SET_EXTERNAL(data__->,IRR_SERVEDTODAY,,__USINT_LITERAL(0));
  };
  __SET_VAR(data__->,FORCED,,__GET_EXTERNAL(data__->IRR_FORCENOW,));
  if (((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(1)) || (__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(2)))) {
    __SET_VAR(data__->,OPEN_SLOT,,(__GET_EXTERNAL(data__->CLI_TEMP_EXT,) >= __GET_EXTERNAL(data__->IRR_DAWNGATE_DEGC,)));
  } else {
    __SET_VAR(data__->,OPEN_SLOT,,((__GET_EXTERNAL(data__->CLI_PHASE,) == __USINT_LITERAL(3)) && (__GET_EXTERNAL(data__->SOL_FROMRISE_MIN,) <= (__GET_EXTERNAL(data__->SOL_DAYRISE_MIN,) + ___UINT_TO_INT(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_EXTERNAL(data__->IRR_MORNINGWIN_MIN,))))));
  };
  __SET_EXTERNAL(data__->,IRR_SLOTOPEN,,__GET_VAR(data__->OPEN_SLOT,));
  __SET_VAR(data__->,SHORTEST,,__UINT_LITERAL(0));
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY1,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY1,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY1,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY2,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY2,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY2,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY3,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY3,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY3,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY4,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY4,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY4,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY5,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY5,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY5,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY6,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY6,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY6,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY7,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY7,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY7,));
  };
  if (((__GET_EXTERNAL(data__->IRR_DAYS_TRAY8,) > __UINT_LITERAL(0)) && ((__GET_VAR(data__->SHORTEST,) == __UINT_LITERAL(0)) || (__GET_EXTERNAL(data__->IRR_DAYS_TRAY8,) < __GET_VAR(data__->SHORTEST,))))) {
    __SET_VAR(data__->,SHORTEST,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY8,));
  };
  if ((__GET_VAR(data__->SHORTEST,) > __UINT_LITERAL(0))) {
    __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,___UINT_TO_REAL(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (UINT)__GET_VAR(data__->SHORTEST,)));
  };
  __SET_VAR(data__->,SPLIT_LIM,,___LIMIT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__REAL_LITERAL(0.0),
    (REAL)__GET_EXTERNAL(data__->IRR_VOLFREQSPLIT,),
    (REAL)__REAL_LITERAL(1.0)));
  __SET_VAR(data__->,STRETCH,,___POW(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_EXTERNAL(data__->IRR_MOD,),
    (REAL)(__REAL_LITERAL(1.0) - __GET_VAR(data__->SPLIT_LIM,))));
  __SET_VAR(data__->,TRAY1.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY1,));
  __SET_VAR(data__->,TRAY1.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY1,));
  __SET_VAR(data__->,TRAY1.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY1,));
  __SET_VAR(data__->,TRAY1.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY1.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY1);
  __SET_VAR(data__->,TRAY2.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY2,));
  __SET_VAR(data__->,TRAY2.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY2,));
  __SET_VAR(data__->,TRAY2.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY2,));
  __SET_VAR(data__->,TRAY2.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY2.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY2);
  __SET_VAR(data__->,TRAY3.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY3,));
  __SET_VAR(data__->,TRAY3.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY3,));
  __SET_VAR(data__->,TRAY3.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY3,));
  __SET_VAR(data__->,TRAY3.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY3.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY3);
  __SET_VAR(data__->,TRAY4.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY4,));
  __SET_VAR(data__->,TRAY4.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY4,));
  __SET_VAR(data__->,TRAY4.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY4,));
  __SET_VAR(data__->,TRAY4.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY4.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY4);
  __SET_VAR(data__->,TRAY5.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY5,));
  __SET_VAR(data__->,TRAY5.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY5,));
  __SET_VAR(data__->,TRAY5.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY5,));
  __SET_VAR(data__->,TRAY5.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY5.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY5);
  __SET_VAR(data__->,TRAY6.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY6,));
  __SET_VAR(data__->,TRAY6.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY6,));
  __SET_VAR(data__->,TRAY6.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY6,));
  __SET_VAR(data__->,TRAY6.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY6.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY6);
  __SET_VAR(data__->,TRAY7.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY7,));
  __SET_VAR(data__->,TRAY7.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY7,));
  __SET_VAR(data__->,TRAY7.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY7,));
  __SET_VAR(data__->,TRAY7.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY7.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY7);
  __SET_VAR(data__->,TRAY8.DAYS,,__GET_EXTERNAL(data__->IRR_DAYS_TRAY8,));
  __SET_VAR(data__->,TRAY8.LAST,,__GET_EXTERNAL(data__->IRR_LAST_TRAY8,));
  __SET_VAR(data__->,TRAY8.FORCE_OFF,,__GET_EXTERNAL(data__->IRR_FORCEOFF_TRAY8,));
  __SET_VAR(data__->,TRAY8.DAY,,__GET_EXTERNAL(data__->SYS_TODAY,));
  __SET_VAR(data__->,TRAY8.STRETCH,,__GET_VAR(data__->STRETCH,));
  TRAY_DUE_body__(&data__->TRAY8);
  if (__GET_VAR(data__->TRAY1.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY1,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY1,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY2.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY2,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY2,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY3.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY3,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY3,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY4.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY4,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY4,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY5.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY5,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY5,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY6.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY6,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY6,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY7.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY7,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY7,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if (__GET_VAR(data__->TRAY8.SKIP,)) {
    __SET_EXTERNAL(data__->,IRR_FORCEOFF_TRAY8,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,IRR_LAST_TRAY8,,__GET_EXTERNAL(data__->SYS_TODAY,));
  };
  if ((((___USINT_TO_UINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (USINT)__GET_EXTERNAL(data__->IRR_SERVEDTODAY,)) >= __GET_EXTERNAL(data__->IRR_TRAYSPERDAY,)) && !(__GET_VAR(data__->FORCED,))) || (!(__GET_VAR(data__->FORCED,)) && !(__GET_VAR(data__->OPEN_SLOT,))))) {
    __SET_EXTERNAL(data__->,IRR_DUETRAY,,__USINT_LITERAL(0));
  } else {
    __SET_VAR(data__->,BEST,,__USINT_LITERAL(0));
    __SET_VAR(data__->,WORST,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(FALSE));
    if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY1,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(1));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY2,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(2));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY3,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(3));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY4,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(4));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY5,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(5));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY6,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(6));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY7,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(7));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    } else if (__GET_EXTERNAL(data__->IRR_FORCEON_TRAY8,)) {
      __SET_VAR(data__->,BEST,,__USINT_LITERAL(8));
      __SET_VAR(data__->,PICKED,,__BOOL_LITERAL(TRUE));
    };
    if (!(__GET_VAR(data__->PICKED,))) {
      if ((__GET_VAR(data__->TRAY1.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY1.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(1));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY1.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY1.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY2.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY2.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(2));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY2.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY2.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY3.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY3.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(3));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY3.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY3.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY4.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY4.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(4));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY4.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY4.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY5.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY5.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(5));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY5.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY5.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY6.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY6.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(6));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY6.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY6.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY7.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY7.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(7));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY7.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY7.INTERVAL,));
      };
      if ((__GET_VAR(data__->TRAY8.DUE,) && ((__GET_VAR(data__->BEST,) == __USINT_LITERAL(0)) || (__GET_VAR(data__->TRAY8.OVERDUE,) > __GET_VAR(data__->WORST,))))) {
        __SET_VAR(data__->,BEST,,__USINT_LITERAL(8));
        __SET_VAR(data__->,WORST,,__GET_VAR(data__->TRAY8.OVERDUE,));
        __SET_EXTERNAL(data__->,IRR_PERIOD_DAYS,,__GET_VAR(data__->TRAY8.INTERVAL,));
      };
    };
    __SET_EXTERNAL(data__->,IRR_DUETRAY,,__GET_VAR(data__->BEST,));
  };

  goto __end;

__end:
  return;
} // IRRIGATION_SCHEDULE_body__() 





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

  __SET_VAR(data__->,SUPPLY.V,,__GET_EXTERNAL(data__->GRID_LN_V,));
  __SET_VAR(data__->,SUPPLY.V_OK,,__GET_EXTERNAL(data__->GRID_LN_V_QLT,));
  __SET_VAR(data__->,SUPPLY.A,,__GET_EXTERNAL(data__->GRID_TOTAL_A,));
  __SET_VAR(data__->,SUPPLY.A_OK,,__GET_EXTERNAL(data__->GRID_TOTAL_A_QLT,));
  __SET_VAR(data__->,SUPPLY.W,,__GET_EXTERNAL(data__->GRID_TOTAL_W,));
  __SET_VAR(data__->,SUPPLY.W_OK,,__GET_EXTERNAL(data__->GRID_TOTAL_W_QLT,));
  __SET_VAR(data__->,SUPPLY.HZ,,__GET_EXTERNAL(data__->GRID_HZ,));
  __SET_VAR(data__->,SUPPLY.HZ_OK,,__GET_EXTERNAL(data__->GRID_HZ_QLT,));
  __SET_VAR(data__->,SUPPLY.LO_V,,__GET_EXTERNAL(data__->PWR_GRIDLO_V,));
  __SET_VAR(data__->,SUPPLY.HI_V,,__GET_EXTERNAL(data__->PWR_GRIDHI_V,));
  __SET_VAR(data__->,SUPPLY.LO_HZ,,__GET_EXTERNAL(data__->PWR_GRIDLO_HZ,));
  __SET_VAR(data__->,SUPPLY.HI_HZ,,__GET_EXTERNAL(data__->PWR_GRIDHI_HZ,));
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

  __SET_VAR(data__->,SUPPLY.V,,__GET_EXTERNAL(data__->ATS_V,));
  __SET_VAR(data__->,SUPPLY.V_OK,,__GET_EXTERNAL(data__->ATS_V_QLT,));
  __SET_VAR(data__->,SUPPLY.A,,__GET_EXTERNAL(data__->ATS_A,));
  __SET_VAR(data__->,SUPPLY.A_OK,,__GET_EXTERNAL(data__->ATS_A_QLT,));
  __SET_VAR(data__->,SUPPLY.W,,__GET_EXTERNAL(data__->ATS_W,));
  __SET_VAR(data__->,SUPPLY.W_OK,,__GET_EXTERNAL(data__->ATS_W_QLT,));
  __SET_VAR(data__->,SUPPLY.HZ,,__GET_EXTERNAL(data__->ATS_HZ,));
  __SET_VAR(data__->,SUPPLY.HZ_OK,,__GET_EXTERNAL(data__->ATS_HZ_QLT,));
  __SET_VAR(data__->,SUPPLY.LO_V,,__GET_EXTERNAL(data__->PWR_LOCALLO_V,));
  __SET_VAR(data__->,SUPPLY.HI_V,,__GET_EXTERNAL(data__->PWR_LOCALHI_V,));
  __SET_VAR(data__->,SUPPLY.LO_HZ,,__GET_EXTERNAL(data__->PWR_LOCALLO_HZ,));
  __SET_VAR(data__->,SUPPLY.HI_HZ,,__GET_EXTERNAL(data__->PWR_LOCALHI_HZ,));
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
  __SET_VAR(data__->,CRANKING.IN,,(!(__GET_EXTERNAL(data__->SYS_GRID_OK,)) && !(__GET_VAR(data__->SUPPLY.ANSWERED,))));
  __SET_VAR(data__->,CRANKING.PT,,___SEC_MS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_EXTERNAL(data__->PWR_GENSTART_SEC,)));
  __SET_VAR(data__->,CRANKING.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
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
  __INIT_EXTERNAL(BOOL,CLI_DTSTRAT_DEGC_QLT,data__->CLI_DTSTRAT_DEGC_QLT,retain)
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
  if (__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,)) {
    __SET_EXTERNAL(data__->,ALARM_TOO_HOT,,(__GET_EXTERNAL(data__->CLI_TEMP_HI,) > __GET_EXTERNAL(data__->CLI_TOOHOT_DEGC,)));
    __SET_EXTERNAL(data__->,ALARM_TOO_COLD,,(__GET_EXTERNAL(data__->CLI_TEMP_HI,) < __GET_EXTERNAL(data__->CLI_TOOCOLD_DEGC,)));
  } else {
    __SET_EXTERNAL(data__->,ALARM_TOO_HOT,,__BOOL_LITERAL(FALSE));
    __SET_EXTERNAL(data__->,ALARM_TOO_COLD,,__BOOL_LITERAL(FALSE));
  };
  if (__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC_QLT,)) {
    __SET_EXTERNAL(data__->,ALARM_TOO_HUMID,,(__GET_EXTERNAL(data__->CLI_DPMARGIN_DEGC,) < __GET_EXTERNAL(data__->CLI_DEHUMFULL_DEGC,)));
  } else {
    __SET_EXTERNAL(data__->,ALARM_TOO_HUMID,,__BOOL_LITERAL(FALSE));
  };
  if (__GET_EXTERNAL(data__->CLI_DTSTRAT_DEGC_QLT,)) {
    __SET_EXTERNAL(data__->,ALARM_STRAT,,(__GET_EXTERNAL(data__->CLI_DTSTRAT_DEGC,) > __GET_EXTERNAL(data__->CLI_DTSTRATHIGH_DEGC,)));
  } else {
    __SET_EXTERNAL(data__->,ALARM_STRAT,,__BOOL_LITERAL(FALSE));
  };

  goto __end;

__end:
  return;
} // CLIMATE_WATCH_body__() 





static void DC_WATCH_init__(DC_WATCH_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,PWR_DC1_V,data__->PWR_DC1_V,retain)
  __INIT_EXTERNAL(BOOL,PWR_DC1_V_QLT,data__->PWR_DC1_V_QLT,retain)
  __INIT_EXTERNAL(REAL,PWR_DC2_V,data__->PWR_DC2_V,retain)
  __INIT_EXTERNAL(BOOL,PWR_DC2_V_QLT,data__->PWR_DC2_V_QLT,retain)
  __INIT_EXTERNAL(REAL,PWR_UPSBAT_V,data__->PWR_UPSBAT_V,retain)
  __INIT_EXTERNAL(BOOL,PWR_UPSBAT_V_QLT,data__->PWR_UPSBAT_V_QLT,retain)
  __INIT_EXTERNAL(REAL,PWR_GENBAT_V,data__->PWR_GENBAT_V,retain)
  __INIT_EXTERNAL(BOOL,PWR_GENBAT_V_QLT,data__->PWR_GENBAT_V_QLT,retain)
  __INIT_EXTERNAL(REAL,PWR_DC1LO_V,data__->PWR_DC1LO_V,retain)
  __INIT_EXTERNAL(REAL,PWR_DC2LO_V,data__->PWR_DC2LO_V,retain)
  __INIT_EXTERNAL(REAL,PWR_UPSBATLO_V,data__->PWR_UPSBATLO_V,retain)
  __INIT_EXTERNAL(REAL,PWR_GENBATLO_V,data__->PWR_GENBATLO_V,retain)
  __INIT_EXTERNAL(UINT,PWR_DC1_AVG_MS,data__->PWR_DC1_AVG_MS,retain)
  __INIT_EXTERNAL(UINT,PWR_DC2_AVG_MS,data__->PWR_DC2_AVG_MS,retain)
  __INIT_EXTERNAL(UINT,PWR_UPSBAT_AVG_MS,data__->PWR_UPSBAT_AVG_MS,retain)
  __INIT_EXTERNAL(UINT,PWR_GENBAT_AVG_MS,data__->PWR_GENBAT_AVG_MS,retain)
  __INIT_EXTERNAL(BOOL,PWR_UPSLOWBAT,data__->PWR_UPSLOWBAT,retain)
  __INIT_EXTERNAL(UDINT,SYS_NOW_MS,data__->SYS_NOW_MS,retain)
  __INIT_EXTERNAL(REAL,SYS_DC1_V,data__->SYS_DC1_V,retain)
  __INIT_EXTERNAL(BOOL,SYS_DC1_V_QLT,data__->SYS_DC1_V_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_DC2_V,data__->SYS_DC2_V,retain)
  __INIT_EXTERNAL(BOOL,SYS_DC2_V_QLT,data__->SYS_DC2_V_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_UPSBAT_V,data__->SYS_UPSBAT_V,retain)
  __INIT_EXTERNAL(BOOL,SYS_UPSBAT_V_QLT,data__->SYS_UPSBAT_V_QLT,retain)
  __INIT_EXTERNAL(REAL,SYS_GENBAT_V,data__->SYS_GENBAT_V,retain)
  __INIT_EXTERNAL(BOOL,SYS_GENBAT_V_QLT,data__->SYS_GENBAT_V_QLT,retain)
  __INIT_EXTERNAL(BOOL,SYS_DC_OK,data__->SYS_DC_OK,retain)
  __INIT_EXTERNAL(BOOL,SYS_SUPPLY_LOCKOUT,data__->SYS_SUPPLY_LOCKOUT,retain)
  __INIT_EXTERNAL(BOOL,ALARM_DC_PS1,data__->ALARM_DC_PS1,retain)
  __INIT_EXTERNAL(BOOL,ALARM_DC_PS2,data__->ALARM_DC_PS2,retain)
  __INIT_EXTERNAL(BOOL,ALARM_DC_UPS,data__->ALARM_DC_UPS,retain)
  __INIT_EXTERNAL(BOOL,ALARM_GENERATOR_BATTERY,data__->ALARM_GENERATOR_BATTERY,retain)
  DC_RAIL_init__(&data__->PS1,retain);
  DC_RAIL_init__(&data__->PS2,retain);
  DC_RAIL_init__(&data__->UPS,retain);
  DC_RAIL_init__(&data__->GEN,retain);
  TON_MS_init__(&data__->LOW_FOR,retain);
}

// Code part
static void DC_WATCH_body__(DC_WATCH_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,LOW,,__BOOL_LITERAL(FALSE));

  __SET_VAR(data__->,PS1.V,,__GET_EXTERNAL(data__->PWR_DC1_V,));
  __SET_VAR(data__->,PS1.V_OK,,__GET_EXTERNAL(data__->PWR_DC1_V_QLT,));
  __SET_VAR(data__->,PS1.LO,,__GET_EXTERNAL(data__->PWR_DC1LO_V,));
  __SET_VAR(data__->,PS1.AVG_MS,,__GET_EXTERNAL(data__->PWR_DC1_AVG_MS,));
  __SET_VAR(data__->,PS1.BAND,,__REAL_LITERAL(0.5));
  __SET_VAR(data__->,PS1.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  DC_RAIL_body__(&data__->PS1);
  __SET_VAR(data__->,PS2.V,,__GET_EXTERNAL(data__->PWR_DC2_V,));
  __SET_VAR(data__->,PS2.V_OK,,__GET_EXTERNAL(data__->PWR_DC2_V_QLT,));
  __SET_VAR(data__->,PS2.LO,,__GET_EXTERNAL(data__->PWR_DC2LO_V,));
  __SET_VAR(data__->,PS2.AVG_MS,,__GET_EXTERNAL(data__->PWR_DC2_AVG_MS,));
  __SET_VAR(data__->,PS2.BAND,,__REAL_LITERAL(0.5));
  __SET_VAR(data__->,PS2.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  DC_RAIL_body__(&data__->PS2);
  __SET_VAR(data__->,UPS.V,,__GET_EXTERNAL(data__->PWR_UPSBAT_V,));
  __SET_VAR(data__->,UPS.V_OK,,__GET_EXTERNAL(data__->PWR_UPSBAT_V_QLT,));
  __SET_VAR(data__->,UPS.LO,,__GET_EXTERNAL(data__->PWR_UPSBATLO_V,));
  __SET_VAR(data__->,UPS.AVG_MS,,__GET_EXTERNAL(data__->PWR_UPSBAT_AVG_MS,));
  __SET_VAR(data__->,UPS.BAND,,__REAL_LITERAL(0.5));
  __SET_VAR(data__->,UPS.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  DC_RAIL_body__(&data__->UPS);
  __SET_VAR(data__->,GEN.V,,__GET_EXTERNAL(data__->PWR_GENBAT_V,));
  __SET_VAR(data__->,GEN.V_OK,,__GET_EXTERNAL(data__->PWR_GENBAT_V_QLT,));
  __SET_VAR(data__->,GEN.LO,,__GET_EXTERNAL(data__->PWR_GENBATLO_V,));
  __SET_VAR(data__->,GEN.AVG_MS,,__GET_EXTERNAL(data__->PWR_GENBAT_AVG_MS,));
  __SET_VAR(data__->,GEN.BAND,,__REAL_LITERAL(0.5));
  __SET_VAR(data__->,GEN.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  DC_RAIL_body__(&data__->GEN);
  __SET_EXTERNAL(data__->,SYS_DC1_V,,__GET_VAR(data__->PS1.Y,));
  __SET_EXTERNAL(data__->,SYS_DC1_V_QLT,,__GET_VAR(data__->PS1.OK,));
  __SET_EXTERNAL(data__->,SYS_DC2_V,,__GET_VAR(data__->PS2.Y,));
  __SET_EXTERNAL(data__->,SYS_DC2_V_QLT,,__GET_VAR(data__->PS2.OK,));
  __SET_EXTERNAL(data__->,SYS_UPSBAT_V,,__GET_VAR(data__->UPS.Y,));
  __SET_EXTERNAL(data__->,SYS_UPSBAT_V_QLT,,__GET_VAR(data__->UPS.OK,));
  __SET_EXTERNAL(data__->,SYS_GENBAT_V,,__GET_VAR(data__->GEN.Y,));
  __SET_EXTERNAL(data__->,SYS_GENBAT_V_QLT,,__GET_VAR(data__->GEN.OK,));
  __SET_EXTERNAL(data__->,ALARM_DC_PS1,,!(__GET_VAR(data__->PS1.HEALTHY,)));
  __SET_EXTERNAL(data__->,ALARM_DC_PS2,,!(__GET_VAR(data__->PS2.HEALTHY,)));
  __SET_EXTERNAL(data__->,ALARM_GENERATOR_BATTERY,,!(__GET_VAR(data__->GEN.HEALTHY,)));
  __SET_VAR(data__->LOW_FOR.,IN,,__GET_EXTERNAL(data__->PWR_UPSLOWBAT,));
  __SET_VAR(data__->LOW_FOR.,PT,,__UDINT_LITERAL(2000));
  __SET_VAR(data__->LOW_FOR.,NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  TON_MS_body__(&data__->LOW_FOR);
  __SET_VAR(data__->,LOW,,__GET_VAR(data__->LOW_FOR.Q,));
  __SET_EXTERNAL(data__->,ALARM_DC_UPS,,__GET_VAR(data__->LOW,));
  __SET_EXTERNAL(data__->,SYS_SUPPLY_LOCKOUT,,__GET_VAR(data__->LOW,));
  __SET_EXTERNAL(data__->,SYS_DC_OK,,((((__GET_VAR(data__->PS1.HEALTHY,) && __GET_VAR(data__->PS2.HEALTHY,)) && __GET_VAR(data__->UPS.HEALTHY,)) && __GET_VAR(data__->GEN.HEALTHY,)) && !(__GET_VAR(data__->LOW,))));

  goto __end;

__end:
  return;
} // DC_WATCH_body__() 





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
  __SET_VAR(data__->,CEILING.T1,,__GET_EXTERNAL(data__->CLI_TEMP_HI1,));
  __SET_VAR(data__->,CEILING.T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_HI1_QLT,));
  __SET_VAR(data__->,CEILING.T2,,__GET_EXTERNAL(data__->CLI_TEMP_HI2,));
  __SET_VAR(data__->,CEILING.T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_HI2_QLT,));
  __SET_VAR(data__->,CEILING.T3,,__GET_EXTERNAL(data__->CLI_TEMP_HI3,));
  __SET_VAR(data__->,CEILING.T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_HI3_QLT,));
  __SET_VAR(data__->,CEILING.H1,,__GET_EXTERNAL(data__->CLI_RH_HI1,));
  __SET_VAR(data__->,CEILING.H1_OK,,__GET_EXTERNAL(data__->CLI_RH_HI1_QLT,));
  __SET_VAR(data__->,CEILING.H2,,__GET_EXTERNAL(data__->CLI_RH_HI2,));
  __SET_VAR(data__->,CEILING.H2_OK,,__GET_EXTERNAL(data__->CLI_RH_HI2_QLT,));
  __SET_VAR(data__->,CEILING.H3,,__GET_EXTERNAL(data__->CLI_RH_HI3,));
  __SET_VAR(data__->,CEILING.H3_OK,,__GET_EXTERNAL(data__->CLI_RH_HI3_QLT,));
  __SET_VAR(data__->,CEILING.TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->,CEILING.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->CEILING);
  __SET_EXTERNAL(data__->,CLI_TEMP_HI,,__GET_VAR(data__->CEILING.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_HI,,__GET_VAR(data__->CEILING.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_CEILING,,__GET_VAR(data__->CEILING.FAULT,));
  __SET_VAR(data__->,FLOOR.T1,,__GET_EXTERNAL(data__->CLI_TEMP_LO1,));
  __SET_VAR(data__->,FLOOR.T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_LO1_QLT,));
  __SET_VAR(data__->,FLOOR.T2,,__GET_EXTERNAL(data__->CLI_TEMP_LO2,));
  __SET_VAR(data__->,FLOOR.T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_LO2_QLT,));
  __SET_VAR(data__->,FLOOR.T3,,__GET_EXTERNAL(data__->CLI_TEMP_LO3,));
  __SET_VAR(data__->,FLOOR.T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_LO3_QLT,));
  __SET_VAR(data__->,FLOOR.H1,,__GET_EXTERNAL(data__->CLI_RH_LO1,));
  __SET_VAR(data__->,FLOOR.H1_OK,,__GET_EXTERNAL(data__->CLI_RH_LO1_QLT,));
  __SET_VAR(data__->,FLOOR.H2,,__GET_EXTERNAL(data__->CLI_RH_LO2,));
  __SET_VAR(data__->,FLOOR.H2_OK,,__GET_EXTERNAL(data__->CLI_RH_LO2_QLT,));
  __SET_VAR(data__->,FLOOR.H3,,__GET_EXTERNAL(data__->CLI_RH_LO3,));
  __SET_VAR(data__->,FLOOR.H3_OK,,__GET_EXTERNAL(data__->CLI_RH_LO3_QLT,));
  __SET_VAR(data__->,FLOOR.TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->,FLOOR.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->FLOOR);
  __SET_EXTERNAL(data__->,CLI_TEMP_LO,,__GET_VAR(data__->FLOOR.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_LO,,__GET_VAR(data__->FLOOR.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_FLOOR,,__GET_VAR(data__->FLOOR.FAULT,));
  __SET_VAR(data__->,CRAWLSPACE.T1,,__GET_EXTERNAL(data__->CLI_TEMP_CS1,));
  __SET_VAR(data__->,CRAWLSPACE.T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_CS1_QLT,));
  __SET_VAR(data__->,CRAWLSPACE.T2,,__GET_EXTERNAL(data__->CLI_TEMP_CS2,));
  __SET_VAR(data__->,CRAWLSPACE.T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_CS2_QLT,));
  __SET_VAR(data__->,CRAWLSPACE.T3,,__GET_EXTERNAL(data__->CLI_TEMP_CS3,));
  __SET_VAR(data__->,CRAWLSPACE.T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_CS3_QLT,));
  __SET_VAR(data__->,CRAWLSPACE.H1,,__GET_EXTERNAL(data__->CLI_RH_CS1,));
  __SET_VAR(data__->,CRAWLSPACE.H1_OK,,__GET_EXTERNAL(data__->CLI_RH_CS1_QLT,));
  __SET_VAR(data__->,CRAWLSPACE.H2,,__GET_EXTERNAL(data__->CLI_RH_CS2,));
  __SET_VAR(data__->,CRAWLSPACE.H2_OK,,__GET_EXTERNAL(data__->CLI_RH_CS2_QLT,));
  __SET_VAR(data__->,CRAWLSPACE.H3,,__GET_EXTERNAL(data__->CLI_RH_CS3,));
  __SET_VAR(data__->,CRAWLSPACE.H3_OK,,__GET_EXTERNAL(data__->CLI_RH_CS3_QLT,));
  __SET_VAR(data__->,CRAWLSPACE.TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->,CRAWLSPACE.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  ZONE_PROBES_body__(&data__->CRAWLSPACE);
  __SET_EXTERNAL(data__->,CLI_TEMP_CS,,__GET_VAR(data__->CRAWLSPACE.TEMP,));
  __SET_EXTERNAL(data__->,CLI_RH_CS,,__GET_VAR(data__->CRAWLSPACE.RH,));
  __SET_EXTERNAL(data__->,ALARM_SNS_CRAWLSPACE,,__GET_VAR(data__->CRAWLSPACE.FAULT,));
  __SET_VAR(data__->,EXTERIOR.T1,,__GET_EXTERNAL(data__->CLI_TEMP_EXT1,));
  __SET_VAR(data__->,EXTERIOR.T1_OK,,__GET_EXTERNAL(data__->CLI_TEMP_EXT1_QLT,));
  __SET_VAR(data__->,EXTERIOR.T2,,__GET_EXTERNAL(data__->CLI_TEMP_EXT2,));
  __SET_VAR(data__->,EXTERIOR.T2_OK,,__GET_EXTERNAL(data__->CLI_TEMP_EXT2_QLT,));
  __SET_VAR(data__->,EXTERIOR.T3,,__GET_EXTERNAL(data__->CLI_TEMP_EXT3,));
  __SET_VAR(data__->,EXTERIOR.T3_OK,,__GET_EXTERNAL(data__->CLI_TEMP_EXT3_QLT,));
  __SET_VAR(data__->,EXTERIOR.H1,,__GET_EXTERNAL(data__->CLI_RH_EXT1,));
  __SET_VAR(data__->,EXTERIOR.H1_OK,,__GET_EXTERNAL(data__->CLI_RH_EXT1_QLT,));
  __SET_VAR(data__->,EXTERIOR.H2,,__GET_EXTERNAL(data__->CLI_RH_EXT2,));
  __SET_VAR(data__->,EXTERIOR.H2_OK,,__GET_EXTERNAL(data__->CLI_RH_EXT2_QLT,));
  __SET_VAR(data__->,EXTERIOR.H3,,__GET_EXTERNAL(data__->CLI_RH_EXT3,));
  __SET_VAR(data__->,EXTERIOR.H3_OK,,__GET_EXTERNAL(data__->CLI_RH_EXT3_QLT,));
  __SET_VAR(data__->,EXTERIOR.TAU_S,,__GET_VAR(data__->TAU_S,));
  __SET_VAR(data__->,EXTERIOR.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
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
  __SET_VAR(data__->,MARGIN_HI,,0);
  __SET_VAR(data__->,MARGIN_LO,,0);
  __SET_VAR(data__->,VPD_HI,,0);
  __SET_VAR(data__->,VPD_LO,,0);
  __SET_VAR(data__->,HI_WORST,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,WORST_TEMP,,0);
  __SET_VAR(data__->,MEASURED,,__BOOL_LITERAL(FALSE));

  __SET_VAR(data__->,MEASURED,,(__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) || __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,)));
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
  __SET_EXTERNAL(data__->,CLI_TEMP_IN_QLT,,__GET_VAR(data__->MEASURED,));
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
  __SET_VAR(data__->,MARGIN_HI,,(__GET_EXTERNAL(data__->CLI_TEMP_HI,) - __GET_VAR(data__->DP_HI,)));
  __SET_VAR(data__->,MARGIN_LO,,(__GET_EXTERNAL(data__->CLI_TEMP_LO,) - __GET_VAR(data__->DP_LO,)));
  if ((__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,) == __GET_EXTERNAL(data__->CLI_TEMP_LO_QLT,))) {
    __SET_VAR(data__->,HI_WORST,,(__GET_VAR(data__->MARGIN_HI,) <= __GET_VAR(data__->MARGIN_LO,)));
  } else {
    __SET_VAR(data__->,HI_WORST,,__GET_EXTERNAL(data__->CLI_TEMP_HI_QLT,));
  };
  if (__GET_VAR(data__->HI_WORST,)) {
    __SET_EXTERNAL(data__->,CLI_DEWPOINT_DEGC,,__GET_VAR(data__->DP_HI,));
    __SET_EXTERNAL(data__->,CLI_DPMARGIN_DEGC,,__GET_VAR(data__->MARGIN_HI,));
    __SET_VAR(data__->,WORST_TEMP,,__GET_EXTERNAL(data__->CLI_TEMP_HI,));
  } else {
    __SET_EXTERNAL(data__->,CLI_DEWPOINT_DEGC,,__GET_VAR(data__->DP_LO,));
    __SET_EXTERNAL(data__->,CLI_DPMARGIN_DEGC,,__GET_VAR(data__->MARGIN_LO,));
    __SET_VAR(data__->,WORST_TEMP,,__GET_EXTERNAL(data__->CLI_TEMP_LO,));
  };
  __SET_EXTERNAL(data__->,CLI_DPMARGIN_DEGC_QLT,,__GET_VAR(data__->MEASURED,));
  __SET_EXTERNAL(data__->,CLI_SATPRESS_KPA,,___SAT_VAPOUR_PRESSURE(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)__GET_VAR(data__->WORST_TEMP,)));
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
  EWMA_init__(&data__->WIND_PUB,retain);
  EWMA_init__(&data__->GUST_PUB,retain);
  EWMA_init__(&data__->RAIN_PUB,retain);
  EWMA_init__(&data__->SUN_PUB,retain);
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
  __SET_VAR(data__->,WIND_VOTE.A,,__GET_EXTERNAL(data__->CLI_WIND1_MPS,));
  __SET_VAR(data__->,WIND_VOTE.A_OK,,__GET_EXTERNAL(data__->CLI_WIND1_MPS_QLT,));
  __SET_VAR(data__->,WIND_VOTE.B,,__GET_EXTERNAL(data__->CLI_WIND2_MPS,));
  __SET_VAR(data__->,WIND_VOTE.B_OK,,__GET_EXTERNAL(data__->CLI_WIND2_MPS_QLT,));
  WX_WORSE_body__(&data__->WIND_VOTE);
  __SET_VAR(data__->,GUST_VOTE.A,,__GET_EXTERNAL(data__->CLI_WINDGUST1_MPS,));
  __SET_VAR(data__->,GUST_VOTE.A_OK,,__GET_EXTERNAL(data__->CLI_WINDGUST1_MPS_QLT,));
  __SET_VAR(data__->,GUST_VOTE.B,,__GET_EXTERNAL(data__->CLI_WINDGUST2_MPS,));
  __SET_VAR(data__->,GUST_VOTE.B_OK,,__GET_EXTERNAL(data__->CLI_WINDGUST2_MPS_QLT,));
  WX_WORSE_body__(&data__->GUST_VOTE);
  __SET_VAR(data__->,RAIN_VOTE.A,,__GET_EXTERNAL(data__->CLI_RAIN1_MMPH,));
  __SET_VAR(data__->,RAIN_VOTE.A_OK,,__GET_EXTERNAL(data__->CLI_RAIN1_MMPH_QLT,));
  __SET_VAR(data__->,RAIN_VOTE.B,,__GET_EXTERNAL(data__->CLI_RAIN2_MMPH,));
  __SET_VAR(data__->,RAIN_VOTE.B_OK,,__GET_EXTERNAL(data__->CLI_RAIN2_MMPH_QLT,));
  WX_WORSE_body__(&data__->RAIN_VOTE);
  __SET_VAR(data__->,SUN_VOTE.A,,__GET_EXTERNAL(data__->CLI_IRRAD1_WPM2,));
  __SET_VAR(data__->,SUN_VOTE.A_OK,,__GET_EXTERNAL(data__->CLI_IRRAD1_WPM2_QLT,));
  __SET_VAR(data__->,SUN_VOTE.B,,__GET_EXTERNAL(data__->CLI_IRRAD2_WPM2,));
  __SET_VAR(data__->,SUN_VOTE.B_OK,,__GET_EXTERNAL(data__->CLI_IRRAD2_WPM2_QLT,));
  WX_MEAN_body__(&data__->SUN_VOTE);
  __SET_VAR(data__->,WIND_PUB.X,,__GET_VAR(data__->WIND_VOTE.Y,));
  __SET_VAR(data__->,WIND_PUB.OK_IN,,(__GET_VAR(data__->WIND_VOTE.USED,) > __USINT_LITERAL(0)));
  __SET_VAR(data__->,WIND_PUB.TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->,WIND_PUB.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,WIND_PUB.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,WIND_PUB.HI,,__REAL_LITERAL(80.0));
  EWMA_body__(&data__->WIND_PUB);
  __SET_VAR(data__->,GUST_PUB.X,,__GET_VAR(data__->GUST_VOTE.Y,));
  __SET_VAR(data__->,GUST_PUB.OK_IN,,(__GET_VAR(data__->GUST_VOTE.USED,) > __USINT_LITERAL(0)));
  __SET_VAR(data__->,GUST_PUB.TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->,GUST_PUB.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,GUST_PUB.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,GUST_PUB.HI,,__REAL_LITERAL(80.0));
  EWMA_body__(&data__->GUST_PUB);
  __SET_VAR(data__->,RAIN_PUB.X,,__GET_VAR(data__->RAIN_VOTE.Y,));
  __SET_VAR(data__->,RAIN_PUB.OK_IN,,(__GET_VAR(data__->RAIN_VOTE.USED,) > __USINT_LITERAL(0)));
  __SET_VAR(data__->,RAIN_PUB.TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->,RAIN_PUB.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,RAIN_PUB.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,RAIN_PUB.HI,,__REAL_LITERAL(200.0));
  EWMA_body__(&data__->RAIN_PUB);
  __SET_VAR(data__->,SUN_PUB.X,,__GET_VAR(data__->SUN_VOTE.Y,));
  __SET_VAR(data__->,SUN_PUB.OK_IN,,(__GET_VAR(data__->SUN_VOTE.USED,) > __USINT_LITERAL(0)));
  __SET_VAR(data__->,SUN_PUB.TAU_S,,__GET_VAR(data__->TAU,));
  __SET_VAR(data__->,SUN_PUB.NOW,,__GET_EXTERNAL(data__->SYS_NOW_MS,));
  __SET_VAR(data__->,SUN_PUB.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,SUN_PUB.HI,,__REAL_LITERAL(1500.0));
  EWMA_body__(&data__->SUN_PUB);
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





