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
static BOOL ___GOOD(
  BOOL EN, 
  BOOL *__ENO, 
  QUALITY Q);
static BOOL ___USABLE(
  BOOL EN, 
  BOOL *__ENO, 
  QUALITY Q);
static QUALITY ___WORST_OF(
  BOOL EN, 
  BOOL *__ENO, 
  QUALITY A, 
  QUALITY B);
static QUALITY ___QUAL(
  BOOL EN, 
  BOOL *__ENO, 
  BOOL OK, 
  BOOL SEEDED);
static HEALTH ___WORST_HEALTH(
  BOOL EN, 
  BOOL *__ENO, 
  HEALTH A, 
  HEALTH B);
static HEALTH ___VERDICT(
  BOOL EN, 
  BOOL *__ENO, 
  BOOL ANSWERED, 
  BOOL TRUSTED);
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
static void VENT_CONTROL_init__(VENT_CONTROL_data__ *data__, BOOL retain);
static void VENT_CONTROL_body__(VENT_CONTROL_data__ *data__);

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


static BOOL ___GOOD(
  BOOL EN, 
  BOOL *__ENO, 
  QUALITY Q)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  BOOL GOOD = __BOOL_LITERAL(FALSE);

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return GOOD;
  }
  GOOD = (Q == QUALITY__Q_GOOD);

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return GOOD;
}


static BOOL ___USABLE(
  BOOL EN, 
  BOOL *__ENO, 
  QUALITY Q)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  BOOL USABLE = __BOOL_LITERAL(FALSE);

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return USABLE;
  }
  USABLE = (Q != QUALITY__Q_NONE);

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return USABLE;
}


static QUALITY ___WORST_OF(
  BOOL EN, 
  BOOL *__ENO, 
  QUALITY A, 
  QUALITY B)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  QUALITY WORST_OF = QUALITY__Q_NONE;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return WORST_OF;
  }
  if (((A == QUALITY__Q_NONE) || (B == QUALITY__Q_NONE))) {
    WORST_OF = QUALITY__Q_NONE;
  } else if (((A == QUALITY__Q_HELD) || (B == QUALITY__Q_HELD))) {
    WORST_OF = QUALITY__Q_HELD;
  } else {
    WORST_OF = QUALITY__Q_GOOD;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return WORST_OF;
}


static QUALITY ___QUAL(
  BOOL EN, 
  BOOL *__ENO, 
  BOOL OK, 
  BOOL SEEDED)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  QUALITY QUAL = QUALITY__Q_NONE;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return QUAL;
  }
  if (OK) {
    QUAL = QUALITY__Q_GOOD;
  } else if (SEEDED) {
    QUAL = QUALITY__Q_HELD;
  } else {
    QUAL = QUALITY__Q_NONE;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return QUAL;
}


static HEALTH ___WORST_HEALTH(
  BOOL EN, 
  BOOL *__ENO, 
  HEALTH A, 
  HEALTH B)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  HEALTH WORST_HEALTH = HEALTH__HEALTHY;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return WORST_HEALTH;
  }
  if (((A == HEALTH__FAULTED) || (B == HEALTH__FAULTED))) {
    WORST_HEALTH = HEALTH__FAULTED;
  } else if (((A == HEALTH__DEGRADED) || (B == HEALTH__DEGRADED))) {
    WORST_HEALTH = HEALTH__DEGRADED;
  } else {
    WORST_HEALTH = HEALTH__HEALTHY;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return WORST_HEALTH;
}


