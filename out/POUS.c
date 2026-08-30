static void STAIRCASE3_init__(STAIRCASE3_data__ *data__, BOOL retain);
static void STAIRCASE3_body__(STAIRCASE3_data__ *data__);
static void DESTRAT_init__(DESTRAT_data__ *data__, BOOL retain);
static void DESTRAT_body__(DESTRAT_data__ *data__);

static void STAIRCASE3_init__(STAIRCASE3_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->EN,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->ENO,__BOOL_LITERAL(TRUE),retain)
  __INIT_VAR(data__->IN,0,retain)
  __INIT_VAR(data__->LO,0,retain)
  __INIT_VAR(data__->MED,0,retain)
  __INIT_VAR(data__->HI,0,retain)
  __INIT_VAR(data__->HYST,0,retain)
  __INIT_VAR(data__->Q,0,retain)
  __INIT_VAR(data__->BAND,__USINT_LITERAL(0),retain)
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

  if ((__GET_VAR(data__->IN,) >= __GET_VAR(data__->HI,))) {
    __SET_VAR(data__->,BAND,,__USINT_LITERAL(3));
  } else if ((__GET_VAR(data__->IN,) >= __GET_VAR(data__->MED,))) {
    __SET_VAR(data__->,BAND,,__USINT_LITERAL(2));
  } else if ((__GET_VAR(data__->IN,) >= __GET_VAR(data__->LO,))) {
    __SET_VAR(data__->,BAND,,__USINT_LITERAL(1));
  } else if ((__GET_VAR(data__->IN,) < (__GET_VAR(data__->LO,) - __GET_VAR(data__->HYST,)))) {
    __SET_VAR(data__->,BAND,,__USINT_LITERAL(0));
  };
  __SET_VAR(data__->,Q,,__GET_VAR(data__->BAND,));

  goto __end;

__end:
  return;
} // STAIRCASE3_body__() 





static void DESTRAT_init__(DESTRAT_data__ *data__, BOOL retain) {
  __INIT_VAR(data__->STRAT,0,retain)
  __INIT_VAR(data__->LOW,0,retain)
  __INIT_VAR(data__->MED,0,retain)
  __INIT_VAR(data__->HIGH,0,retain)
  __INIT_VAR(data__->HYST,0,retain)
  __INIT_VAR(data__->FAN_LOW,0,retain)
  __INIT_VAR(data__->FAN_MED,0,retain)
  __INIT_VAR(data__->FAN_HIGH,0,retain)
  __INIT_VAR(data__->DEMAND,0,retain)
  STAIRCASE3_init__(&data__->BANDS,retain);
}

// Code part
static void DESTRAT_body__(DESTRAT_data__ *data__) {
  // Initialise TEMP variables

  __SET_VAR(data__->BANDS.,IN,,__GET_VAR(data__->STRAT,));
  __SET_VAR(data__->BANDS.,LO,,__GET_VAR(data__->LOW,));
  __SET_VAR(data__->BANDS.,MED,,__GET_VAR(data__->MED,));
  __SET_VAR(data__->BANDS.,HI,,__GET_VAR(data__->HIGH,));
  __SET_VAR(data__->BANDS.,HYST,,__GET_VAR(data__->HYST,));
  STAIRCASE3_body__(&data__->BANDS);
  {
    USINT __case_expression = __GET_VAR(data__->BANDS.Q,);
    if ((__case_expression == __USINT_LITERAL(1))) {
      __SET_VAR(data__->,DEMAND,,__GET_VAR(data__->FAN_LOW,));
    }
    else if ((__case_expression == __USINT_LITERAL(2))) {
      __SET_VAR(data__->,DEMAND,,__GET_VAR(data__->FAN_MED,));
    }
    else if ((__case_expression == __USINT_LITERAL(3))) {
      __SET_VAR(data__->,DEMAND,,__GET_VAR(data__->FAN_HIGH,));
    }
    else {
      __SET_VAR(data__->,DEMAND,,__REAL_LITERAL(0.0));
    }
  };

  goto __end;

__end:
  return;
} // DESTRAT_body__() 





