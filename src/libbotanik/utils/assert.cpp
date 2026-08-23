#include "libbotanik/utils/assert.h"

#include <cstdint>

#include "libbotanik/hal/platform.h"
#include "libbotanik/logs/logger.h"

namespace Botanik {

    namespace {
        /** @brief The one sink, set once at setup. A function-local static so
         *  there is no init-order dependency on a translation unit. */
        Logs::Logger*& _sink() {
            static Logs::Logger* logger = nullptr;
            return logger;
        }
    }


    void set_assert_logger(Logs::Logger* logger) { _sink() = logger; }

    void assert_fail(const char* expr, const char* file, int line) {
        Logs::Logger* logger = _sink();
        if (logger != nullptr)
            logger->critical(Logs::ASSERT, "assertion failed:\n%s (%s:%d)",
                             expr, file, line);
        // Break for the debugger, then keep running - the same on the simulator
        // as on hardware. The CRITICAL is the report.
        if (HAL::Debug::under_debugger()) HAL::Debug::breakpoint();
    }

}