static HEALTH ___VERDICT(
  BOOL EN, 
  BOOL *__ENO, 
  BOOL ANSWERED, 
  BOOL TRUSTED)
{
  BOOL ENO = __BOOL_LITERAL(TRUE);
  HEALTH VERDICT = HEALTH__HEALTHY;

  // Control execution
  if (!EN) {
    if (__ENO != NULL) {
      *__ENO = __BOOL_LITERAL(FALSE);
    }
    return VERDICT;
  }
  if (!(ANSWERED)) {
    VERDICT = HEALTH__FAULTED;
  } else if (!(TRUSTED)) {
    VERDICT = HEALTH__DEGRADED;
  } else {
    VERDICT = HEALTH__HEALTHY;
  };

  goto __end;

__end:
  if (__ENO != NULL) {
    *__ENO = ENO;
  }
  return VERDICT;
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
  __INIT_VAR(data__->Q_IN,QUALITY__Q_NONE,retain)
  __INIT_VAR(data__->TAU_S,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->HI,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->Q,QUALITY__Q_NONE,retain)
  __INIT_VAR(data__->AVG,0,retain)
  __INIT_VAR(data__->LAST,0,retain)
  __INIT_VAR(data__->PRIMED,__BOOL_LITERAL(FALSE),retain)
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

  __SET_VAR(data__->,Q,,___QUAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (BOOL)___GOOD(
      (BOOL)__BOOL_LITERAL(TRUE),
      NULL,
      (QUALITY)__GET_VAR(data__->Q_IN,)),
    (BOOL)__GET_VAR(data__->PRIMED,)));
  if (!(___GOOD(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (QUALITY)__GET_VAR(data__->Q_IN,)))) {
    __SET_VAR(data__->,LAST,,__GET_VAR(data__->NOW,));
    if (!(__GET_VAR(data__->PRIMED,))) {
      __SET_VAR(data__->,Y,,__GET_VAR(data__->LO,));
    };
    goto __end;
  };
  if (!(__GET_VAR(data__->PRIMED,))) {
    __SET_VAR(data__->,AVG,,__GET_VAR(data__->X,));
    __SET_VAR(data__->,PRIMED,,__BOOL_LITERAL(TRUE));
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
  __INIT_VAR(data__->V_Q,QUALITY__Q_NONE,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->AVG_MS,0,retain)
  __INIT_VAR(data__->BAND,0,retain)
  __INIT_VAR(data__->NOW,0,retain)
  __INIT_VAR(data__->Y,0,retain)
  __INIT_VAR(data__->Q,QUALITY__Q_NONE,retain)
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

  __SET_VAR(data__->,AVG.X,,__GET_VAR(data__->V,));
  __SET_VAR(data__->,AVG.Q_IN,,__GET_VAR(data__->V_Q,));
  __SET_VAR(data__->,AVG.TAU_S,,(___UINT_TO_REAL(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_VAR(data__->AVG_MS,)) / __REAL_LITERAL(1000.0)));
  __SET_VAR(data__->,AVG.NOW,,__GET_VAR(data__->NOW,));
  __SET_VAR(data__->,AVG.LO,,__REAL_LITERAL(0.0));
  __SET_VAR(data__->,AVG.HI,,__REAL_LITERAL(0.0));
  EWMA_body__(&data__->AVG);
  if (___GOOD(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (QUALITY)__GET_VAR(data__->V_Q,))) {
    __SET_VAR(data__->DB.,V,,__GET_VAR(data__->AVG.Y,));
    __SET_VAR(data__->DB.,BAND,,__GET_VAR(data__->BAND,));
    DEADBAND_body__(&data__->DB);
    __SET_VAR(data__->,SEEN,,__BOOL_LITERAL(TRUE));
  };
  __SET_VAR(data__->,Y,,__GET_VAR(data__->DB.Y,));
  __SET_VAR(data__->,Q,,__GET_VAR(data__->AVG.Q,));
  if (___GOOD(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (QUALITY)__GET_VAR(data__->V_Q,))) {
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





static void VENT_CONTROL_init__(VENT_CONTROL_data__ *data__, BOOL retain) {
  __INIT_EXTERNAL(REAL,AIR_TEMP_HI1,data__->AIR_TEMP_HI1,retain)
  __INIT_EXTERNAL(REAL,AIR_TEMP_HI2,data__->AIR_TEMP_HI2,retain)
  __INIT_EXTERNAL(REAL,AIR_TARGETMIN_DEGC,data__->AIR_TARGETMIN_DEGC,retain)
  __INIT_EXTERNAL(REAL,AIR_TARGETMAX_DEGC,data__->AIR_TARGETMAX_DEGC,retain)
  __INIT_EXTERNAL(REAL,AIR_VENTFULLDEM,data__->AIR_VENTFULLDEM,retain)
  __INIT_EXTERNAL(REAL,AIR_TEMP_IN,data__->AIR_TEMP_IN,retain)
  __INIT_EXTERNAL(REAL,VENT_TGT,data__->VENT_TGT,retain)
  __INIT_EXTERNAL(REAL,VENT_POS,data__->VENT_POS,retain)
  __INIT_EXTERNAL(REAL,VENT_DRIVE,data__->VENT_DRIVE,retain)
  __INIT_EXTERNAL(REAL,VENT_DRIVE2,data__->VENT_DRIVE2,retain)
  __INIT_EXTERNAL(HEALTH,PROG_VENT_HEALTH,data__->PROG_VENT_HEALTH,retain)
}

// Code part
static void VENT_CONTROL_body__(VENT_CONTROL_data__ *data__) {
  // Initialise TEMP variables

  if ((__GET_EXTERNAL(data__->AIR_TEMP_HI1,) > __GET_EXTERNAL(data__->AIR_TEMP_HI2,))) {
    __SET_EXTERNAL(data__->,AIR_TEMP_IN,,__GET_EXTERNAL(data__->AIR_TEMP_HI1,));
  } else {
    __SET_EXTERNAL(data__->,AIR_TEMP_IN,,__GET_EXTERNAL(data__->AIR_TEMP_HI2,));
  };
  __SET_EXTERNAL(data__->,VENT_TGT,,__GET_EXTERNAL(data__->AIR_TARGETMAX_DEGC,));
  if ((__GET_EXTERNAL(data__->VENT_TGT,) < __GET_EXTERNAL(data__->AIR_TARGETMIN_DEGC,))) {
    __SET_EXTERNAL(data__->,VENT_TGT,,__GET_EXTERNAL(data__->AIR_TARGETMIN_DEGC,));
  };
  if ((__GET_EXTERNAL(data__->AIR_TEMP_IN,) <= __GET_EXTERNAL(data__->VENT_TGT,))) {
    __SET_EXTERNAL(data__->,VENT_POS,,__REAL_LITERAL(0.0));
  } else if ((__GET_EXTERNAL(data__->AIR_TEMP_IN,) >= (__GET_EXTERNAL(data__->VENT_TGT,) + __GET_EXTERNAL(data__->AIR_VENTFULLDEM,)))) {
    __SET_EXTERNAL(data__->,VENT_POS,,__REAL_LITERAL(1.0));
  } else {
    __SET_EXTERNAL(data__->,VENT_POS,,((__GET_EXTERNAL(data__->AIR_TEMP_IN,) - __GET_EXTERNAL(data__->VENT_TGT,)) / __GET_EXTERNAL(data__->AIR_VENTFULLDEM,)));
  };
  __SET_EXTERNAL(data__->,VENT_DRIVE,,__GET_EXTERNAL(data__->VENT_POS,));
  __SET_EXTERNAL(data__->,VENT_DRIVE2,,__GET_EXTERNAL(data__->VENT_POS,));
  if ((___ABS(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (REAL)(__GET_EXTERNAL(data__->AIR_TEMP_HI1,) - __GET_EXTERNAL(data__->AIR_TEMP_HI2,))) > __REAL_LITERAL(10.0))) {
    __SET_EXTERNAL(data__->,PROG_VENT_HEALTH,,HEALTH__DEGRADED);
  } else {
    __SET_EXTERNAL(data__->,PROG_VENT_HEALTH,,HEALTH__HEALTHY);
  };

  goto __end;

__end:
  return;
} // VENT_CONTROL_body__() 





