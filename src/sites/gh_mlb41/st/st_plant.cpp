/**
 * @file st_plant.cpp
 * @brief HAND-WRITTEN - the program roster, in the shape vars.cpp uses.
 *
 * The only translation unit that compiles matiec's resource, because the
 * generated bodies are `static` in it. That is also why the trampolines are
 * here: a table of function pointers needs one wrapper per POU, and only this
 * file can see what to wrap.
 *
 * The roster is stated ONCE, as an X-macro, and expanded twice - trampolines,
 * then the table. Adding a program is one line here and one in plant.st.
 */

#include "st_programs.h"
#include "st_runtime.h"
#include "st_bank.h"
#include "resolve_ci.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wshadow"
#include "generated/plc.c"
#pragma GCC diagnostic pop

/* Named by every generated read, write and resolve. Defined here because this
 * is the translation unit the generated code is compiled in. */
namespace Botanik { namespace St { Vars::Bank* io = 0; } }

namespace Site {

/** @brief The roster. `POU` is the ST program's name upper-cased, `INST` the
 *  instance `plant.st` gives it, and the period is this table's to state -
 *  the RESOURCE's TASK bindings do not reach here.
 *
 *  ORDER IS EXECUTION ORDER within the beat, as it was when these were C++
 *  programs: conditioning first, then the laws, then the actuators. */
#define SITE_ST_PROGRAM_LIST(X)                                                \
    X(temp_rh_sensors,  TEMP_RH_SENSORS,        SENSORS,  BOTANIK_SITE_SENSE_MS)   \
    X(strat,            STRAT_SENSOR,           STRAT,    BOTANIK_SITE_SENSE_MS)   \
    X(climate_watch,    CLIMATE_WATCH,          CLIMWATCH, BOTANIK_SITE_CONTROL_MS)\
    X(weather_watch,    WEATHER_WATCH,          WXWATCH,  BOTANIK_SITE_CONTROL_MS) \
    X(day_phase,        DAY_PHASE_CONTROLLER,   DAYPHASE, BOTANIK_SITE_CONTROL_MS) \
    X(target_temp,      TARGET_TEMP_CONTROLLER, TARGET,   BOTANIK_SITE_CONTROL_MS) \
    X(humidity,         HUMIDITY_CONTROLLER,    HUMIDITY, BOTANIK_SITE_CONTROL_MS) \
    X(intake_dry,       INTAKE_DRY_GATE,        DRYGATE,  BOTANIK_SITE_CONTROL_MS) \
    X(intake_gates,     INTAKE_GATES,           GATES,    BOTANIK_SITE_CONTROL_MS) \
    X(destrat,          DESTRAT_CONTROLLER,     DESTRAT,  BOTANIK_SITE_CONTROL_MS) \
    X(roof_lockout,     ROOF_LOCKOUT,           ROOF,     BOTANIK_SITE_CONTROL_MS) \
    X(intractors,       INTRACTOR_CONTROLLER,   INTRACTORS, BOTANIK_SITE_CONTROL_MS)\
    X(fans,             FAN_CONTROLLER,         FANS,     BOTANIK_SITE_CONTROL_MS)

/* The roster's own index, so a handler knows which entry it is. */
enum {
#define X(name, POU, INST, PERIOD) IDX_##INST,
    SITE_ST_PROGRAM_LIST(X)
#undef X
};

#define X(name, POU, INST, PERIOD)                                             \
    static void _init_##INST() { POU##_init__(&INST, 0); }                     \
    static void _run_##INST()  { POU##_body__(&INST); }                        \
    static void _task_##INST(Botanik::Component* self,                         \
                             const Botanik::Tasks::Tick& t) {                  \
        static_cast<StRuntime*>(self)->scan(IDX_##INST, t.now_ms);             \
    }
SITE_ST_PROGRAM_LIST(X)
#undef X

const StProgram st_programs[] = {
#define X(name, POU, INST, PERIOD)                                             \
    { #name, PERIOD, &_init_##INST, &_run_##INST, &_task_##INST },
    SITE_ST_PROGRAM_LIST(X)
#undef X
};

const uint8_t ST_PROGRAM_COUNT =
    (uint8_t)(sizeof(st_programs) / sizeof(st_programs[0]));

}  // namespace Site
