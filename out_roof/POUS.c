static void TOF_MS_init__(TOF_MS_data__ *data__, BOOL retain);
static void TOF_MS_body__(TOF_MS_data__ *data__);
static void ROOF_LOCKOUT_init__(ROOF_LOCKOUT_data__ *data__, BOOL retain);
static void ROOF_LOCKOUT_body__(ROOF_LOCKOUT_data__ *data__);

static void TOF_MS_init__(TOF_MS_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->IN,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->PT,0,retain)
  __INIT_VAR(data__->DT_MS,0,retain)
  __INIT_VAR(data__->Q,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->ET,0,retain)
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

  if (__GET_VAR(data__->IN,)) {
    __SET_VAR(data__->,ET,,__UDINT_LITERAL(0));
    __SET_VAR(data__->,Q,,__BOOL_LITERAL(TRUE));
  } else if (__GET_VAR(data__->Q,)) {
    if ((__GET_VAR(data__->ET,) > (__UDINT_LITERAL(4000000000) - __GET_VAR(data__->DT_MS,)))) {
      __SET_VAR(data__->,ET,,__UDINT_LITERAL(4000000000));
    } else {
      __SET_VAR(data__->,ET,,(__GET_VAR(data__->ET,) + __GET_VAR(data__->DT_MS,)));
    };
    if ((__GET_VAR(data__->ET,) >= __GET_VAR(data__->PT,))) {
      __SET_VAR(data__->,Q,,__BOOL_LITERAL(FALSE));
    };
  };

  goto __end;

__end:
  return;
} // TOF_MS_body__() 





static void ROOF_LOCKOUT_init__(ROOF_LOCKOUT_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->RAIN_MMPH,0,retain)
  __INIT_VAR(data__->RAIN_MMPH_QLT,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->WIND_MPS,0,retain)
  __INIT_VAR(data__->WIND_MPS_QLT,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->GUST_MPS,0,retain)
  __INIT_VAR(data__->SHUT_BEGIN,0,retain)
  __INIT_VAR(data__->SHUT_END,0,retain)
  __INIT_VAR(data__->SHUT_MAX,0,retain)
  __INIT_VAR(data__->RAIN_HOLD_S,0,retain)
  __INIT_VAR(data__->SUPPLY_LOCK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->DT_MS,0,retain)
  __INIT_VAR(data__->RAIN_LOCK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->WIND_LOCK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->WINDOW_LOCK,__BOOL_LITERAL(FALSE),retain)
  __INIT_VAR(data__->CAP,0,retain)
  TOF_MS_init__(&data__->RAIN_FOR,retain);
}

// Code part
static void ROOF_LOCKOUT_body__(ROOF_LOCKOUT_data__ *data__) {
  // Initialise TEMP variables
  __SET_VAR(data__->,BLIND,,__BOOL_LITERAL(FALSE));
  __SET_VAR(data__->,W,,0);
  __SET_VAR(data__->,CEILING,,0);

  __SET_VAR(data__->,BLIND,,(!(__GET_VAR(data__->RAIN_MMPH_QLT,)) || !(__GET_VAR(data__->WIND_MPS_QLT,))));
  __SET_VAR(data__->RAIN_FOR.,IN,,(__GET_VAR(data__->RAIN_MMPH,) > __REAL_LITERAL(0.0)));
  __SET_VAR(data__->RAIN_FOR.,PT,,(___UINT_TO_UDINT(
    (BOOL)__BOOL_LITERAL(TRUE),
    NULL,
    (UINT)__GET_VAR(data__->RAIN_HOLD_S,)) * __UDINT_LITERAL(1000)));
  __SET_VAR(data__->RAIN_FOR.,DT_MS,,__GET_VAR(data__->DT_MS,));
  TOF_MS_body__(&data__->RAIN_FOR);
  __SET_VAR(data__->,RAIN_LOCK,,(__GET_VAR(data__->RAIN_FOR.Q,) || __GET_VAR(data__->BLIND,)));
  if ((__GET_VAR(data__->GUST_MPS,) > __GET_VAR(data__->WIND_MPS,))) {
    __SET_VAR(data__->,W,,__GET_VAR(data__->GUST_MPS,));
  } else {
    __SET_VAR(data__->,W,,__GET_VAR(data__->WIND_MPS,));
  };
  __SET_VAR(data__->,WIND_LOCK,,(__GET_VAR(data__->BLIND,) || (__GET_VAR(data__->W,) >= __GET_VAR(data__->SHUT_END,))));
  __SET_VAR(data__->,WINDOW_LOCK,,((__GET_VAR(data__->RAIN_LOCK,) || __GET_VAR(data__->WIND_LOCK,)) || __GET_VAR(data__->SUPPLY_LOCK,)));
  __SET_VAR(data__->,CEILING,,__REAL_LITERAL(1.0));
  if (((__GET_VAR(data__->W,) >= __GET_VAR(data__->SHUT_BEGIN,)) && (__GET_VAR(data__->SHUT_END,) > __GET_VAR(data__->SHUT_BEGIN,)))) {
    __SET_VAR(data__->,CEILING,,(__GET_VAR(data__->SHUT_MAX,) * (__REAL_LITERAL(1.0) - ((__GET_VAR(data__->W,) - __GET_VAR(data__->SHUT_BEGIN,)) / (__GET_VAR(data__->SHUT_END,) - __GET_VAR(data__->SHUT_BEGIN,))))));
  };
  if (__GET_VAR(data__->WINDOW_LOCK,)) {
    __SET_VAR(data__->,CAP,,__REAL_LITERAL(0.0));
  } else {
    __SET_VAR(data__->,CAP,,___LIMIT__REAL__REAL__REAL__REAL(
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





