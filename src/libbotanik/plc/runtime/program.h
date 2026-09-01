/**
 * @file program.h
 * @brief One entry per ST program: what it is called, how often it runs.
 */

#ifndef BOTANIK_PLC_RUNTIME_PROGRAM_H
#define BOTANIK_PLC_RUNTIME_PROGRAM_H

#include <cstdint>
#include "libbotanik/config/base.h"

#include "libbotanik/core/component.h"
#include "libbotanik/tasks/tasks.h"

#if BOTANIK_FEATURE_PLC

namespace Botanik { namespace Plc { namespace Runtime {

    /** @brief What the runtime needs to treat a generated POU as a program.
     *
     *  `init` and `run` are trampolines: matiec's bodies each take their own
     *  `_data__` type, so a table of them needs one wrapper apiece. They are
     *  defined in the only translation unit that can see the generated
     *  statics.
     */
    struct Program {
        const char* name;        ///< the `prog_<name>` row, and the task label
        uint32_t    period_ms;   ///< how often, stated here and nowhere else
        void      (*init)();     ///< the generated bind
        void      (*run)();      ///< the generated law
        /** @brief The registry handler. Its own per program, because a Tick
         *  does not say which task it belongs to and the roster is the only
         *  place the index is a compile-time constant. */
        void      (*task)(Botanik::Component*, const Botanik::Tasks::Tick&);
        /** @brief The rows this program is the AUTHOR of: its plain
         *  `VAR_EXTERNAL` names, generated from the ST itself. The bank cannot
         *  answer this - an ST row resolves as VAR_IN_OUT and so claims no
         *  exclusive writer - and these are what a program out of the scan
         *  drops to their declared safe state. */
        const char* const* writes;
        uint8_t            write_count;
    };

    extern const Program programs[];
    extern const uint8_t PROGRAM_COUNT;

}}}  // namespace Botanik::Plc::Runtime

#endif // BOTANIK_FEATURE_PLC

#endif
