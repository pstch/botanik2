#include "libbotanik/utils/assert.h"

#include <cstdint>

#include "libbotanik/hal/platform.h"

namespace Botanik {

    namespace {
        /** @brief The one sink, set once at setup. A function-local static so
         *  there is no init-order dependency on a translation unit. */
        AssertSink& _sink() {
            static AssertSink sink = nullptr;
            return sink;
        }
    }


    void set_assert_sink(AssertSink sink) { _sink() = sink; }

    void assert_fail(const char* expr, const char* file, int line) {
        AssertSink sink = _sink();
        if (sink != nullptr) sink(expr, file, line);
        // Break for the debugger, then keep running - the same on the simulator
        // as on hardware. The CRITICAL is the report.
        if (HAL::Debug::under_debugger()) HAL::Debug::breakpoint();
    }

}
