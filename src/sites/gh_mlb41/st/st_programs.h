/**
 * @file st_programs.h
 * @brief One entry per ST program: what it is called, how often it runs.
 */

#ifndef BOTANIK_SITE_ST_PROGRAMS_H
#define BOTANIK_SITE_ST_PROGRAMS_H

#include <cstdint>

#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"

namespace Site {

    /** @brief What the runtime needs to treat a generated POU as a program.
     *
     *  `init` and `run` are trampolines: matiec's bodies each take their own
     *  `_data__` type, so a table of them needs one wrapper apiece. They are
     *  defined in the only translation unit that can see the generated
     *  statics.
     */
    struct StProgram {
        const char* name;        ///< the `prog_<name>` row, and the task label
        uint32_t    period_ms;   ///< how often, stated here and nowhere else
        void      (*init)();     ///< the generated bind
        void      (*run)();      ///< the generated law
        /** @brief The registry handler. Its own per program, because a Tick
         *  does not say which task it belongs to and the roster is the only
         *  place the index is a compile-time constant. */
        void      (*task)(Botanik::Component*, const Botanik::Tasks::Tick&);
    };

    extern const StProgram st_programs[];
    extern const uint8_t   ST_PROGRAM_COUNT;

}  // namespace Site

#endif
